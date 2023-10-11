/*
*   RT64VK
*/

#include "Constants.hlsli"
#include "Color.hlsli"
#include "GlobalParams.hlsli"

// Tonemappers sourced from https://64.github.io/tonemapping/
#define TONEMAP_MODE_RAW_IMAGE 0
#define TONEMAP_MODE_SIMPLE 1
#define TONEMAP_MODE_REINHARD 2
#define TONEMAP_MODE_REINHARD_LUMA 3
#define TONEMAP_MODE_REINHARD_JODIE 4
#define TONEMAP_MODE_UNCHARTED_2 5
#define TONEMAP_MODE_ACES 6

// Recommended settings for tonemapping
//  I say recommended, but its just my personal tastes
// Tonemapper: Unchared 2
// Exposure: 1.0
// White point: 1.0
// Eye Adaption Minimum Luminence: -17.0
// Eye Adaption Luminance Range: 11.0
// Eye Adaption Update Time: 50.0
// Eye Adaption Brightening Factor: 10.0

// Got this from https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
// About that beer I owed ya...
// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
static const float3x3 ACESInputMat =
{
    { 0.59719, 0.35458, 0.04823 },
    { 0.07600, 0.90834, 0.01566 },
    { 0.02840, 0.13383, 0.83777 }
};

// ODT_SAT => XYZ => D60_2_D65 => sRGB
static const float3x3 ACESOutputMat =
{
    { 1.60475, -0.53108, -0.07367 },
    { -0.10208, 1.10813, -0.00605 },
    { -0.00327, -0.07276, 1.07602 }
};

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
    float lumaIn = RGBtoLuminance(color) + EPSILON;
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
    float3 whiteScale = float3(1.0f, 1.0f, 1.0f) / (((w * (A * w + C * B) + D * E) / (w * (A * w + B) + D * F)) - E / F);
    
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
    color = max(mul(ACESInputMat, color), 0.0f);

    // Apply RRT and ODT
    color = RRTAndODTFit(color);
    color = max(mul(ACESOutputMat, color), 0.0f);
    
    return color;
}

Texture2D<float4> gOutput : register(t0);
SamplerState gSampler : register(s0);

float4 PSMain(in float4 pos : SV_Position, in float2 uv : TEXCOORD0) : SV_TARGET {
    float3 color = max(gOutput.SampleLevel(gSampler, uv, 0), 0.0f).rgb;
    
    switch (abs(tonemapMode))
    {
        case TONEMAP_MODE_SIMPLE:
            color *= tonemapExposure;
            break;
        case TONEMAP_MODE_REINHARD:
            color = Reinhard(color, tonemapExposure) * tonemapExposure;
            break;
        case TONEMAP_MODE_REINHARD_LUMA:
            color = ReinhardLuma(color, tonemapExposure) * tonemapExposure;
            break;
        case TONEMAP_MODE_REINHARD_JODIE:
            color = ReinhardJodie(color) * tonemapExposure;
            break;
        case TONEMAP_MODE_UNCHARTED_2:
            color = Uncharted2(color * 2.0f, tonemapExposure);
            break;
        case TONEMAP_MODE_ACES:
            color = ACESFitted(color * tonemapExposure);
            break;
    }

    if (tonemapMode > 0) {
        // TODO: Saturation is weird. Might reimplement it when I find something better
        color = WhiteBlackPoint(tonemapBlack, tonemapWhite, color);
        color = pow(color, tonemapGamma);
    }
    
    // Show clipping
    if (tonemapMode < 0) {
        if (color.r > 1.0f || color.g > 1.0f || color.b > 1.0f) {
            color = float3(0.0f, 0.0f, 0.0f);
        }
    }
    return float4(color, 1.0f);
}