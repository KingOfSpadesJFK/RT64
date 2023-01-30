
#include "Constants.hlsli"
#include "GlobalParams.hlsli"

#define RAY_MIN_DISTANCE					0.1f
#define RAY_MAX_DISTANCE					100000.0f
struct RayDiff {
    float3 dOdx;
    float3 dOdy;
    float3 dDdx;
    float3 dDdy;
};

struct HitInfo {
	uint nhits;
    RayDiff rayDiff;
};

struct ShadowHitInfo {
	float shadowHit;
    RayDiff rayDiff;
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
	float3 nonNormRayDir = d.x * cameraU.xyz + d.y * cameraV.xyz + cameraW.xyz;
	float4 target = mul(projectionI, float4(d.x, -d.y, 1, 1));
	float3 rayOrigin = mul(viewI, float4(0, 0, 0, 1)).xyz;
	float3 rayDirection = mul(viewI, float4(target.xyz, 0)).xyz;
	printf("We gon be alright %f", rayOrigin.x);

    RayDiff rayDiff;
	rayDiff.dOdx = float3(0.0f, 0.0f, 1.0f);
	rayDiff.dOdy = float3(0.0f, 0.0f, 0.0f);
    rayDiff.dDdx = float3(0.0f, 0.0f, 0.0f);
    rayDiff.dDdy = float3(0.0f, 0.0f, 0.0f);

	RayDesc ray;
	ray.Origin = rayOrigin;
	ray.Direction = rayDirection;
	ray.TMin = RAY_MIN_DISTANCE;
	ray.TMax = RAY_MAX_DISTANCE;
	ray.Origin = float3(0.0f, 0.0f, 0.0f);
	ray.Direction = float3(0.0f, 0.0f, -1.0f);
	ray.TMin = RAY_MIN_DISTANCE;
	ray.TMax = RAY_MAX_DISTANCE;
	HitInfo payload;
	payload.nhits = 0;
	payload.rayDiff = rayDiff;
	TraceRay(SceneBVH, RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, 0, 0, 0, ray, payload);
	gDiffuse[launchIndex] = float4(payload.rayDiff.dOdx, 1.0f);
}

[shader("anyhit")]
void anyhit(inout HitInfo payload, Attributes attrib) {
    payload.rayDiff.dOdx = float3(0.0f,1.0f,0.0f);
}

[shader("miss")]
void miss(inout HitInfo payload, Attributes attrib) {
    payload.rayDiff.dOdx = float3(1.0f,0.0f,0.0f);
}