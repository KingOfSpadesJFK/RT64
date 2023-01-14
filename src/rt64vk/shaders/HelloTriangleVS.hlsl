struct VSInput {
    [[vk::location(0)]] float2 inPosition : POSITION;
    [[vk::location(1)]] float3 inColor : COLOR0;
};

struct PSInput {
    [[vk::location(0)]] float3 inColor : COLOR0;
    float4 position : SV_POSITION;
};

PSInput main(VSInput input) {
    PSInput output;
    output.position = float4(input.inPosition, 0.0, 1.0);
    output.inColor = input.inColor;
    return output;
}