//
// RT64
//

#include "Color.hlsli"
#include "Constants.hlsli"
#include "GlobalParams.hlsli"
#include "BicubicFiltering.hlsli"

Texture2D<float4> gFlow : register(t0);
Texture2D<float4> gDiffuse : register(t1);
Texture2D<float4> gDirectLight : register(t2);
Texture2D<float4> gSpecularLight : register(t3);
Texture2D<float4> gIndirectLight : register(t4);
Texture2D<float4> gVolumetrics : register(t5);
Texture2D<float4> gReflection : register(t6);
Texture2D<float4> gRefraction : register(t7);
Texture2D<float4> gTransparent : register(t8);
Texture2D<float4> gFog : register(t9);
Texture2D<float> gAmbientOcclusion : register(t10);

SamplerState gSampler : register(s0);

float4 PSMain(in float4 pos : SV_Position, in float2 uv : TEXCOORD0) : SV_TARGET {
    float4 diffuse = gDiffuse.SampleLevel(gSampler, uv, 0);
    if (diffuse.a > EPSILON) {
        float3 directLight = gDirectLight.SampleLevel(gSampler, uv, 0).rgb;
        float3 specularLight = gSpecularLight.SampleLevel(gSampler, uv, 0).rgb;
        float3 indirectLight = gIndirectLight.SampleLevel(gSampler, uv, 0).rgb;
        float ambient = saturate(gAmbientOcclusion.SampleLevel(gSampler, uv, 0) + RGBtoLuminance(directLight + specularLight + indirectLight));
        indirectLight *= ambient;
        float3 reflection = gReflection.SampleLevel(gSampler, uv, 0).rgb;
        float3 refraction = gRefraction.SampleLevel(gSampler, uv, 0).rgb;
        float3 transparent = gTransparent.SampleLevel(gSampler, uv, 0).rgb;
        float4 fog = gFog.SampleLevel(gSampler, uv, 0);
        float3 result = lerp(LinearToSrgb(diffuse.rgb), LinearToSrgb(diffuse.rgb * (directLight + indirectLight) + specularLight), diffuse.a);
        result.rgb += reflection;
        result.rgb += refraction;
        result.rgb += transparent;
        // TODO: Change how the volumetrics and fog blend together
        if (processingFlags & RT64_VIEW_VOLUMETRICS_FLAG) {
            float4 volumetrics = BicubicFilter(gVolumetrics, gSampler, uv * volumetricResolution, resolution.xy);
            fog = BlendAOverB(volumetrics, fog);
        }
        result = BlendAOverB(fog, float4(result, 1.0f)).rgb;
        return float4(result, 1.0f);
    }
    else
    {
        float4 fog = gFog.SampleLevel(gSampler, uv, 0);
        if (processingFlags & RT64_VIEW_VOLUMETRICS_FLAG) {
            float4 volumetrics = BicubicFilter(gVolumetrics, gSampler, uv * volumetricResolution, resolution.xy);
            fog = BlendAOverB(volumetrics, fog);
        }
        float3 result = LinearToSrgb(diffuse.rgb);
        result = BlendAOverB(fog, float4(result, 1.0f)).rgb;
        return float4(result, 1.0f);

    }
}