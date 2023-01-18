

Texture2D<float4> texture : register(t1);
SamplerState samplerColor : register(s1);


float4 PSMain(
                            float4 gl_position   : SV_POSITION,
        [[vk::location(0)]] float3 inNormal     : COLOR0,
        [[vk::location(1)]] float2 inUV         : TEXCOORD
    ) : SV_TARGET
{
    
    return texture.SampleLevel(samplerColor, inUV, 1) + float4(inNormal, 1.0f);
}
