//
// RT64
//

#ifdef SHADER_AS_STRING
R"raw(
#else
cbuffer gParams : register(b0) {
	float4x4 view;
	float4x4 viewI;
	float4x4 prevViewI;
	float4x4 projection;
	float4x4 projectionI;
	float4x4 viewProj;
	float4x4 prevViewProj;
	float4 cameraU;
	float4 cameraV;
	float4 cameraW;
	float4 viewport;
	float4 resolution;
	float4 ambientBaseColor;
	float4 ambientNoGIColor;
	float4 eyeLightDiffuseColor;
	float4 eyeLightSpecularColor;
	float4 skyDiffuseMultiplier;
	float4 skyHSLModifier;
	float2 pixelJitter;
	float skyYawOffset;
	float giDiffuseStrength;
	float giSkyStrength;
	float motionBlurStrength;
	float tonemapExposure;
	float tonemapBlack;
	float tonemapWhite;
	float tonemapGamma;
	uint tonemapMode;
	int skyPlaneTexIndex;
	uint randomSeed;
	uint diSamples;
	uint giSamples;
	uint giBounces;
	uint diReproject;
	uint giReproject;
	uint binaryLockMask;
	uint maxLights;
	uint motionBlurSamples;
	uint visualizationMode;
	uint frameCount;
}

#define VISUALIZATION_MODE_FINAL					0
#define VISUALIZATION_MODE_SHADING_POSITION			1
#define VISUALIZATION_MODE_SHADING_NORMAL			2
#define VISUALIZATION_MODE_SHADING_SPECULAR			3
#define VISUALIZATION_MODE_DIFFUSE					4
#define VISUALIZATION_MODE_BACKGROUND				5
#define VISUALIZATION_MODE_INSTANCE_ID				6
#define VISUALIZATION_MODE_DIRECT_LIGHT_RAW			7
#define VISUALIZATION_MODE_DIRECT_LIGHT_FILTERED	8
#define VISUALIZATION_MODE_INDIRECT_LIGHT_RAW		9
#define VISUALIZATION_MODE_INDIRECT_LIGHT_FILTERED	10
#define VISUALIZATION_MODE_REFLECTION				11
#define VISUALIZATION_MODE_REFRACTION				12
#define VISUALIZATION_MODE_TRANSPARENT				13
#define VISUALIZATION_MODE_FLOW						14
#define VISUALIZATION_MODE_REACTIVE_MASK			15
#define VISUALIZATION_MODE_LOCK_MASK				16
#define VISUALIZATION_MODE_DEPTH					17
//)raw"
#endif