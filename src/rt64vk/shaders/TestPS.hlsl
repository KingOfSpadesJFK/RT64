struct MaterialProperties {
        int diffuseTexIndex;
        int normalTexIndex;
        int specularTexIndex;
        float ignoreNormalFactor;
        float uvDetailScale;
        float reflectionFactor;
        float reflectionFresnelFactor;
        float reflectionShineFactor;
        float refractionFactor;
        float3 specularColor;
        float specularExponent;
        float solidAlphaMultiplier;
        float shadowAlphaMultiplier;
        float depthBias;
        float shadowRayBias;
        float3 selfLight;
        uint lightGroupMaskBits;
        float3 fogColor;
        float4 diffuseColorMix;
        float fogMul;
        float fogOffset;
        uint fogEnabled;
        uint _reserved;
};
//

struct InstanceTransforms {
        float4x4 objectToWorld;
        float4x4 objectToWorldNormal;
        float4x4 objectToWorldPrevious;
};

StructuredBuffer<InstanceTransforms> instanceTransforms : register(t5);
StructuredBuffer<MaterialProperties> instanceMaterials : register(t6);

float WithDistanceBias(float distance, uint instanceId) {
        return distance - instanceMaterials[instanceId].depthBias;
}

float WithoutDistanceBias(float distance, uint instanceId) {
        return distance + instanceMaterials[instanceId].depthBias;
}
//
int instanceId : register(b0);
SamplerState gTextureSampler : register(s10);

Texture2D<float4> gTextures[512] : register(t8);
//
void PSMain(
    in float4 vertexPosition : SV_POSITION,
    in float3 vertexNormal : NORMAL,
    in float2 vertexUV : TEXCOORD,
    in float4 input1 : COLOR0,
    out float4 resultColor : SV_TARGET
) {
    int diffuseTexIndex = instanceMaterials[instanceId].diffuseTexIndex;
    float4 texVal0 = gTextures[NonUniformResourceIndex(diffuseTexIndex)].Sample(gTextureSampler, vertexUV);
    resultColor = float4((float4(texVal0.rgb, 1.0f)).rgb, input1.a);
}