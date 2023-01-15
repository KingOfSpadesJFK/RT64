


float4 PSMain(
    float4 position : SV_POSITION,
    float3 inColor : COLOR0
    ) : SV_TARGET
{
    return float4(inColor, 1.0f);
}
