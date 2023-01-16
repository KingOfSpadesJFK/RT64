
#include "GlobalParams.hlsli"

void VSMain(
    float4 inPosition : POSITION, 
    float3 inColor : COLOR0,

    out float3 outColor : COLOR0,
    out float4 outPosition : SV_POSITION
    )
{
    outPosition = mul(projection, mul(view, float4(inPosition.xyz, 1.0)));
    // outPosition = float4(inPosition.xyz, 1.0);
    outColor = inColor;
}