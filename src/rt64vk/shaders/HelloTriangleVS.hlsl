
#include "GlobalParams.hlsli"

void VSMain(
    [[vk::location(0)]] float4 inPosition : POSITION, 
    [[vk::location(1)]] float3 inColor : COLOR0,

    [[vk::location(0)]] out float3 outColor : COLOR0,
    out float4 outPosition : SV_POSITION
    )
{
    outPosition = inPosition;
    outColor = inColor;
}