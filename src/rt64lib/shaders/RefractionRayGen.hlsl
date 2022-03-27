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
#include "Fog.hlsli"
#include "BgSky.hlsli"

#include "Common.hlsli"

[shader("raygeneration")]
void RefractionRayGen() {
	uint2 launchIndex = DispatchRaysIndex().xy;
	uint2 launchDims = DispatchRaysDimensions().xy;
	int instanceId = gInstanceId[launchIndex];
	float refractionAlpha = gRefraction[launchIndex].a;
	if ((instanceId < 0) || (refractionAlpha <= EPSILON)) {
		return;
	}

	// Grab the ray origin and direction from the buffers.
	float3 rayOrigin = gShadingPosition[launchIndex].xyz;
	float3 viewDirection = gViewDirection[launchIndex].xyz;
	float3 shadingNormal = gShadingNormal[launchIndex].xyz;
	float refractionFactor = instanceMaterials[instanceId].refractionFactor;
    float3 rayDirection = refract(viewDirection, shadingNormal, refractionFactor);
			
	// Roughness
    if (gShadingRoughness[launchIndex] >= EPSILON) {
        rayDirection = normalize(getCosHemisphereSampleBlueNoise(launchIndex, frameCount, gShadingRoughness[launchIndex] / 3.0f, rayDirection));
    }

	// Mix background and sky color together.
	float2 screenUV = (float2)(launchIndex) / (float2)(launchDims);
	float3 bgColor = SampleBackground2D(screenUV);
	float4 skyColor = SampleSky2D(screenUV);
	bgColor = lerp(bgColor, skyColor.rgb, skyColor.a);

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

	// Process hits.
	float3 resPosition = float3(0.0f, 0.0f, 0.0f);
	float3 resNormal = float3(0.0f, 0.0f, 0.0f);
	float3 resSpecular = float3(0.0f, 0.0f, 0.0f);
	float3 resEmissive = float3(0.0f, 0.0f, 0.0f);
    float resRoughness = 0.0;
	int resInstanceId = -1;
	float4 resColor = float4(0.0f, 0.0f, 0.0f, 1.0f);
	float3 resTransparent = float3(0.0f, 0.0f, 0.0f);
	for (uint hit = 0; hit < payload.nhits; hit++) {
		uint hitBufferIndex = getHitBufferIndex(hit, launchIndex, launchDims);
		float4 hitColor = gHitColor[hitBufferIndex];
		float alphaContrib = (resColor.a * hitColor.a);
		if (alphaContrib >= EPSILON) {
			uint hitInstanceId = gHitInstanceId[hitBufferIndex];
			bool usesLighting = (instanceMaterials[hitInstanceId].lightGroupMaskBits > 0);
            float3 vertexPosition = rayOrigin + rayDirection * WithoutDistanceBias(gHitDistAndFlow[hitBufferIndex].x, hitInstanceId);
            float3 vertexEmissive = gHitEmissive[hitBufferIndex].rgb;
            float3 emissive = vertexEmissive * instanceMaterials[hitInstanceId].selfLight;

			// Calculate the fog for the resulting color using the camera data if the option is enabled.
			/*
			if (instanceMaterials[hitInstanceId].fogEnabled) {
				float4 fogColor = ComputeFogFromCamera(hitInstanceId, vertexPosition);
				resTransparent += fogColor.rgb * fogColor.a * alphaContrib;
				alphaContrib *= (1.0f - fogColor.a);
            } */
			
			// Preliminary implementation of scene-driven fog 
			{ 
                float3 cameraPos = mul(viewI, float4(0, 0, 0, 1)).xyz;
                float4 fogColor = SceneFogFromOrigin(vertexPosition, cameraPos, ambientFogFactors.x, ambientFogFactors.y, ambientFogColor);
                float4 groundFog = SceneGroundFogFromOrigin(vertexPosition, cameraPos, groundFogFactors.x, groundFogFactors.y, groundFogHeightFactors.x, groundFogHeightFactors.y, groundFogColor);
                float4 combinedColor = float4(0.f, 0.f, 0.f, 0.f);
                combinedColor = BlendAOverB(fogColor, groundFog);
                resTransparent += combinedColor.rgb * combinedColor.a * alphaContrib;
                alphaContrib *= (1.0f - fogColor.a);
            }

			if (usesLighting) {
				float3 vertexNormal = gHitNormal[hitBufferIndex].xyz;
				float3 vertexSpecular = gHitSpecular[hitBufferIndex].rgb;
				float vertexRoughness = gHitRoughness[hitBufferIndex];
				float3 specular = instanceMaterials[hitInstanceId].specularColor * vertexSpecular.rgb;
                float roughness = instanceMaterials[hitInstanceId].roughnessFactor * vertexRoughness;
				resColor.rgb += hitColor.rgb * alphaContrib;
				resPosition = vertexPosition;
				resNormal = vertexNormal;
				resSpecular = specular;
                resRoughness = roughness;
				resInstanceId = hitInstanceId;
                resEmissive += emissive;
            }
			else {
                resTransparent += LinearToSrgb(SrgbToLinear(hitColor.rgb) * alphaContrib * (ambientBaseColor.rgb + ambientNoGIColor.rgb + emissive));
            }

			resColor.a *= (1.0 - hitColor.a);
		}

		if (resColor.a <= EPSILON) {
			break;
		}
	}

	if (resInstanceId >= 0) {
        float2x3 lightMatrix = ComputeLightsRandom(launchIndex, rayDirection, resInstanceId, resPosition, resNormal, resSpecular, resRoughness, rayOrigin, 1, instanceMaterials[instanceId].lightGroupMaskBits, instanceMaterials[instanceId].ignoreNormalFactor, true);
        float3 directLight = lightMatrix._11_12_13 + resEmissive;
        float3 specularLight = lightMatrix._21_22_23;
        resColor.rgb = LinearToSrgb(SrgbToLinear(resColor.rgb) * (gIndirectLightAccum[launchIndex].rgb + directLight) + specularLight);
    }

	// Blend with the background.
	resColor.rgb += bgColor * resColor.a + resTransparent;
	resColor.a = 1.0f;

	// Add refraction result.
	gRefraction[launchIndex].rgb += resColor.rgb * refractionAlpha;
}