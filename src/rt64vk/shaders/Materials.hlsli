//
// RT64
//

#ifdef SHADER_AS_STRING
R"raw(
#else
struct MaterialProperties {
	float4 diffuseColorMix;
	float3 specularColor;
	float3 selfLight;
	float3 fogColor;
	int diffuseTexIndex;
	int normalTexIndex;
	int specularTexIndex;
	float ignoreNormalFactor;
	float uvDetailScale;
	float reflectionFactor;
	float reflectionFresnelFactor;
	float reflectionShineFactor;
	float refractionFactor;
	float specularExponent;
	float solidAlphaMultiplier;
	float shadowAlphaMultiplier;
	float depthBias;
	float shadowRayBias;
	uint lightGroupMaskBits;
	float fogMul;
	float fogOffset;
	uint fogEnabled;
	float lockMask;
	uint _reserved;
};
//)raw"
#endif