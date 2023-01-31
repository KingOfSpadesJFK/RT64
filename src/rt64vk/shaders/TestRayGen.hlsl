
#include "Constants.hlsli"
#include "GlobalParams.hlsli"

#define RAY_MIN_DISTANCE					0.1f
#define RAY_MAX_DISTANCE					100000.0f

struct Payload {
    float4 color;
};

struct Attributes {
	float2 bary;
};

RaytracingAccelerationStructure SceneBVH : register(t0);
RWTexture2D<float4> gDiffuse : register(u4);

[shader("raygeneration")]
void raygen() {
	uint2 launchIndex = DispatchRaysIndex().xy;
	uint2 launchDims = DispatchRaysDimensions().xy;
	float2 d = (((launchIndex.xy + 0.5f + pixelJitter) / float2(launchDims)) * 2.f - 1.f);
	float4 target = mul(projectionI, float4(d.x, -d.y, 1, 1));
	float3 rayOrigin = mul(viewI, float4(0, 0, 0, 1)).xyz;
	float3 rayDirection = mul(viewI, float4(target.xyz, 0)).xyz;

	RayDesc ray;
	ray.Origin = rayOrigin;
	ray.Direction = rayDirection;
	ray.TMin = RAY_MIN_DISTANCE;
	ray.TMax = RAY_MAX_DISTANCE;
	Payload payload;
	payload.color = float4(0.2,0.4,0.60,1.0);
	TraceRay(SceneBVH, RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, 0, 1, 0, ray, payload);

	gDiffuse[launchIndex] = payload.color;
}

[shader("anyhit")]
void anyhit(inout Payload payload : SV_RayPayload, in Attributes attrib) {
    payload.color = float4(0.0f,1.0f,0.0f,1.0f);
}

[shader("miss")]
void miss(inout Payload payload : SV_RayPayload, in Attributes attrib) {
    payload.color = float4(1.0f,0.0f,0.0f,1.0f);
}