/*
*	RT64VK
*/

#ifdef SHADER_AS_STRING
R"raw(
#else
struct InstanceTransforms {
	float4x4 objectToWorld;
	float4x4 objectToWorldNormal;
	float4x4 objectToWorldPrevious;
};

struct PushConstant {
	int instanceId;
	InstanceTransforms iTransforms;
	MaterialProperties iMaterial;
}

[[vk::push_constant]] PushConstant pc;

Texture2D<float4> gDiffuseTexture0 : register(t0);
Texture2D<float4> gDiffuseTexture1 : register(t1);
Texture2D<float4> gNormalTexture   : register(t2);
Texture2D<float4> gSpecularTexture : register(t3);

float WithDistanceBias(float distance, MaterialProperties material) {
	return distance - material.depthBias;
}

float WithoutDistanceBias(float distance, MaterialProperties material) {
	return distance + material.depthBias;
}
//)raw"
#endif