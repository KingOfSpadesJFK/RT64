cbuffer ubo : register(b0) { 
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};

void VSMain(
    float4 inPosition : POSITION, 
    float3 inColor : COLOR0,

    out float3 outColor : COLOR0,
    out float4 outPosition : SV_POSITION
    )
{
    outPosition = mul(proj, mul(view, mul(model, float4(inPosition.xyz, 1.0))));
    // outPosition = float4(inPosition.xyz, 1.0);
    outColor = inColor;
}