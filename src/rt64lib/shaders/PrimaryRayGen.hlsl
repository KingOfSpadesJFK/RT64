//
// RT64
//

#include "Color.hlsli"
#include "Constants.hlsli"
#include "GlobalBuffers.hlsli"
#include "GlobalParams.hlsli"
#include "GlobalHitBuffers.hlsli"
#include "Materials.hlsli"
#include "Instances.hlsli"
#include "Random.hlsli"
#include "Ray.hlsli"
#include "Textures.hlsli"
#include "BgSky.hlsli"
#include "Lights.hlsli"
#include "Fog.hlsli"

#include "Common.hlsli"

float FresnelReflectAmount(float3 normal, float3 incident, float reflectivity, float fresnelMultiplier)
{
	// TODO: Probably use a more accurate approximation than this.
    float ret = pow(max(1.0f + dot(normal, incident), EPSILON), 5.0f);
    return reflectivity + ((1.0 - reflectivity) * ret * fresnelMultiplier);
}

float2 WorldToScreenPos(float4x4 viewProj, float3 worldPos) {
	float4 clipSpace = mul(viewProj, float4(worldPos, 1.0f));
	float3 NDC = clipSpace.xyz / clipSpace.w;
	return (0.5f + NDC.xy / 2.0f);
}

[shader("raygeneration")]
void PrimaryRayGen() {
	uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDims = DispatchRaysDimensions().xy;
    int instanceId = gInstanceId[launchIndex];
	float2 d = (((launchIndex.xy + 0.5f + pixelJitter) / float2(launchDims)) * 2.f - 1.f);
	float3 nonNormRayDir = d.x * cameraU.xyz + d.y * cameraV.xyz + cameraW.xyz;
	float4 target = mul(projectionI, float4(d.x, -d.y, 1, 1));
	float3 rayOrigin = mul(viewI, float4(0, 0, 0, 1)).xyz;
    float3 rayDirection = mul(viewI, float4(target.xyz, 0)).xyz;
    bool lightShafts = processingFlags & RT64_VIEW_VOLUMETRICS_FLAG;

	// Initialize the buffers.
	gViewDirection[launchIndex] = float4(rayDirection, 0.0f);
	gReflection[launchIndex] = float4(0.0f, 0.0f, 0.0f, 0.0f);
	gRefraction[launchIndex] = float4(0.0f, 0.0f, 0.0f, 0.0f);

	// Sample the background.
	float2 screenUV = float2(launchIndex.x, launchIndex.y) / float2(launchDims.x, launchDims.y);
    float3 bgPosition = rayOrigin + rayDirection * RAY_MAX_DISTANCE;
	float2 prevBgPos = WorldToScreenPos(prevViewProj, bgPosition);
    float2 curBgPos = WorldToScreenPos(viewProj, bgPosition);
    float3 bgColor = SampleBackground2D(screenUV);
    float4 skyColor = SampleSky2D(screenUV);
    bgColor = lerp(bgColor, skyColor.rgb, skyColor.a);
	
	// Calculate fog in background
	{ 
        float4 fogColor = SceneFogFromOrigin(bgPosition, rayOrigin, ambientFogFactors.x, ambientFogFactors.y, ambientFogColor);
        gFog[launchIndex] = fogColor;
    }

	// Compute ray differentials.
	RayDiff rayDiff;
	rayDiff.dOdx = float3(0.0f, 0.0f, 0.0f);
	rayDiff.dOdy = float3(0.0f, 0.0f, 0.0f);
	computeRayDiffs(nonNormRayDir, cameraU.xyz, cameraV.xyz, resolution.zw, rayDiff.dDdx, rayDiff.dDdy);

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
    float4 resNormal = float4(-rayDirection, 0.0);
	float3 resSpecular = float3(0.0f, 0.0f, 0.0f);
    float3 resEmissive = float3(0.0f, 0.0f, 0.0f);
    float resRoughness = 0.0f;
    float resMetalness = 0.0f;
    float resAmbient = 0.0f;
	float3 resTransparent = float3(0.0f, 0.0f, 0.0f);
    float3 resTransparentLight = float3(0.0f, 0.0f, 0.0f);
	bool resTransparentLightComputed = false;
	float4 resColor = float4(0, 0, 0, 1);
	float2 resFlow = (curBgPos - prevBgPos) * resolution.xy;
    float resDepth = 1.0f;
    float historyLength = 0.0f;
	int resInstanceId = -1;
	for (uint hit = 0; hit < payload.nhits; hit++) {
		uint hitBufferIndex = getHitBufferIndex(hit, launchIndex, launchDims);
		float4 hitColor = gHitColor[hitBufferIndex];
		float alphaContrib = (resColor.a * hitColor.a);
		if (alphaContrib >= EPSILON) {
			uint instanceId = gHitInstanceId[hitBufferIndex];
			bool usesLighting = (instanceMaterials[instanceId].lightGroupMaskBits > 0);
			bool applyLighting = usesLighting && (hitColor.a > APPLY_LIGHTS_MINIMUM_ALPHA);
            float vertexDistance = WithoutDistanceBias(gHitDistAndFlow[hitBufferIndex].x, instanceId);
            float3 vertexPosition = rayOrigin + rayDirection * vertexDistance;
            float3 vertexNormal = gHitNormal[hitBufferIndex].xyz;
			float3 vertexSpecular = gHitSpecular[hitBufferIndex].rgb;
			float vertexRoughness = gHitRoughness[hitBufferIndex];
			float vertexMetalness = gHitMetalness[hitBufferIndex];
			float vertexAmbientOcclusion = gHitAmbient[hitBufferIndex];
            float3 normal = vertexNormal;
			float3 specular = instanceMaterials[instanceId].specularColor * vertexSpecular.rgb;
			float3 emissive = gHitEmissive[hitBufferIndex].rgb * instanceMaterials[instanceId].selfLight;
            float roughness = vertexRoughness * instanceMaterials[instanceId].roughnessFactor;
            float metalness = vertexMetalness * instanceMaterials[instanceId].metallicFactor;
            float reflectionFactor = instanceMaterials[instanceId].reflectionFactor;
            float refractionFactor = instanceMaterials[instanceId].refractionFactor;
            specular = specular * (1.0f - metalness) + hitColor.rgb * metalness;
			   
			// Calculate the fog for the resulting color using the camera data if the option is enabled.
			bool storeHit = false;
			/*   
			if (instanceMaterials[instanceId].fogEnabled) {
				float4 fogColor = ComputeFogFromCamera(instanceId, vertexPosition);
				resTransparent += fogColor.rgb * fogColor.a * alphaContrib;
				alphaContrib *= (1.0f - fogColor.a);   
			}  
			*/ 
			// Scene-driven fog. Volumetric lighting is on a different shader 
			{ 
                float4 fogColor = SceneFogFromOrigin(vertexPosition, rayOrigin, ambientFogFactors.x, ambientFogFactors.y, ambientFogColor);
                float4 groundFog = SceneGroundFogFromOrigin(vertexPosition, rayOrigin, groundFogFactors.x, groundFogFactors.y, groundFogHeightFactors.x, groundFogHeightFactors.y, groundFogColor);
                float4 combinedColor = float4(0.f, 0.f, 0.f, 0.f);
                combinedColor = BlendAOverB(fogColor, groundFog);
                gFog[launchIndex] = combinedColor;
            } 

			// Reflection.
            if (reflectionFactor > EPSILON)
            {
				float reflectionFresnelFactor = instanceMaterials[instanceId].reflectionFresnelFactor;
                float fresnelAmount = FresnelReflectAmount(vertexNormal, rayDirection, reflectionFactor, reflectionFresnelFactor);
				
                gReflection[launchIndex].a = fresnelAmount * alphaContrib;
                if (instanceMaterials[instanceId].metalnessTexIndex >= 0) {
                    gReflection[launchIndex].a *= metalness;
                }
                storeHit = true;
            }

			// Add the color to the hit color or the transparent buffer if the lighting is disabled.
			float3 resColorAdd = hitColor.rgb * alphaContrib;
			if (applyLighting) {
				storeHit = true;
                resColor.rgb += resColorAdd;
            }
			// Expensive case: transparent geometry that is not solid enough to act as the main
			// instance in the deferred pass and it also needs lighting to work correctly.
			// We sample one light at random and use it for any other transparent geometry that
			// has the same problem.
			else if (usesLighting) {
				if (!resTransparentLightComputed) {
                    float2x3 lightMatrix = ComputeLightsRandom(launchIndex, rayDirection, instanceId, vertexPosition, vertexNormal, specular, roughness, rayOrigin, 1, instanceMaterials[instanceId].lightGroupMaskBits, instanceMaterials[instanceId].ignoreNormalFactor, true);
                    resTransparentLight = lightMatrix._11_12_13 + lightMatrix._21_22_23;
					resTransparentLightComputed = true;
				}

                resTransparent += LinearToSrgb(SrgbToLinear(resColorAdd) * (ambientBaseColor.rgb + ambientNoGIColor.rgb + emissive + resTransparentLight));
            }
			// Cheap case: we ignore the geometry entirely from the lighting pass and just add
			// it to the transparency buffer directly.
			else {
                resTransparent += LinearToSrgb(SrgbToLinear(resColorAdd) * (ambientBaseColor.rgb + ambientNoGIColor.rgb + emissive));
            }

			resColor.a *= (1.0 - hitColor.a);

			// Refraction. Stop searching for more hits afterwards.
			if (refractionFactor > EPSILON) {
				storeHit = true;
				gRefraction[launchIndex].a = resColor.a;
				resColor.a = 0.0f;
			}

			// Store hit if it's been flagged as the primary one.
			if (storeHit && (resInstanceId < 0)) {
				float3 vertexFlow = gHitDistAndFlow[hitBufferIndex].yzw;
				float2 prevPos = WorldToScreenPos(prevViewProj, vertexPosition - vertexFlow);
				float2 curPos = WorldToScreenPos(viewProj, vertexPosition);
				float4 projPos = mul(viewProj, float4(vertexPosition, 1.0f));
				resPosition = vertexPosition;
				resNormal.xyz = normal;
				resSpecular = specular;
                resEmissive = emissive;
                resRoughness = roughness;
                resMetalness = metalness;
                resAmbient = vertexAmbientOcclusion;
				resInstanceId = instanceId;
				resFlow = (curPos - prevPos) * resolution.xy;
				resDepth = projPos.z / projPos.w;
            }
		}

		if (resColor.a <= EPSILON) {
			break;
		}
	}

	// Blend with the background.
    resColor.rgb += bgColor * resColor.a; 
    resColor.a = 1.0f - resColor.a;
	
	// Accumulate
    historyLength = min(historyLength + 1.0f, 64.0f);
    resNormal.a = historyLength;

	// Store shading information buffers.
	gShadingPosition[launchIndex] = float4(resPosition, 0.0f);
    gShadingNormal[launchIndex] = resNormal;
	gShadingSpecular[launchIndex] = float4(resSpecular, 0.0f);
	gShadingEmissive[launchIndex] = float4(resEmissive, 0.0f);
    gShadingRoughness[launchIndex] = resRoughness;
	gShadingMetalness[launchIndex] = resMetalness;
    gShadingAmbient[launchIndex] = resAmbient;
    gDiffuse[launchIndex] = SrgbToLinear(resColor);
	gInstanceId[launchIndex] = resInstanceId;
	gTransparent[launchIndex] = float4(resTransparent, 1.0f);
	gFlow[launchIndex] = float2(-resFlow.x, resFlow.y);
    gNormal[launchIndex] = resNormal;
	gDepth[launchIndex] = resDepth;
}

[shader("miss")]
void SurfaceMiss(inout HitInfo payload : SV_RayPayload) {
	// No-op.
}

[shader("miss")]
void ShadowMiss(inout ShadowHitInfo payload : SV_RayPayload) {
	// No-op.
}