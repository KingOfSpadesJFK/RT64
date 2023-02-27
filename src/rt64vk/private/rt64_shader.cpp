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
	SS("[[vk::shader_record_ext]] cbuffer sbtData {");
	SS("	uint64_t vertexBuffer;");
	SS("	uint64_t indexBuffer;")
	SS("};");

}

void getVertexData(std::stringstream &ss, bool vertexPosition, bool vertexNormal, bool vertexUV, int inputCount, bool useAlpha, bool vertexBinormalAndTangent) {
	VertexLayout vl(vertexPosition, vertexNormal, vertexUV, inputCount, useAlpha);

	SS("uint3 index3 = vk::RawBufferLoad<uint3>(indexBuffer + (triangleIndex * 3) * 4);");

	if (vertexPosition) {
		for (int i = 0; i < 3; i++) {
			SS("float3 pos" + std::to_string(i) + " = vk::RawBufferLoad<float3>(vertexBuffer + index3[" + std::to_string(i) + "] * " + std::to_string(vl.vertexSize) + " + " + std::to_string(vl.positionOffset) + ");");
			SS("float3 posW" + std::to_string(i) + " = mul(instanceTransforms[instanceId].objectToWorld, float4(pos" + std::to_string(i) + ", 1.0f)).xyz; ");
		}

		SS("float3 vertexPosition = pos0 * barycentrics[0] + pos1 * barycentrics[1] + pos2 * barycentrics[2];");
	}

	if (vertexNormal) {
		for (int i = 0; i < 3; i++) {
			SS("float3 norm" + std::to_string(i) + " = vk::RawBufferLoad<float3>(vertexBuffer + index3[" + std::to_string(i) + "] * " + std::to_string(vl.vertexSize) + " + " + std::to_string(vl.normalOffset) + ");");
		}

		SS("float3 vertexNormal = norm0 * barycentrics[0] + norm1 * barycentrics[1] + norm2 * barycentrics[2];");
		SS("float3 triangleNormal = -cross(pos2 - pos0, pos1 - pos0);");
		SS("vertexNormal = any(vertexNormal) ? normalize(vertexNormal) : triangleNormal;");

		// Transform the triangle normal.
		SS("triangleNormal = normalize(mul(instanceTransforms[instanceId].objectToWorldNormal, float4(triangleNormal, 0.f)).xyz);");
	}

	if (vertexUV) {
		for (int i = 0; i < 3; i++) {
			SS("float2 uv" + std::to_string(i) + " = vk::RawBufferLoad<float2>(vertexBuffer + index3[" + std::to_string(i) + "] * " + std::to_string(vl.vertexSize) + " + " + std::to_string(vl.uvOffset) + ");");
		}

		SS("float2 vertexUV = uv0 * barycentrics[0] + uv1 * barycentrics[1] + uv2 * barycentrics[2];");
	}

	for (int i = 0; i < inputCount; i++) {
		std::string floatNum = useAlpha ? "4" : "3";
		std::string index = std::to_string(i + 1);
		for (int j = 0; j < 3; j++) {
			SS("float" + floatNum + " input" + index + std::to_string(j) + " = vk::RawBufferLoad<float" + floatNum + ">(vertexBuffer + index3[" + std::to_string(j) + "] * " + std::to_string(vl.vertexSize) + " + " + std::to_string(vl.inputOffset[i]) + ");");
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
		SS("if (uvk != 0) {");
		SS("	vertexTangent = normalize((uvc * dpos2 - uvd * dpos1) / uvk);");
		SS("}");
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
		this->samplerRegisterIndex = uniqueSamplerRegisterIndex((uint32_t)filter, (uint32_t)hAddr, (uint32_t)vAddr);

		bool normalMapEnabled = flags & RT64_SHADER_NORMAL_MAP_ENABLED;
		bool specularMapEnabled = flags & RT64_SHADER_SPECULAR_MAP_ENABLED;
		const std::string baseName =
			"Shader_" +
			std::to_string(shaderId) +
			"_" + std::to_string(samplerRegisterIndex) +
			(normalMapEnabled ? "_Nrm" : "") +
			(specularMapEnabled ? "_Spc" : "");

		if (flags & RT64_SHADER_RASTER_ENABLED) {
			const std::string vertexShader = baseName + "VS";
			const std::string pixelShader = baseName + "PS";
			generateRasterGroup(shaderId, filter, flags & RT64_SHADER_RASTER_TRANSFORMS_ENABLED, hAddr, vAddr, vertexShader, pixelShader);
			rasterGroupInit = true;
		}

		if (flags & RT64_SHADER_RAYTRACE_ENABLED) {
			const std::string hitGroup = baseName + "HitGroup";
			const std::string closestHit = baseName + "ClosestHit";
			const std::string anyHit = baseName + "AnyHit";
			const std::string shadowHitGroup = baseName + "ShadowHitGroup";
			const std::string shadowClosestHit = baseName + "ShadowClosestHit";
			const std::string shadowAnyHit = baseName + "ShadowAnyHit";
			generateSurfaceHitGroup(shaderId, filter, hAddr, vAddr, normalMapEnabled, specularMapEnabled, hitGroup, closestHit, anyHit);
			generateShadowHitGroup(shaderId, filter, hAddr, vAddr, shadowHitGroup, shadowClosestHit, shadowAnyHit);
			hitGroupInit = true;
		}

		device->addShader(this);
	}

	Shader::~Shader() {
		device->removeShader(this);

		vkDestroyShaderModule(device->getVkDevice(), rasterGroup.vertexModule, nullptr);
		vkDestroyShaderModule(device->getVkDevice(), rasterGroup.fragmentModule, nullptr);
		vkDestroyPipeline(device->getVkDevice(), rasterGroup.pipeline, nullptr);
		vkDestroyPipelineLayout(device->getVkDevice(), rasterGroup.pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device->getVkDevice(), rasterGroup.descriptorSetLayout, nullptr);

		vkDestroyShaderModule(device->getVkDevice(), surfaceHitGroup.shaderModule, nullptr);
		vkDestroyShaderModule(device->getVkDevice(), shadowHitGroup.shaderModule, nullptr);
	}

	void Shader::generateRasterGroup(
			unsigned int shaderId, 
			Filter filter, 
            bool use3DTransforms,
			AddressingMode hAddr, 
			AddressingMode vAddr, 
			const std::string &vertexShaderName, 
			const std::string &pixelShaderName) 
	{
		ColorCombinerParams cc(shaderId);
		bool vertexUV = cc.useTextures[0] || cc.useTextures[1];
		VertexLayout vl(true, true, vertexUV, cc.inputCount, cc.opt_alpha);

		std::stringstream ss;
		SS(INCLUDE_HLSLI(MaterialsHLSLI));
		SS(INCLUDE_HLSLI(InstancesHLSLI));
		SS(INCLUDE_HLSLI(GlobalParamsHLSLI));
		SS("struct PushConstant { int instanceId; };");
		SS("[[vk::push_constant]] PushConstant pc;");

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
		if (use3DTransforms) {
			SS("    oPosition = mul(projection, mul(view, mul(instanceTransforms[pc.instanceId].objectToWorld, float4(iPosition.xyz, 1.0))));");
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
			SS("    int diffuseTexIndex = instanceMaterials[pc.instanceId].diffuseTexIndex;");
			SS("    float4 texVal0 = gTextures[NonUniformResourceIndex(diffuseTexIndex)].Sample(gTextureSampler, vertexUV);");
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
		rasterGroup.id = device->getRasterGroupCount();
		compileShaderCode(shaderCode, VK_SHADER_STAGE_VERTEX_BIT, vertexShaderName, L"vs_6_3", rasterGroup.vertexInfo, rasterGroup.vertexModule);
		compileShaderCode(shaderCode, VK_SHADER_STAGE_FRAGMENT_BIT, pixelShaderName, L"ps_6_3", rasterGroup.fragmentInfo, rasterGroup.fragmentModule);
		generateRasterDescriptorSetLayout(filter, use3DTransforms, hAddr, vAddr, samplerRegisterIndex, rasterGroup.descriptorSetLayout, rasterGroup.descriptorSet);

		// Set up the push constnants
		VkPushConstantRange pushConstant;
		pushConstant.offset = 0;
		pushConstant.size = sizeof(uint32_t);
		pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		// Create the pipeline layout
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &rasterGroup.descriptorSetLayout; 
		// Get the push constant into the pipeline layout
		pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
		pipelineLayoutInfo.pushConstantRangeCount = 1;

        VK_CHECK(vkCreatePipelineLayout(device->getVkDevice(), &pipelineLayoutInfo, nullptr, &rasterGroup.pipelineLayout));
		
		// The rest of this involves creating the pipeline

		// Dynamic states
        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages {
			rasterGroup.vertexInfo, rasterGroup.fragmentInfo
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
        rasterizer.depthClampEnable = VK_TRUE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = use3DTransforms ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;
		rasterizer.depthBiasClamp = 100.0f;

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
        pipelineInfo.renderPass = device->getPresentRenderPass();
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
        pipelineInfo.basePipelineIndex = -1; // Optional
        VK_CHECK(vkCreateGraphicsPipelines(device->getVkDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &rasterGroup.pipeline));
	}

	void Shader::generateSurfaceHitGroup(unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr, bool normalMapEnabled, bool specularMapEnabled, const std::string& hitGroupName, const std::string& closestHitName, const std::string& anyHitName) {
		ColorCombinerParams cc(shaderId);

		std::stringstream ss;
		incMeshBuffers(ss);

		SS(INCLUDE_HLSLI(MaterialsHLSLI));
		SS(INCLUDE_HLSLI(InstancesHLSLI));
		SS(INCLUDE_HLSLI(GlobalHitBuffersHLSLI));
		SS(INCLUDE_HLSLI(RayHLSLI));
		SS(INCLUDE_HLSLI(RandomHLSLI));
		SS(INCLUDE_HLSLI(GlobalParamsHLSLI));

		if (cc.useTextures[0]) {
			SS("SamplerState gTextureSampler : register(s" + std::to_string(samplerRegisterIndex) + ");");
			SS(INCLUDE_HLSLI(TexturesHLSLI));
		}

		SS("[shader(\"anyhit\")]");
		SS("void " + anyHitName + "(inout HitInfo payload : SV_RayPayload, in Attributes attrib) {");
		SS("    uint instanceId = InstanceIndex();");
		SS("    uint triangleIndex = PrimitiveIndex();");
		SS("    float3 barycentrics = float3((1.0f - attrib.bary.x - attrib.bary.y), attrib.bary.x, attrib.bary.y);");
		SS("    float4 diffuseColorMix = instanceMaterials[instanceId].diffuseColorMix;");

		bool vertexUV = cc.useTextures[0] || cc.useTextures[1];
		getVertexData(ss, true, true, vertexUV, cc.inputCount, cc.opt_alpha, vertexUV && normalMapEnabled);

		if (cc.useTextures[0]) {
			SS("	float2 ddx, ddy;");
			SS("	RayDiff propRayDiff = propagateRayDiffs(payload.rayDiff, WorldRayDirection(), RayTCurrent(), triangleNormal);");
			SS("	float2 dBarydx, dBarydy;");
			SS("	computeBarycentricDifferentials(propRayDiff, WorldRayDirection(), posW1 - posW0, posW2 - posW0, triangleNormal, dBarydx, dBarydy);");
			SS("	computeTextureDifferentials(dBarydx, dBarydy, uv0, uv1, uv2, ddx, ddy);");
			SS("    int diffuseTexIndex = instanceMaterials[instanceId].diffuseTexIndex;");
			SS("    float4 texVal0 = gTextures[NonUniformResourceIndex(diffuseTexIndex)].SampleGrad(gTextureSampler, vertexUV, ddx, ddy);");
			SS("    texVal0.rgb = lerp(texVal0.rgb, diffuseColorMix.rgb, max(-diffuseColorMix.a, 0.0f));");
		}

		if (cc.useTextures[1]) {
			// TODO
			SS("    float4 texVal1 = float4(1.0f, 0.0f, 1.0f, 1.0f);");
		}

		if (!cc.color_alpha_same && cc.opt_alpha) {
			SS("    float4 resultColor = float4((" + colorFormula(cc.c, cc.do_single[0], cc.do_multiply[0], cc.do_mix[0], false, true) + ").rgb, " + alphaFormula(cc.c, cc.do_single[1], cc.do_multiply[1], cc.do_mix[1], true, true) + ");");
		}
		else {
			SS("    float4 resultColor = " + colorFormula(cc.c, cc.do_single[0], cc.do_multiply[0], cc.do_mix[0], cc.opt_alpha, cc.opt_alpha) + ";");
		}

		// Only mix the final diffuse color if the alpha is positive.
		SS("    resultColor.rgb = lerp(resultColor.rgb, diffuseColorMix.rgb, max(diffuseColorMix.a, 0.0f));");

		// Apply the solid alpha multiplier.
		SS("    resultColor.a = clamp(instanceMaterials[instanceId].solidAlphaMultiplier * resultColor.a, 0.0f, 1.0f);");

#ifdef TEXTURE_EDGE_ENABLED
		if (cc.opt_texture_edge) {
			SS("    if (resultColor.a > 0.3f) {");
			SS("      resultColor.a = 1.0f;");
			SS("    }");
			SS("    else {");
			SS("      IgnoreHit();");
			SS("    }");
		}
#endif

		if (cc.opt_noise) {
			SS("    uint seed = initRand(DispatchRaysIndex().x + DispatchRaysIndex().y * DispatchRaysDimensions().x, frameCount, 16);");
			SS("    resultColor.a *= round(nextRand(seed));");
		}
		
		SS("vertexNormal = normalize(mul(instanceTransforms[instanceId].objectToWorldNormal, float4(vertexNormal, 0.f)).xyz);");
		SS("float normalSign = (dot(triangleNormal, WorldRayDirection()) <= 0.0f) ? 1.0f : -1.0f;");
		SS("vertexNormal *= normalSign;");
		
		if (vertexUV && normalMapEnabled) {
			SS("    vertexTangent = normalize(mul(instanceTransforms[instanceId].objectToWorldNormal, float4(vertexTangent, 0.f)).xyz) * normalSign;");
			SS("    vertexBinormal = normalize(mul(instanceTransforms[instanceId].objectToWorldNormal, float4(vertexBinormal, 0.f)).xyz) * normalSign;");
			SS("    float3x3 tbn = float3x3(vertexTangent, vertexBinormal, vertexNormal);");
			SS("    int normalTexIndex = instanceMaterials[instanceId].normalTexIndex;");
			SS("    if (normalTexIndex >= 0) {");
			SS("        float uvDetailScale = instanceMaterials[instanceId].uvDetailScale;");
			SS("        float3 normalColor = gTextures[NonUniformResourceIndex(normalTexIndex)].SampleGrad(gTextureSampler, vertexUV * uvDetailScale, ddx * uvDetailScale, ddy * uvDetailScale).xyz;");
			SS("        normalColor.y = 1.0f - normalColor.y;");
			SS("        normalColor = (normalColor * 2.0f) - 1.0f;");
			SS("        float3 newNormal = normalize(mul(normalColor, tbn));");
			SS("        vertexNormal = newNormal;");
			SS("    }");
		}

		SS("	float3 prevWorldPos = mul(instanceTransforms[instanceId].objectToWorldPrevious, float4(vertexPosition, 1.0f)).xyz;");
		SS("	float3 curWorldPos = mul(instanceTransforms[instanceId].objectToWorld, float4(vertexPosition, 1.0f)).xyz;");
		SS("	float3 vertexFlow = curWorldPos - prevWorldPos;");
		SS("    float3 vertexSpecular = float3(1.0f, 1.0f, 1.0f);");
		if (vertexUV && specularMapEnabled) {
			SS("    int specularTexIndex = instanceMaterials[instanceId].specularTexIndex;");
			SS("    if (specularTexIndex >= 0) {");
			SS("        float uvDetailScale = instanceMaterials[instanceId].uvDetailScale;");
			SS("        vertexSpecular = gTextures[NonUniformResourceIndex(specularTexIndex)].SampleGrad(gTextureSampler, vertexUV * uvDetailScale, ddx * uvDetailScale, ddy * uvDetailScale).rgb;");
			SS("    }");
		}

		SS("    uint2 pixelIdx = DispatchRaysIndex().xy;");
		SS("    uint2 pixelDims = DispatchRaysDimensions().xy;");
		SS("    uint hitStride = pixelDims.x * pixelDims.y;");

	// 	// HACK: Add some bias for the comparison based on the instance ID so coplanar surfaces are friendlier with each other.
	// 	// This can likely be implemented as an instance property at some point to control depth sorting.
		SS("    float tval = WithDistanceBias(RayTCurrent(), instanceId);");
		SS("    uint hi = getHitBufferIndex(min(payload.nhits, MAX_HIT_QUERIES), pixelIdx, pixelDims);");
		SS("    uint minHi = getHitBufferIndex(0, pixelIdx, pixelDims);");
		SS("    uint lo = hi - hitStride;");
		SS("    while ((hi > minHi) && (tval < gHitDistAndFlow[lo].x)) {");
		SS("        gHitDistAndFlow[hi] = gHitDistAndFlow[lo];");
		SS("        gHitColor[hi] = gHitColor[lo];");
		SS("        gHitNormal[hi] = gHitNormal[lo];");
		SS("        gHitSpecular[hi] = gHitSpecular[lo];");
		SS("        gHitInstanceId[hi] = gHitInstanceId[lo];");
		SS("        hi -= hitStride;");
		SS("        lo -= hitStride;");
		SS("    }");
		SS("    uint hitPos = hi / hitStride;");
		SS("    if (hitPos >= MAX_HIT_QUERIES) {");
		SS("    	IgnoreHit();");
		SS("    } else {");
		SS("    	gHitDistAndFlow[hi] = float4(tval, vertexFlow);");
		SS("    	gHitColor[hi] = resultColor;");
		SS("    	gHitNormal[hi] = float4(vertexNormal, 1.0f);");
		SS("    	gHitSpecular[hi] = float4(vertexSpecular, 1.0f);");
		SS("    	gHitInstanceId[hi] = instanceId;");
		SS("    	++payload.nhits;");
		SS("    	if (hitPos != MAX_HIT_QUERIES - 1) {");
		SS("    		IgnoreHit();");
		SS("		}");
		SS("    }");
		SS("}");
		SS("[shader(\"closesthit\")]");
		SS("void " + closestHitName + "(inout HitInfo payload : SV_RayPayload, in Attributes attrib) { }");

		// Compile shader.
		std::string shaderCode = ss.str();
		surfaceHitGroup.id = device->getHitGroupCount();
		compileShaderCode(shaderCode, VK_SHADER_STAGE_ANY_HIT_BIT_KHR, "", L"lib_6_3", surfaceHitGroup.shaderInfo, surfaceHitGroup.shaderModule);
		surfaceHitGroup.hitGroupName = hitGroupName;
		surfaceHitGroup.closestHitName = closestHitName;
		surfaceHitGroup.anyHitName = anyHitName;
		surfaceHitGroup.shaderInfo.pName = surfaceHitGroup.anyHitName.c_str();
	}

	void Shader::generateShadowHitGroup(unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr, const std::string &hitGroupName, const std::string &closestHitName, const std::string &anyHitName) {
		ColorCombinerParams cc(shaderId);
		std::stringstream ss;
		incMeshBuffers(ss);
		
		SS(INCLUDE_HLSLI(MaterialsHLSLI));
		SS(INCLUDE_HLSLI(InstancesHLSLI));
		SS(INCLUDE_HLSLI(RayHLSLI));
		SS(INCLUDE_HLSLI(RandomHLSLI));
		SS(INCLUDE_HLSLI(GlobalParamsHLSLI));

		if (cc.useTextures[0]) {
			SS("SamplerState gTextureSampler : register(s" + std::to_string(samplerRegisterIndex) + ");");
			SS(INCLUDE_HLSLI(TexturesHLSLI));
		}
		
		SS("[shader(\"anyhit\")]");
		SS("void " + anyHitName + "(inout ShadowHitInfo payload : SV_RayPayload, in Attributes attrib) {");
		if (cc.opt_alpha) {
			SS("    uint instanceId = InstanceIndex();");
			SS("    uint triangleIndex = PrimitiveIndex();");
			SS("    float3 barycentrics = float3((1.0f - attrib.bary.x - attrib.bary.y), attrib.bary.x, attrib.bary.y);");

			getVertexData(ss, true, true, cc.useTextures[0] || cc.useTextures[1], cc.inputCount, cc.opt_alpha, false);

			if (cc.useTextures[0]) {
				SS("    int diffuseTexIndex = instanceMaterials[instanceId].diffuseTexIndex;");
				SS("    float4 texVal0 = gTextures[NonUniformResourceIndex(diffuseTexIndex)].SampleLevel(gTextureSampler, vertexUV, 0);");
			}

			if (cc.useTextures[1]) {
				// TODO
				SS("    float4 texVal1 = float4(1.0f, 0.0f, 1.0f, 1.0f);");
			}

			if (!cc.color_alpha_same && cc.opt_alpha) {
				SS("    float resultAlpha = " + alphaFormula(cc.c, cc.do_single[1], cc.do_multiply[1], cc.do_mix[1], true, true) + ";");
			}
			else {
				SS("    float resultAlpha = (" + colorFormula(cc.c, cc.do_single[0], cc.do_multiply[0], cc.do_mix[0], cc.opt_alpha, cc.opt_alpha) + ").a;");
			}

			SS("    resultAlpha = clamp(resultAlpha * instanceMaterials[instanceId].shadowAlphaMultiplier, 0.0f, 1.0f);");
			
#ifdef TEXTURE_EDGE_ENABLED
			if (cc.opt_texture_edge) {
				SS("    if (resultAlpha > 0.3f) {");
				SS("      resultAlpha = 1.0f;");
				SS("    }");
				SS("    else {");
				SS("      IgnoreHit();");
				SS("    }");
			}
#endif

			if (cc.opt_noise) {
				SS("    uint seed = initRand(DispatchRaysIndex().x + DispatchRaysIndex().y * DispatchRaysDimensions().x, frameCount, 16);");
				SS("    resultAlpha *= round(nextRand(seed));");
			}

			SS("    payload.shadowHit = max(payload.shadowHit - resultAlpha, 0.0f);");
			SS("    if (payload.shadowHit > 0.0f) {");
			SS("		IgnoreHit();");
			SS("    }");
		}
		else {
			SS("payload.shadowHit = 0.0f;");
		}
		SS("}");
		SS("[shader(\"closesthit\")]");
		SS("void " + closestHitName + "(inout ShadowHitInfo payload : SV_RayPayload, in Attributes attrib) { }");

		// Compile shader.
		std::string shaderCode = ss.str();
		shadowHitGroup.id = device->getHitGroupCount() + 1;
		compileShaderCode(shaderCode, VK_SHADER_STAGE_ANY_HIT_BIT_KHR, "", L"lib_6_3", shadowHitGroup.shaderInfo, shadowHitGroup.shaderModule);
		shadowHitGroup.hitGroupName = hitGroupName;
		shadowHitGroup.closestHitName = closestHitName;
		shadowHitGroup.anyHitName = anyHitName;
		shadowHitGroup.shaderInfo.pName = shadowHitGroup.anyHitName.c_str();
	}

	// Creates the descriptor set layout for the raster instance
	void Shader::generateRasterDescriptorSetLayout(Filter filter, bool useGParams, AddressingMode hAddr, AddressingMode vAddr, uint32_t samplerRegisterIndex, VkDescriptorSetLayout& descriptorSetLayout, VkDescriptorSet& descriptorSet) {
		VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

        std::vector<VkDescriptorSetLayoutBinding> bindings;
        std::vector<VkDescriptorPoolSize> poolSizes{};
		if (useGParams) {
			bindings.push_back({CBV_INDEX(gParams) + CBV_SHIFT, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr});
		}
		bindings.push_back({SRV_INDEX(instanceTransforms) + SRV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr});
		bindings.push_back({SRV_INDEX(instanceMaterials) + SRV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
		bindings.push_back({SRV_INDEX(gTextures) + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, SRV_TEXTURES_MAX, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
		bindings.push_back({samplerRegisterIndex + SAMPLER_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
		
		device->generateDescriptorSetLayout(bindings, flags, descriptorSetLayout);
	}
	
	void Shader::compileShaderCode(const std::string& shaderCode, VkShaderStageFlagBits stage, const std::string& entryName, const std::wstring& profile, VkPipelineShaderStageCreateInfo& shaderStage, VkShaderModule& shaderModule) {
#ifndef NDEBUG
		fprintf(stdout, "Compiling...\n\n%s\n", shaderCode.c_str());
		printf("\n____________________________________________\n");
#endif
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> stringConverter;		// Because DXC required wstrings for some reason
		IDxcBlob* dxcBlob = nullptr;
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
		arguments.push_back(L"-fspv-target-env=vulkan1.3");	
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

		IDxcOperationResult* result = nullptr;
		D3D12_CHECK(device->getDxcCompiler()->Compile(textBlob, L"", stringConverter.from_bytes(entryName).c_str(), profile.c_str(), arguments.data(), (UINT32)(arguments.size()), nullptr, 0, nullptr, &result));

		HRESULT resultCode;
		D3D12_CHECK(result->GetStatus(&resultCode));
		if (FAILED(resultCode)) {
			IDxcBlobEncoding* error;
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

	// Public
	Shader::RasterGroup& RT64::Shader::getRasterGroup() { return rasterGroup; }
	bool Shader::hasRasterGroup() const { return rasterGroupInit; }
	Shader::HitGroup Shader::getSurfaceHitGroup() { return surfaceHitGroup; }
	Shader::HitGroup Shader::getShadowHitGroup() { return shadowHitGroup; }
	bool Shader::hasHitGroups() const { return hitGroupInit; }
	uint32_t Shader::hitGroupCount() const { return (surfaceHitGroup.shaderModule != VK_NULL_HANDLE) + (shadowHitGroup.shaderModule != VK_NULL_HANDLE); };
	uint32_t Shader::getFlags() const { return flags; }
	bool Shader::has3DRaster() const { return flags & RT64_SHADER_RASTER_TRANSFORMS_ENABLED; }
	unsigned int Shader::getSamplerRegisterIndex() const { return samplerRegisterIndex; }

	void Shader::allocateRasterDescriptorSet() {
		device->allocateDescriptorSet(rasterGroup.descriptorSetLayout, rasterGroup.descriptorSet);
	}
};

// Library exports

DLEXPORT RT64_SHADER* RT64_CreateShader(RT64_DEVICE* devicePtr, unsigned int shaderId, unsigned int filter, unsigned int hAddr, unsigned int vAddr, int flags) {
    try {
        RT64::Device* device = (RT64::Device*)(devicePtr);
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