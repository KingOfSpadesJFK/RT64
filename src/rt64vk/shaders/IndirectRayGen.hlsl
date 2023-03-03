//
// RT64
//

#include "Color.hlsli"
#include "Constants.hlsli"
#include "GlobalBuffers.hlsli"
#include "GlobalHitBuffers.hlsli"
#include "GlobalParams.hlsli"
#include "Materials.hlsli"
#include "Instances.hlsli"
#include "Ray.hlsli"
#include "Random.hlsli"
#include "Textures.hlsli"
#include "Lights.hlsli"
#include "BgSky.hlsli"

float3 getCosHemisphereSampleBlueNoise(uint2 pixelPos, uint frameCount, float3 hitNorm) {
	float2 randVal = getBlueNoise(pixelPos, frameCount).rg;

	// Cosine weighted hemisphere sample from RNG
	float3 bitangent = getPerpendicularVector(hitNorm);
	float3 tangent = cross(bitangent, hitNorm);
	float r = sqrt(randVal.x);
	float phi = 2.0f * 3.14159265f * randVal.y;

	// Get our cosine-weighted hemisphere lobe sample direction
	return tangent * (r * cos(phi).x) + bitangent * (r * sin(phi)) + hitNorm.xyz * sqrt(max(0.0, 1.0f - randVal.x));
}

struct PushConstant { 
    float giBounceDivisor;
    float giResolutionScale;
	uint giBounce;
};

[[vk::push_constant]] PushConstant pc;

[shader("raygeneration")]
void IndirectRayGen() {
	uint2 launchIndex = DispatchRaysIndex().xy;
	uint2 scaledLaunchIndex = (launchIndex * pc.giResolutionScale);
	if (pc.giResolutionScale > 1.0f){
		uint2 launchIndexOffset = getBlueNoise(launchIndex, frameCount + pc.giBounce).rg * pc.giResolutionScale;
		scaledLaunchIndex += launchIndexOffset;
	}
	int instanceId = gInstanceId[scaledLaunchIndex];
	if ((instanceId < 0)) {
		return;			// Skip if the instance ID is invalid
	}
	uint2 launchDims = DispatchRaysDimensions().xy;
	float3 rayOrigin = gShadingPosition[scaledLaunchIndex].xyz;
	float3 shadingNormal = gShadingNormal[scaledLaunchIndex].xyz;
	float3 newIndirect = float3(0.0f, 0.0f, 0.0f);
	float historyLength = 0.0f;

	// Reproject previous indirect.
	if (giReproject) {
		const float WeightNormalExponent = 128.0f;
		float2 flow = gFlow[scaledLaunchIndex].xy;
		int2 prevIndex = int2(scaledLaunchIndex + float2(0.5f, 0.5f)+ flow);
		float prevDepth = gPrevDepth[prevIndex];
		float3 prevNormal = gPrevNormal[prevIndex].xyz;
		float4 prevIndirectAccum = gPrevIndirectLightAccum[prevIndex];
		float depth = gDepth[scaledLaunchIndex];
		float weightDepth = abs(depth - prevDepth) / 0.01f;
		float weightNormal = pow(max(0.0f, dot(prevNormal, shadingNormal)), WeightNormalExponent);
		float historyWeight = exp(-weightDepth) * weightNormal;
		newIndirect = prevIndirectAccum.rgb;
		historyLength = prevIndirectAccum.a * historyWeight;
	}

	float3 hitPosition = rayOrigin;
	float3 hitNormal = shadingNormal;
	int hitInstanceId = -1;
	uint maxSamples = giSamples;
	const uint blueNoiseMult = 64 / giSamples;
	while (maxSamples > 0) {
		float3 rayDirection = getCosHemisphereSampleBlueNoise(launchIndex, frameCount + pc.giBounce + maxSamples * blueNoiseMult, shadingNormal);

		// Ray differential.
		RayDiff rayDiff;
		rayDiff.dOdx = float3(0.0f, 0.0f, 0.0f);
		rayDiff.dOdy = float3(0.0f, 0.0f, 0.0f);
		rayDiff.dDdx = float3(0.0f, 0.0f, 0.0f);
		rayDiff.dDdy = float3(0.0f, 0.0f, 0.0f);

		// Trace.
		RayDesc ray;
		ray.Origin = rayOrigin;
		ray.Direction = rayDirection;
		ray.TMin = RAY_MIN_DISTANCE;
		ray.TMax = RAY_MAX_DISTANCE;
		HitInfo payload;
		payload.nhits = 0;
		payload.rayDiff = rayDiff;
		TraceRay(SceneBVH, RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, 0, 0, 0, ray, payload);

		// Mix background and sky color together.
		float3 bgColor = SrgbToLinear(SampleBackgroundAsEnvMap(rayDirection));
		float4 skyColor = SrgbToLinear(SampleSkyPlane(rayDirection));
		bgColor = lerp(bgColor, skyColor.rgb, skyColor.a);

		// Process hits.
		float3 resPosition = rayOrigin;
		float3 resNormal = float3(0.0f, 0.0f, 0.0f);
		float3 resSpecular = float3(0.0f, 0.0f, 0.0f);
		float4 resColor = float4(0, 0, 0, 1);
		int resInstanceId = -1;
		for (uint hit = 0; hit < payload.nhits; hit++) {
			uint hitBufferIndex = getHitBufferIndex(hit, launchIndex, launchDims);
			float4 hitColor = gHitColor[hitBufferIndex];
			float alphaContrib = (resColor.a * hitColor.a);
			if (alphaContrib >= EPSILON) {
				uint instanceId = gHitInstanceId[hitBufferIndex];
				float3 vertexPosition = rayOrigin + rayDirection * WithoutDistanceBias(gHitDistAndFlow[hitBufferIndex].x, instanceId);
				float3 vertexNormal = gHitNormal[hitBufferIndex].xyz;
				float3 vertexSpecular = gHitSpecular[hitBufferIndex].rgb;
				float3 specular = instanceMaterials[instanceId].specularColor * vertexSpecular.rgb;
				resColor.rgb += hitColor.rgb * alphaContrib;
				resColor.a *= (1.0 - hitColor.a);
				resPosition = vertexPosition;
				resNormal = vertexNormal;
				resSpecular = specular;
				resInstanceId = instanceId;
			}

			if (resColor.a <= EPSILON) {
				break;
			}
		}

		// Add diffuse bounce as indirect light.
		float3 resIndirect = ambientNoGIColor.rgb / (giBounces + giSamples);
		if (resInstanceId >= 0) {
			float3 directLight = ComputeLightsRandom(launchIndex, rayDirection, resInstanceId, resPosition, resNormal, resSpecular, 1, true) + instanceMaterials[resInstanceId].selfLight;
			float3 indirectLight = (SrgbToLinear(resColor.rgb) * (1.0f - resColor.a) * (directLight)) * giDiffuseStrength;
			resIndirect += indirectLight;
			resIndirect += ambientBaseColor.rgb * resColor.a;
		}

		resIndirect += bgColor * giSkyStrength * resColor.a;

		// Accumulate.
		historyLength = min(historyLength + 1.0f, 64.0f);
		newIndirect = lerp(newIndirect.rgb, resIndirect, 1.0f / historyLength);

		// New positions and normals
		if (resInstanceId >= 0) {
			hitPosition = resPosition;
			hitNormal = resNormal;
			hitInstanceId = resInstanceId;
		}

		maxSamples--;
	}
	
	// Store the new positions and normals
	if (giBounces > 1) {
		gShadingPosition[scaledLaunchIndex] = float4(hitPosition, 0.0f);
		gShadingNormal[scaledLaunchIndex] = float4(hitNormal, 0.0f);
		gInstanceId[scaledLaunchIndex] = hitInstanceId;
	}

	gIndirectLightAccum[scaledLaunchIndex] += float4(newIndirect / pc.giBounceDivisor, historyLength);
}