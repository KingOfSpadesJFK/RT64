//
// RT64
//

#include "Constants.hlsli"
#include "Color.hlsli"
#include "GlobalParams.hlsli"

Texture2D<float4> gFlow : register(t0);
Texture2D<float4> gDiffuse : register(t1);
Texture2D<float4> gDirectLight : register(t2);
Texture2D<float4> gIndirectLight : register(t3);
Texture2D<float4> gReflection : register(t4);
Texture2D<float4> gRefraction : register(t5);
Texture2D<float4> gTransparent : register(t6);
Texture2D<float4> gDiffuseBG : register(t7);

SamplerState gSampler : register(s0);

float4 PSMain(in float4 pos : SV_Position, in float2 uv : TEXCOORD0) : SV_TARGET {
    float4 diffuse = gDiffuse.SampleLevel(gSampler, uv, 0);
    float4 diffuseBG = gDiffuseBG.SampleLevel(gSampler, uv, 0);
    if (diffuse.a > EPSILON) {
        float3 directLight = gDirectLight.SampleLevel(gSampler, uv, 0).rgb;
        float3 indirectLight = gIndirectLight.SampleLevel(gSampler, uv, 0).rgb;
        float3 reflection = gReflection.SampleLevel(gSampler, uv, 0).rgb;
        float3 refraction = gRefraction.SampleLevel(gSampler, uv, 0).rgb;
        float3 transparent = gTransparent.SampleLevel(gSampler, uv, 0).rgb;
        float3 result = lerp(LinearToSrgb(diffuse.rgb), LinearToSrgb(diffuse.rgb * (directLight + indirectLight)), diffuse.a);
        result += reflection;
        result += refraction;
        result += transparent;
        return float4(result + LinearToSrgb(diffuseBG.rgb), 1.0f);
    }
    else {
        return LinearToSrgb(float4(diffuse.rgb + diffuseBG.rgb, 1.0f));
    }
}