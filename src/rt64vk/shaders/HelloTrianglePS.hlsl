struct PSInput {
    float4 position : SV_POSITION;
    float3 inColor : COLOR0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}
