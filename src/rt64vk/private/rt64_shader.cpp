//
// RT64
//
#ifndef RT64_MINIMAL

#include "rt64_shader.h"

#include "rt64_device.h"
#include "rt64_shader_hlsli.h"

#include <locale>
#include <codecvt>
#include <string>

// Private

#define TEXTURE_EDGE_ENABLED

enum {
	SHADER_0,
	SHADER_INPUT_1,
	SHADER_INPUT_2,
	SHADER_INPUT_3,
	SHADER_INPUT_4,
	SHADER_TEXEL0,
	SHADER_TEXEL0A,
	SHADER_TEXEL1
};

#define SHADER_OPT_ALPHA (1 << 24)
#define SHADER_OPT_TEXTURE_EDGE (1 << 26)
#define SHADER_OPT_NOISE (1 << 27)

#define SS(x) ss << (x) << std::endl;

struct ColorCombinerParams {
	int c[2][4];
	int inputCount = 0;
	bool useTextures[2] = { false, false };
	int do_single[2];
	int do_multiply[2];
	int do_mix[2];
	int color_alpha_same;
	int opt_alpha;
	int opt_texture_edge;
	int opt_noise;

	ColorCombinerParams(int shaderId) {
		for (int i = 0; i < 4; i++) {
			c[0][i] = (shaderId >> (i * 3)) & 7;
			c[1][i] = (shaderId >> (12 + i * 3)) & 7;
		}

		for (int i = 0; i < 2; i++) {
			for (int j = 0; j < 4; j++) {
				if (c[i][j] >= SHADER_INPUT_1 && c[i][j] <= SHADER_INPUT_4) {
					if (c[i][j] > inputCount) {
						inputCount = c[i][j];
					}
				}
				if (c[i][j] == SHADER_TEXEL0 || c[i][j] == SHADER_TEXEL0A) {
					useTextures[0] = true;
				}
				if (c[i][j] == SHADER_TEXEL1) {
					useTextures[1] = true;
				}
			}
		}
		
		do_single[0] = c[0][2] == 0;
		do_single[1] = c[1][2] == 0;
		do_multiply[0] = c[0][1] == 0 && c[0][3] == 0;
		do_multiply[1] = c[1][1] == 0 && c[1][3] == 0;
		do_mix[0] = c[0][1] == c[0][3];
		do_mix[1] = c[1][1] == c[1][3];
		color_alpha_same = (shaderId & 0xfff) == ((shaderId >> 12) & 0xfff);
		opt_alpha = (shaderId & SHADER_OPT_ALPHA) != 0;
		opt_texture_edge = (shaderId & SHADER_OPT_TEXTURE_EDGE) != 0;
		opt_noise = (shaderId & SHADER_OPT_NOISE) != 0;
	}
};

struct VertexLayout {
	unsigned int vertexSize = 0;
	unsigned int positionOffset = 0;
	unsigned int normalOffset = 0;
	unsigned int uvOffset = 0;
	unsigned int inputOffset[4] = { 0,0,0,0 };
	VertexLayout(bool vertexPosition, bool vertexNormal, bool vertexUV, unsigned int inputCount, bool useAlpha) {
		positionOffset = vertexSize; if (vertexPosition) vertexSize += sizeof(glm::vec4);
		normalOffset = vertexSize; if (vertexNormal) vertexSize += sizeof(glm::vec3);
		uvOffset = vertexSize; if (vertexUV) vertexSize += sizeof(glm::vec2);
		for (unsigned int i = 0; i < inputCount; i++) {
			inputOffset[i] = vertexSize;
			vertexSize += useAlpha ? 16 : 12;
		}
	}
};

void incMeshBuffers(std::stringstream &ss) {
	SS("ByteAddressBuffer vertexBuffer : register(t2);");
	SS("ByteAddressBuffer indexBuffer : register(t3);");
}

void getVertexData(std::stringstream &ss, bool vertexPosition, bool vertexNormal, bool vertexUV, int inputCount, bool useAlpha, bool vertexBinormalAndTangent) {
	VertexLayout vl(vertexPosition, vertexNormal, vertexUV, inputCount, useAlpha);

	SS("uint3 index3 = indexBuffer.Load3((triangleIndex * 3) * 4);");

	if (vertexPosition) {
		for (int i = 0; i < 3; i++) {
			SS("float3 pos" + std::to_string(i) + " = asfloat(vertexBuffer.Load3(index3[" + std::to_string(i) + "] * " + std::to_string(vl.vertexSize) + " + " + std::to_string(vl.positionOffset) + "));");
			SS("float3 posW" + std::to_string(i) + " = mul(instanceTransforms[instanceId].objectToWorld, float4(pos" + std::to_string(i) + ", 1.0f)).xyz; ");
		}

		SS("float3 vertexPosition = pos0 * barycentrics[0] + pos1 * barycentrics[1] + pos2 * barycentrics[2];");
	}

	if (vertexNormal) {
		for (int i = 0; i < 3; i++) {
			SS("float3 norm" + std::to_string(i) + " = asfloat(vertexBuffer.Load3(index3[" + std::to_string(i) + "] * " + std::to_string(vl.vertexSize) + " + " + std::to_string(vl.normalOffset) + "));");
		}

		SS("float3 vertexNormal = norm0 * barycentrics[0] + norm1 * barycentrics[1] + norm2 * barycentrics[2];");
		SS("float3 triangleNormal = -cross(pos2 - pos0, pos1 - pos0);");
		SS("vertexNormal = any(vertexNormal) ? normalize(vertexNormal) : triangleNormal;");

		// Transform the triangle normal.
		SS("triangleNormal = normalize(mul(instanceTransforms[instanceId].objectToWorldNormal, float4(triangleNormal, 0.f)).xyz);");
	}

	if (vertexUV) {
		for (int i = 0; i < 3; i++) {
			SS("float2 uv" + std::to_string(i) + " = asfloat(vertexBuffer.Load2(index3[" + std::to_string(i) + "] * " + std::to_string(vl.vertexSize) + " + " + std::to_string(vl.uvOffset) + "));");
		}

		SS("float2 vertexUV = uv0 * barycentrics[0] + uv1 * barycentrics[1] + uv2 * barycentrics[2];");
	}

	for (int i = 0; i < inputCount; i++) {
		std::string floatNum = useAlpha ? "4" : "3";
		std::string index = std::to_string(i + 1);
		for (int j = 0; j < 3; j++) {
			SS("float" + floatNum + " input" + index + std::to_string(j) + " = asfloat(vertexBuffer.Load" + floatNum + "(index3[" + std::to_string(j) + "] * " + std::to_string(vl.vertexSize) + " + " + std::to_string(vl.inputOffset[i]) + "));");
		}

		SS("float4 input" + index + " = " + (useAlpha ? "" : "float4(") + "input" + index + "0 * barycentrics[0] + input" + index + "1 * barycentrics[1] + input" + index + "2 * barycentrics[2]" + (useAlpha ? "" : ", 1.0f)") + ";");
	}

	if (vertexBinormalAndTangent) {
		// Compute the tangent vector for the polygon.
		// Derived from http://area.autodesk.com/blogs/the-3ds-max-blog/how_the_3ds_max_scanline_renderer_computes_tangent_and_binormal_vectors_for_normal_mapping
		SS("float uva = uv1.x - uv0.x;");
		SS("float uvb = uv2.x - uv0.x;");
		SS("float uvc = uv1.y - uv0.y;");
		SS("float uvd = uv2.y - uv0.y;");
		SS("float uvk = uvb * uvc - uva * uvd;");
		SS("float3 dpos1 = pos1 - pos0;");
		SS("float3 dpos2 = pos2 - pos0;");
		SS("float3 vertexTangent;");
		SS("if (uvk != 0) vertexTangent = normalize((uvc * dpos2 - uvd * dpos1) / uvk);");
		SS("else {");
		SS("    if (uva != 0) vertexTangent = normalize(dpos1 / uva);");
		SS("    else if (uvb != 0) vertexTangent = normalize(dpos2 / uvb);");
		SS("    else vertexTangent = 0.0f;");
		SS("}");
		SS("float2 duv1 = uv1 - uv0;");
		SS("float2 duv2 = uv2 - uv1;");
		SS("duv1.y = -duv1.y;");
		SS("duv2.y = -duv2.y;");
		SS("float3 cr = cross(float3(duv1.xy, 0.0f), float3(duv2.xy, 0.0f));");
		SS("float binormalMult = (cr.z < 0.0f) ? -1.0f : 1.0f;");
		SS("float3 vertexBinormal = cross(vertexTangent, vertexNormal) * binormalMult;");
	}
}

std::string colorInput(int item, bool with_alpha, bool inputs_have_alpha, bool hint_single_element) {
	switch (item) {
	default:
	case SHADER_0:
		return with_alpha ? "float4(0.0f, 0.0f, 0.0f, 0.0f)" : "float4(0.0f, 0.0f, 0.0f, 1.0f)";
	case SHADER_INPUT_1:
		return with_alpha || !inputs_have_alpha ? "input1" : "float4(input1.rgb, 1.0f)";
	case SHADER_INPUT_2:
		return with_alpha || !inputs_have_alpha ? "input2" : "float4(input2.rgb, 1.0f)";
	case SHADER_INPUT_3:
		return with_alpha || !inputs_have_alpha ? "input3" : "float4(input3.rgb, 1.0f)";
	case SHADER_INPUT_4:
		return with_alpha || !inputs_have_alpha ? "input4" : "float4(input4.rgb, 1.0f)";
	case SHADER_TEXEL0:
		return with_alpha ? "texVal0" : "float4(texVal0.rgb, 1.0f)";
	case SHADER_TEXEL0A:
		if (hint_single_element) {
			return "float4(texVal0.a, texVal0.a, texVal0.a, texVal0.a)";
		}
		else {
			if (with_alpha) {
				return "float4(texVal0.a, texVal0.a, texVal0.a, texVal0.a)";
			}
			else {
				return "float4(texVal0.a, texVal0.a, texVal0.a, 1.0f)";
			}
		}
	case SHADER_TEXEL1:
		return with_alpha ? "texVal1" : "float4(texVal1.rgb, 1.0f)";
	}
}

std::string colorFormula(int c[2][4], int do_single, int do_multiply, int do_mix, bool with_alpha, int opt_alpha) {
	if (do_single) {
		return colorInput(c[0][3], with_alpha, opt_alpha, false);
	}
	else if (do_multiply) {
		return colorInput(c[0][0], with_alpha, opt_alpha, false) + " * " + colorInput(c[0][2], with_alpha, opt_alpha, true);
	}
	else if (do_mix) {
		return "lerp(" + colorInput(c[0][1], with_alpha, opt_alpha, false) + ", " + colorInput(c[0][0], with_alpha, opt_alpha, false) + ", " + colorInput(c[0][2], with_alpha, opt_alpha, true) + ")";
	}
	else {
		return "(" + colorInput(c[0][0], with_alpha, opt_alpha, false) + " - " + colorInput(c[0][1], with_alpha, opt_alpha, false) + ") * " + colorInput(c[0][2], with_alpha, opt_alpha, true) + ".r + " + colorInput(c[0][3], with_alpha, opt_alpha, false);
	}
}

std::string alphaInput(int item) {
	switch (item) {
	default:
	case SHADER_0:
		return "0.0f";
	case SHADER_INPUT_1:
		return "input1.a";
	case SHADER_INPUT_2:
		return "input2.a";
	case SHADER_INPUT_3:
		return "input3.a";
	case SHADER_INPUT_4:
		return "input4.a";
	case SHADER_TEXEL0:
		return "texVal0.a";
	case SHADER_TEXEL0A:
		return "texVal0.a";
	case SHADER_TEXEL1:
		return "texVal1.a";
	}
}

std::string alphaFormula(int c[2][4], int do_single, int do_multiply, int do_mix, bool with_alpha, int opt_alpha) {
	if (do_single) {
		return alphaInput(c[1][3]);
	}
	else if (do_multiply) {
		return alphaInput(c[1][0]) + " * " + alphaInput(c[1][2]);
	}
	else if (do_mix) {
		return "lerp(" + alphaInput(c[1][1]) + ", " + alphaInput(c[1][0]) + ", " + alphaInput(c[1][2]) + ")";
	}
	else {
		return "(" + alphaInput(c[1][0]) + " - " + alphaInput(c[1][1]) + ") * " + alphaInput(c[1][2]) + " + " + alphaInput(c[1][3]);
	}
}

RT64::Shader::Filter convertFilter(unsigned int filter) {
	switch (filter) {
	case RT64_SHADER_FILTER_LINEAR:
		return RT64::Shader::Filter::Linear;
	case RT64_SHADER_FILTER_POINT:
	default:
		return RT64::Shader::Filter::Point;
	}
}

RT64::Shader::AddressingMode convertAddressingMode(unsigned int mode) {
	switch (mode) {
	case RT64_SHADER_ADDRESSING_CLAMP:
		return RT64::Shader::AddressingMode::Clamp;
	case RT64_SHADER_ADDRESSING_MIRROR:
		return RT64::Shader::AddressingMode::Mirror;
	case RT64_SHADER_ADDRESSING_WRAP:
	default:
		return RT64::Shader::AddressingMode::Wrap;
	}
}

namespace RT64 
{

	Shader::Shader(Device* device, unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr, int flags) {
		assert(device != nullptr);
		this->device = device;
		this->flags = flags;
		this->samplerRegisterIndex = uniqueSamplerRegisterIndex(filter, hAddr, vAddr);

		bool normalMapEnabled = flags & RT64_SHADER_NORMAL_MAP_ENABLED;
		bool specularMapEnabled = flags & RT64_SHADER_SPECULAR_MAP_ENABLED;
		const std::string baseName =
			"Shader_" +
			std::to_string(shaderId) +
			"_" + std::to_string(uniqueSamplerRegisterIndex(filter, hAddr, vAddr)) +
			(normalMapEnabled ? "_Nrm" : "") +
			(specularMapEnabled ? "_Spc" : "");

		if (flags & RT64_SHADER_RASTER_ENABLED) {
			const std::string vertexShader = baseName + "VS";
			const std::string pixelShader = baseName + "PS";
			bool perspective = flags & RT64_SHADER_3D_ENABLED;
			generateRasterGroup(shaderId, filter, hAddr, vAddr, vertexShader, pixelShader, perspective);
		}

		// if (flags & RT64_SHADER_RAYTRACE_ENABLED) {
		// 	const std::string hitGroup = baseName + "HitGroup";
		// 	const std::string closestHit = baseName + "ClosestHit";
		// 	const std::string anyHit = baseName + "AnyHit";
		// 	const std::string shadowHitGroup = baseName + "ShadowHitGroup";
		// 	const std::string shadowClosestHit = baseName + "ShadowClosestHit";
		// 	const std::string shadowAnyHit = baseName + "ShadowAnyHit";
		// 	generateSurfaceHitGroup(shaderId, filter, hAddr, vAddr, normalMapEnabled, specularMapEnabled, hitGroup, closestHit, anyHit);
		// 	generateShadowHitGroup(shaderId, filter, hAddr, vAddr, shadowHitGroup, shadowClosestHit, shadowAnyHit);
		// }

		device->addShader(this);
	}

	Shader::~Shader() {
		device->removeShader(this);

		vkDestroyShaderModule(device->getVkDevice(), rasterGroup.vertexModule, nullptr);
		vkDestroyShaderModule(device->getVkDevice(), rasterGroup.fragmentModule, nullptr);
		vkDestroyPipeline(device->getVkDevice(), rasterGroup.pipeline, nullptr);
		vkDestroyPipelineLayout(device->getVkDevice(), rasterGroup.pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device->getVkDevice(), rasterGroup.descriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(device->getVkDevice(), rasterGroup.descriptorPool, nullptr);

		// vkDestroyShaderModule(device->getVkDevice(), surfaceHitGroup.shaderModule, nullptr);
		// vkDestroyPipelineLayout(device->getVkDevice(), surfaceHitGroup.pipelineLayout, nullptr);
		// vkDestroyDescriptorSetLayout(device->getVkDevice(), surfaceHitGroup.descriptorSetLayout, nullptr);
		// vkDestroyDescriptorPool(device->getVkDevice(), surfaceHitGroup.descriptorPool, nullptr);

		// vkDestroyShaderModule(device->getVkDevice(), shadowHitGroup.shaderModule, nullptr);
		// vkDestroyPipelineLayout(device->getVkDevice(), shadowHitGroup.pipelineLayout, nullptr);
		// vkDestroyDescriptorSetLayout(device->getVkDevice(), shadowHitGroup.descriptorSetLayout, nullptr);
		// vkDestroyDescriptorPool(device->getVkDevice(), shadowHitGroup.descriptorPool, nullptr);
	}

	void Shader::generateRasterGroup(
			unsigned int shaderId, 
			Filter filter, 
			AddressingMode hAddr, 
			AddressingMode vAddr, 
			const std::string &vertexShaderName, 
			const std::string &pixelShaderName, 
			bool perspective) 
	{
		ColorCombinerParams cc(shaderId);
		bool vertexUV = cc.useTextures[0] || cc.useTextures[1];
		VertexLayout vl(true, true, vertexUV, cc.inputCount, cc.opt_alpha);

		std::stringstream ss;
		SS(INCLUDE_HLSLI(MaterialsHLSLI));
		SS(INCLUDE_HLSLI(InstancesHLSLI));
		SS(INCLUDE_HLSLI(GlobalParamsHLSLI));
		// SS("int instanceId : register(b1);");
		SS("int blinstanceId : register(b1);");

		unsigned int samplerRegisterIndex = uniqueSamplerRegisterIndex(filter, hAddr, vAddr);
		if (cc.useTextures[0]) {
			SS("SamplerState gTextureSampler : register(s" + std::to_string(samplerRegisterIndex) + ");");
			SS(INCLUDE_HLSLI(TexturesHLSLI));
		}

		// Vertex shader.
		SS("void " + vertexShaderName + "(");
		SS("    in float4 iPosition : POSITION,");
		SS("    in float3 iNormal : NORMAL,");
		if (vertexUV) {
			SS("    in float2 iUV : TEXCOORD,");
		}
		for (int i = 0; i < cc.inputCount; i++) {
			const std::string floatNumber = cc.opt_alpha ? "4" : "3";
			SS("    in float" + floatNumber + " iInput" + std::to_string(i + 1) + " : COLOR" + std::to_string(i) + ",");
		}
		SS("    out float4 oPosition : SV_POSITION,");
		SS("    out float3 oNormal : NORMAL,");
		if (vertexUV) {
			SS("    out float2 oUV : TEXCOORD" + std::string((cc.inputCount > 0) ? "," : ""));
		}
		for (int i = 0; i < cc.inputCount; i++) {
			SS("    out float4 oInput" + std::to_string(i + 1) + " : COLOR" + std::to_string(i) + std::string(((i + 1) < cc.inputCount) ? "," : ""));
		}
		SS(") {");
		if (perspective){
			// SS("    oPosition = mul(projection, mul(view, mul(instanceTransforms[instanceId].objectToWorld, float4(iPosition.xyz, 1.0))));");
			SS("    oPosition = mul(projection, mul(view, mul(instanceTransforms[0].objectToWorld, float4(iPosition.xyz, 1.0))));");
		} else {
			SS("    oPosition = iPosition;");
		}
		SS("    oNormal = iNormal;");
		if (vertexUV) {
			SS("    oUV = iUV;");
		}
		for (int i = 0; i < cc.inputCount; i++) {
			SS("    oInput" + std::to_string(i + 1) + " = " + std::string(cc.opt_alpha ? "" : "float4(") + "iInput" + std::to_string(i + 1) + std::string(cc.opt_alpha ? "" : ", 1.0f)") + ";");
		}
		SS("}");

		// Pixel shader.
		SS("void " + pixelShaderName + "(");
		SS("    in float4 vertexPosition : SV_POSITION,");
		SS("    in float3 vertexNormal : NORMAL,");
		if (vertexUV) {
			SS("    in float2 vertexUV : TEXCOORD,");
		}
		for (int i = 0; i < cc.inputCount; i++) {
			SS("    in float4 input" + std::to_string(i + 1) + " : COLOR" + std::to_string(i) + ",");
		}
		SS("    out float4 resultColor : SV_TARGET");
		SS(") {");

		if (cc.useTextures[0]) {
			SS("    int diffuseTexIndex = instanceMaterials[0].diffuseTexIndex;");
			// SS("    float4 texVal0 = gTextures[NonUniformResourceIndex(diffuseTexIndex)].Sample(gTextureSampler, vertexUV);");
			SS("    float4 texVal0 = gTextures[0].Sample(gTextureSampler, vertexUV);");
		}

		if (cc.useTextures[1]) {
			// TODO
			SS("    float4 texVal1 = float4(1.0f, 0.0f, 1.0f, 1.0f);");
		}

		if (!cc.color_alpha_same && cc.opt_alpha) {
			SS("    resultColor = float4((" + colorFormula(cc.c, cc.do_single[0], cc.do_multiply[0], cc.do_mix[0], false, true) + ").rgb, " + alphaFormula(cc.c, cc.do_single[1], cc.do_multiply[1], cc.do_mix[1], true, true) + ");");
		}
		else {
			SS("    resultColor = " + colorFormula(cc.c, cc.do_single[0], cc.do_multiply[0], cc.do_mix[0], cc.opt_alpha, cc.opt_alpha) + ";");
		}
		SS("}");

		// Compile the shaders
		std::string shaderCode = ss.str();
		rasterGroup.pixelShaderName = pixelShaderName;
		rasterGroup.vertexShaderName = vertexShaderName;
		VkPipelineShaderStageCreateInfo vertexStage = {};
		VkPipelineShaderStageCreateInfo fragmentStage = {};
		compileShaderCode(shaderCode, VK_SHADER_STAGE_VERTEX_BIT, vertexShaderName, L"vs_6_3", vertexStage, rasterGroup.vertexModule);
		compileShaderCode(shaderCode, VK_SHADER_STAGE_FRAGMENT_BIT, pixelShaderName, L"ps_6_3", fragmentStage, rasterGroup.fragmentModule);
		generateRasterDescriptorSetLayout(filter, hAddr, vAddr, samplerRegisterIndex, rasterGroup.descriptorSetLayout, rasterGroup.descriptorPool, rasterGroup.descriptorSet);

		// Create the pipeline layout
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &rasterGroup.descriptorSetLayout; 

        VK_CHECK(vkCreatePipelineLayout(device->getVkDevice(), &pipelineLayoutInfo, nullptr, &rasterGroup.pipelineLayout));
		
		// The rest of this involves creating the pipeline

		// Dynamic states
        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages {
			vertexStage, fragmentStage
		};
        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();
        
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // Viewport state
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        // Rasterizer
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        // Color blending
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional
        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

		// Depth stencil
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.stencilTestEnable = VK_FALSE;
        depthStencil.front = {}; // Optional
        depthStencil.back = {}; // Optional

		// Define the vertex layout.
        VkVertexInputBindingDescription vertexBind{};
        vertexBind.binding = 0;
        vertexBind.stride = vl.vertexSize;
        vertexBind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		// Create the attributes for the vertex inputs
        std::vector<VkVertexInputAttributeDescription> attributes;
		attributes.push_back({0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, });
		attributes.push_back({1, 0, VK_FORMAT_R32G32B32_SFLOAT, vl.normalOffset});
		if (vertexUV) {
			attributes.push_back({2, 0, VK_FORMAT_R32G32_SFLOAT, vl.uvOffset});
		}
		for (unsigned int i = 0; i < cc.inputCount; i++) {
			attributes.push_back({vertexUV ? 3 + i : 2 + i, 0, cc.opt_alpha ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R32G32B32_SFLOAT, vl.inputOffset[i]});
		}
        // Bind the vertex inputs
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertexInputInfo.pVertexBindingDescriptions = &vertexBind;
        vertexInputInfo.pVertexAttributeDescriptions = attributes.data();

        // With your powers combined, I am Captain Pipeline!!!
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = shaderStages.size();
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = rasterGroup.pipelineLayout;
        pipelineInfo.renderPass = device->getRenderPass();
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
        pipelineInfo.basePipelineIndex = -1; // Optional
        VK_CHECK(vkCreateGraphicsPipelines(device->getVkDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &rasterGroup.pipeline));
	}

	// Creates the descriptor set layout and pool
	void Shader::generateRasterDescriptorSetLayout(Filter filter, AddressingMode hAddr, AddressingMode vAddr, uint32_t samplerRegisterIndex, VkDescriptorSetLayout& descriptorSetLayout, VkDescriptorPool& descriptorPool, VkDescriptorSet& descriptorSet) {
		VkDevice& device = this->device->getVkDevice();
		VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

        std::vector<VkDescriptorSetLayoutBinding> bindings;
        std::vector<VkDescriptorPoolSize> poolSizes{};
		bindings.push_back({CBV_INDEX(gParams) + CBV_SHIFT, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr});
		poolSizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1});
		bindings.push_back({1 + CBV_SHIFT, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
		poolSizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1});
        bindings.push_back({SRV_INDEX(instanceTransforms) + SRV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr});
		poolSizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1});
        bindings.push_back({SRV_INDEX(instanceMaterials) + SRV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
		poolSizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1});
        bindings.push_back({SRV_INDEX(gTextures) + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, SRV_TEXTURES_MAX, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
		poolSizes.push_back({VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, SRV_TEXTURES_MAX});
        bindings.push_back({samplerRegisterIndex + SAMPLER_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
		poolSizes.push_back({VK_DESCRIPTOR_TYPE_SAMPLER, 1});

        std::vector<VkDescriptorBindingFlags> vectorOfFlags(bindings.size(), flags);
		VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo {};
		flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
		flagsInfo.bindingCount = vectorOfFlags.size();
		flagsInfo.pBindingFlags = vectorOfFlags.data();
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = bindings.size();
        layoutInfo.pBindings = bindings.data();
		layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
		layoutInfo.pNext = &flagsInfo;
        VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout));

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = poolSizes.size();
        poolInfo.pPoolSizes = poolSizes.data();
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        poolInfo.maxSets = 1;
		VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool));

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;
        VkResult res = vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);
	}
	
	void Shader::compileShaderCode(const std::string& shaderCode, VkShaderStageFlagBits stage, const std::string& entryName, const std::wstring& profile, VkPipelineShaderStageCreateInfo& shaderStage, VkShaderModule& shaderModule) {
#ifndef NDEBUG
		fprintf(stdout, "Compiling...\n\n%s\n", shaderCode.c_str());
#endif
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> stringConverter;		// Because DXC required wstrings for some reason
		IDxcBlob* dxcBlob;
		IDxcBlobEncoding* textBlob = nullptr;
		D3D12_CHECK(device->getDxcLibrary()->CreateBlobWithEncodingFromPinned((LPBYTE)shaderCode.c_str(), (uint32_t)shaderCode.size(), 0, &textBlob));

		// Good ol Microsoft making this shit more complicated than it needed to be
		std::vector<LPCWSTR> arguments;
		std::wstring srv_shift = std::to_wstring(SRV_SHIFT);
		LPCWSTR __srv_shift = srv_shift.c_str();
		std::wstring uav_shift = std::to_wstring(UAV_SHIFT);
		LPCWSTR __uav_shift = uav_shift.c_str();
		std::wstring cbv_shift = std::to_wstring(CBV_SHIFT);
		LPCWSTR __cbv_shift = cbv_shift.c_str();
		std::wstring sampler_shift = std::to_wstring(SAMPLER_SHIFT);
		LPCWSTR __sampler_shift = sampler_shift.c_str();
		arguments.push_back(L"-spirv");
		arguments.push_back(L"-fspv-target-env=vulkan1.2");	
		arguments.push_back(L"-fvk-t-shift");
		arguments.push_back(__srv_shift);	
		arguments.push_back(L"0");
		arguments.push_back(L"-fvk-s-shift");
		arguments.push_back(__sampler_shift);	
		arguments.push_back(L"0");
		arguments.push_back(L"-fvk-u-shift");
		arguments.push_back(__uav_shift);	
		arguments.push_back(L"0");
		arguments.push_back(L"-fvk-b-shift");
		arguments.push_back(__cbv_shift);
		arguments.push_back(L"0");
		arguments.push_back(L"-Qstrip_debug");
		// Can't do this in SPIR-V
		// arguments.push_back(L"-Qstrip_reflect");		

		IDxcOperationResult *result = nullptr;
		D3D12_CHECK(device->getDxcCompiler()->Compile(textBlob, L"", stringConverter.from_bytes(entryName).c_str(), profile.c_str(), arguments.data(), (UINT32)(arguments.size()), nullptr, 0, nullptr, &result));

		HRESULT resultCode;
		D3D12_CHECK(result->GetStatus(&resultCode));
		if (FAILED(resultCode)) {
			IDxcBlobEncoding *error;
			HRESULT hr = result->GetErrorBuffer(&error);
			if (FAILED(hr)) {
				throw std::runtime_error("Failed to get shader compiler error");
			}

			// Convert error blob to a string.
			std::vector<char> infoLog(error->GetBufferSize() + 1);
			memcpy(infoLog.data(), error->GetBufferPointer(), error->GetBufferSize());
			infoLog[error->GetBufferSize()] = 0;

			throw std::runtime_error("Shader compilation error: " + std::string(infoLog.data()));
		}

		// Get the result from the text blob into dxcBlob
		D3D12_CHECK(result->GetResult(&dxcBlob));
		
		// Now we're done with the DXC stuff. Here's some Vulkan stuff
		// Get the dxcBlob into a void*
		uint8_t* bobBlobLaw = static_cast<uint8_t*>(dxcBlob->GetBufferPointer());
		VkDeviceSize gobBluth = dxcBlob->GetBufferSize();
		// for (int i = 0; i < gobBluth; i++) {
		// 	printf("Ox%02x", bobBlobLaw[i]);
		// 	if (i % 16 == 15) {
		// 		std::cout << "\n";
		// 	} else {
		// 		std::cout << " ";
		// 	}
		// }
		device->createShaderModule(bobBlobLaw, gobBluth, entryName.c_str(), stage, shaderStage, shaderModule, nullptr);
	}

	unsigned int Shader::uniqueSamplerRegisterIndex(Filter filter, AddressingMode hAddr, AddressingMode vAddr) {
		// Index 0 is reserved by the sampler used in the tracer.
		unsigned int uniqueID = 1;
		uniqueID += (unsigned int)(filter) * 9;
		uniqueID += (unsigned int)(hAddr) * 3;
		uniqueID += (unsigned int)(vAddr);
		return uniqueID;
	}

// Public
	const Shader::RasterGroup& RT64::Shader::getRasterGroup() const { return rasterGroup; }
	bool Shader::hasRasterGroup() const { return (rasterGroup.vertexModule != nullptr) || (rasterGroup.fragmentModule != nullptr); }
	Shader::HitGroup& Shader::getSurfaceHitGroup() { return surfaceHitGroup; }
	Shader::HitGroup& Shader::getShadowHitGroup() { return shadowHitGroup; }
	bool Shader::hasHitGroups() const { return (surfaceHitGroup.shaderModule != nullptr) || (shadowHitGroup.shaderModule != nullptr); }
	uint32_t Shader::getFlags() const { return flags; }
	unsigned int Shader::getSamplerRegisterIndex() const { return samplerRegisterIndex; }

};

// Library exports

DLEXPORT RT64_SHADER *RT64_CreateShader(RT64_DEVICE *devicePtr, unsigned int shaderId, unsigned int filter, unsigned int hAddr, unsigned int vAddr, int flags) {
    try {
        RT64::Device *device = (RT64::Device *)(devicePtr);
		RT64::Shader::Filter sFilter = convertFilter(filter);
		RT64::Shader::AddressingMode sHAddr = convertAddressingMode(hAddr);
		RT64::Shader::AddressingMode sVAddr = convertAddressingMode(vAddr);
        return (RT64_SHADER *)(new RT64::Shader(device, shaderId, sFilter, sHAddr, sVAddr, flags));
    }
    RT64_CATCH_EXCEPTION();
    return nullptr;
}

DLEXPORT void RT64_DestroyShader(RT64_SHADER *shaderPtr) {
	delete (RT64::Shader *)(shaderPtr);
}

#endif