//
// RT64
//

#include "Constants.hlsli"
#include "GlobalParams.hlsli"
#include "Color.hlsli"
#include "BicubicFiltering.hlsli"

// Tonemappers sourced from https://64.github.io/tonemapping/
#define TONEMAP_MODE_RAW_IMAGE 0
#define TONEMAP_MODE_REINHARD 1
#define TONEMAP_MODE_REINHARD_LUMA 2
#define TONEMAP_MODE_REINHARD_JODIE 3
#define TONEMAP_MODE_UNCHARTED_2 4
#define TONEMAP_MODE_ACES 5
#define TONEMAP_MODE_SIMPLE 6
#define TONEMAP_MODE_BLOOM_ONLY 7

// Reccommended settings for tonemapping
// Tonemapper: Unchared 2
// Exposure: 1.0
// White point: 1.0
// Eye Adaption Minimum Luminence: -17.0
// Eye Adaption Luminance Range: 11.0
// Eye Adaption Update Time: 50.0
// Eye Adaption Brightening Factor: 10.0

Texture2D<float4> gOutput : register(t0);
Texture2D<float4> gFlow : register(t1);
Texture2D<float> gLumaAvg : register(t2);
Texture2D<float4> gBloom : register(t3);

SamplerState gSampler : register(s0);

float3 WhiteBlackPoint(float3 bl, float3 wp, float3 color)
{
    return (color - bl) / (wp - bl);
}

float3 Reinhard(float3 color, float xp)
{
    float3 num = color * (1.0f + (color / (xp * xp)));
    return num / (1.0f + color);
}

float3 ChangeLuma(float3 color, float lumaOut)
{
    float lumaIn = RGBtoLuminance(color);
    return color * (lumaOut / lumaIn);
}

float3 ReinhardLuma(float3 color, float xp)
{
    float lumaOld = RGBtoLuminance(color);
    float num = lumaOld * (1.0f + (lumaOld / (xp * xp)));
    float lumaNew = num / (1.0f + lumaOld);
    return ChangeLuma(color, lumaNew);
}

// Taken from https://www.shadertoy.com/view/4dBcD1
float3 ReinhardJodie(float3 rgb)
{
    float luma = RGBtoLuminance(rgb);
    float3 tc = rgb / (rgb + 1.0f);
    return lerp(rgb / (luma + 1.0f), tc, tc);
}

float3 Uncharted2(float3 rgb, float exp)
{
    float A = 0.15f;
    float B = 0.50f;
    float C = 0.10f;
    float D = 0.20f;
    float E = 0.02f;
    float F = 0.30f;
    float3 color = ((rgb * exp * (A * rgb * exp + C * B) + D * E) / (rgb * exp * (A * rgb * exp + B) + D * F)) - E / F;
    float3 w = float3(11.2f, 11.2f, 11.2f);
    float3 whiteScale = float3(1.0f, 1.0f, 1.0f) / (((11.2f * (A * 11.2f + C * B) + D * E) / (11.2f * (A * 11.2f + B) + D * F)) - E / F);
    
    return color * whiteScale;
}

// ACES in HLSL from https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
float3 RRTAndODTFit(float3 v)
{
    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}

float3 ACESFitted(float3 color)
{
    color = mul(ACESInputMat, color);

    // Apply RRT and ODT
    color = RRTAndODTFit(color);
    color = mul(ACESOutputMat, color);
    
    return color;
}

float3 Tonemapper(float3 rgb, float exposure)
{
    switch (tonemapMode)
    {
        case TONEMAP_MODE_REINHARD:
            return Reinhard(rgb * exposure, exposure);
        case TONEMAP_MODE_REINHARD_LUMA:
            return ReinhardLuma(rgb * exposure, exposure);
        case TONEMAP_MODE_REINHARD_JODIE:
            return ReinhardJodie(rgb * exposure);
        case TONEMAP_MODE_UNCHARTED_2:
            return Uncharted2(rgb * 2.0f, exposure);
        case TONEMAP_MODE_ACES:
            return ACESFitted(rgb * exposure);
        case TONEMAP_MODE_SIMPLE:
            return rgb *= exposure;
    }
    
    return rgb;
}

float4 MotionBlur(float2 uv)
{
    float2 flow = gFlow.SampleLevel(gSampler, uv, 0).xy / resolution.xy;
    float flowLength = length(flow);
    if (flowLength > 1e-6f)
    {
        const float SampleStep = motionBlurStrength / motionBlurSamples;
        float4 sumColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
        float sumWeight = 0.0f;
        float2 startUV = uv - (flow * motionBlurStrength / 2.0f);
        for (uint s = 0; s < motionBlurSamples; s++)
        {
            float2 sampleUV = max(startUV + flow * s * SampleStep, float2(0.0f, 0.0f));
            float sampleWeight = 1.0f;
            sumColor += gOutput.SampleLevel(gSampler, sampleUV, 0) * sampleWeight;
            sumWeight += sampleWeight;
        }

        return sumColor / sumWeight;
    }
    return gOutput.SampleLevel(gSampler, uv, 0);
}

float4 Bloom(float bloomExposure, float bloomThreshold, float bloomAlpha, float2 uv)
{
    uint3 bloomResolution = uint3(0, 0, 0);
    gBloom.GetDimensions(0, bloomResolution.x, bloomResolution.y, bloomResolution.z);
    float4 bloom = BicubicFilter(gBloom, gSampler, uv, bloomResolution.xy);
    bloom.rgb *= bloomExposure;
    bloom.rgb = max(bloom.rgb - bloomThreshold, 0.0);
    bloom.a = RGBtoLuminance(bloom.rgb) * bloomAlpha;
    return bloom;
}


float4 PSMain(in float4 pos : SV_Position, in float2 uv : TEXCOORD0) : SV_TARGET {
    
    float4 color = float4(0.0f, 0.0f, 0.0f, 1.0f);
    if ((motionBlurStrength > 0.0f) && (motionBlurSamples > 0)) {
        color = MotionBlur(uv);
    }
    else {
        color = gOutput.SampleLevel(gSampler, uv, 0);
    }
    
    // Tonemap the image
    float avgLuma = gLumaAvg.Sample(gSampler, float2(0.0, 0.0));
    if (!(processingFlags & RT64_VIEW_EYE_ADAPTION_FLAG)) {
        avgLuma = 1.0f;
    }
    color.rgb = Tonemapper(max(color.rgb, 0.0f), tonemapExposure / avgLuma);
    
    if (tonemapMode == TONEMAP_MODE_RAW_IMAGE) {
        return color;
    }
    
    // Add bloom to the image
    float4 bloom = float4(0, 0, 0, 0);
    if (processingFlags & 0x10) {
        bloom = Bloom(tonemapExposure * bloomExposure / avgLuma, tonemapWhite * bloomThreshold, bloomAmount, uv);
    }
    if (tonemapMode == TONEMAP_MODE_BLOOM_ONLY) {
        return bloom;
    }
    
    // Post-tonemapping
    // TODO: Saturation is weird. Might reimplement it when I find something better
    color.rgb = WhiteBlackPoint(tonemapBlack, tonemapWhite, color.rgb);
    color.rgb = pow(color.rgb, tonemapGamma);
    
    return color;
}