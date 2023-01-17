
#include "GlobalParams.hlsli"

void VSMain(
        [[vk::location(0)]] float4 inPosition       : POSITION, 
        [[vk::location(1)]] float3 inNormal         : NORMAL,
        [[vk::location(2)]] float2 inUV             : TEXCOORD,

                            out float4 gl_position  : SV_POSITION,
        [[vk::location(0)]] out float3 outColor     : COLOR0,
        [[vk::location(1)]] out float2 outUV        : TEXCOORD
    )
{
    gl_position = mul(projection, mul(view, float4(inPosition.xyz, 1.0)));
    // outPosition = float4(inPosition.xyz, 1.0);
    outColor = inNormal;
    outUV = inUV;
}