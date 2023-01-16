//
// RT64
//
#ifndef RT64_MINIMAL

#include "rt64_shader.h"

#include "rt64_device.h"
#include "rt64_shader_hlsli.h"

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

namespace RT64 
{
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
		int vertexSize = 0;
		int positionOffset = 0;
		int normalOffset = 0;
		int uvOffset = 0;
		int inputOffset[4] = { 0,0,0,0 };

		VertexLayout(bool vertexPosition, bool vertexNormal, bool vertexUV, int inputCount, bool useAlpha) {
			positionOffset = vertexSize; if (vertexPosition) vertexSize += 16;
			normalOffset = vertexSize; if (vertexNormal) vertexSize += 12;
			uvOffset = vertexSize; if (vertexUV) vertexSize += 8;
			for (int i = 0; i < inputCount; i++) {
				inputOffset[i] = vertexSize;
				vertexSize += useAlpha ? 16 : 12;
			}
		}
	};

	Shader::Shader(Device *device, unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr, int flags) {
		assert(device != nullptr);
		this->device = device;

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
			generateRasterGroup(shaderId, filter, hAddr, vAddr, vertexShader, pixelShader);
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
		}

		device->addShader(this);
	}

	Shader::~Shader() {
		device->removeShader(this);

		vkDestroyShaderModule(device->getVkDevice(), *rasterGroup.blobVS, nullptr);
		vkDestroyShaderModule(device->getVkDevice(), *rasterGroup.blobPS, nullptr);
		vkDestroyPipeline(device->getVkDevice(), *rasterGroup.pipeline, nullptr);
		vkDestroyPipelineLayout(device->getVkDevice(), *rasterGroup.rasterPipelinLayout, nullptr);
		vkDestroyDescriptorSetLayout(device->getVkDevice(), *rasterGroup.descriptorSetLayout, nullptr);

		vkDestroyShaderModule(device->getVkDevice(), *surfaceHitGroup.blob, nullptr);
		vkDestroyPipelineLayout(device->getVkDevice(), *surfaceHitGroup.rasterPipelinLayout, nullptr);
		vkDestroyDescriptorSetLayout(device->getVkDevice(), *surfaceHitGroup.descriptorSetLayout, nullptr);
		vkDestroyShaderModule(device->getVkDevice(), *shadowHitGroup.blob, nullptr);
		vkDestroyPipelineLayout(device->getVkDevice(), *shadowHitGroup.rasterPipelinLayout, nullptr);
		vkDestroyDescriptorSetLayout(device->getVkDevice(), *shadowHitGroup.descriptorSetLayout, nullptr);
	}

	#define SS(x) ss << x << std::endl;

	void Shader::generateRasterGroup(unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr, const std::string &vertexShaderName, const std::string &pixelShaderName) {
		ColorCombinerParams cc(shaderId);
		bool vertexUV = cc.useTextures[0] || cc.useTextures[1];
		VertexLayout vl(true, true, vertexUV, cc.inputCount, cc.opt_alpha);

		std::stringstream ss;
		SS(INCLUDE_HLSLI(MaterialsHLSLI));
		SS(INCLUDE_HLSLI(InstancesHLSLI));
		SS("int instanceId : register(b0);");

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
		SS("    oPosition = iPosition;");
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
			SS("    int diffuseTexIndex = instanceMaterials[instanceId].diffuseTexIndex;");
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

		std::string shaderCode = ss.str();
		rasterGroup.pixelShaderName = pixelShaderName;
		rasterGroup.vertexShaderName = vertexShaderName;
		compileShaderCode(shaderCode, rasterGroup.pixelShaderName, L"ps_6_3", &rasterGroup.blobPS);
		compileShaderCode(shaderCode, rasterGroup.vertexShaderName, L"vs_6_3", &rasterGroup.blobVS);
		// rasterGroup.rootSignature = generateRasterRootSignature(filter, hAddr, vAddr, samplerRegisterIndex);

		// Define the vertex layout.
		// std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDescs;
		// inputElementDescs.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, (UINT)(vl.positionOffset), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
		// inputElementDescs.push_back({ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, (UINT)(vl.normalOffset), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
		// if (vertexUV) {
		// 	inputElementDescs.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, (UINT)(vl.uvOffset), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
		// }
		// for (int i = 0; i < cc.inputCount; i++) {
		// 	inputElementDescs.push_back({ "COLOR", (UINT)(i), cc.opt_alpha ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R32G32B32_FLOAT, 0, (UINT)(vl.inputOffset[i]), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
		// }

		// Blend state.
		// const D3D12_RENDER_TARGET_BLEND_DESC alphaBlendDesc = {
		// 	TRUE, FALSE,
		// 	D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD,
		// 	D3D12_BLEND_ONE, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD,
		// 	D3D12_LOGIC_OP_NOOP,
		// 	D3D12_COLOR_WRITE_ENABLE_ALL
		// };

		// D3D12_BLEND_DESC bd = {};
		// bd.AlphaToCoverageEnable = FALSE;
		// bd.IndependentBlendEnable = FALSE;
		// for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) {
		// 	bd.RenderTarget[i] = alphaBlendDesc;
		// }

		// Describe and create the graphics pipeline state object (PSO).
		// D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		// psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		// psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		// psoDesc.BlendState = bd;
		// psoDesc.DepthStencilState.DepthEnable = FALSE;
		// psoDesc.DepthStencilState.StencilEnable = FALSE;
		// psoDesc.SampleMask = UINT_MAX;
		// psoDesc.NumRenderTargets = 1;
		// psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		// psoDesc.SampleDesc.Count = 1;
		// psoDesc.InputLayout = { inputElementDescs.data(), (UINT)(inputElementDescs.size()) };
		// psoDesc.pRootSignature = rasterGroup.rootSignature;
		// psoDesc.VS.BytecodeLength = rasterGroup.blobVS->GetBufferSize();
		// psoDesc.VS.pShaderBytecode = rasterGroup.blobVS->GetBufferPointer();
		// psoDesc.PS.BytecodeLength = rasterGroup.blobPS->GetBufferSize();
		// psoDesc.PS.pShaderBytecode = rasterGroup.blobPS->GetBufferPointer();
		// psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		// D3D12_CHECK(device->getD3D12Device()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&rasterGroup.pipelineState)));
	}

	void Shader::compileShaderCode(const std::string &shaderCode, const std::wstring &entryName, const std::wstring &profile, VkShaderModule** shaderBlob) {
#ifndef NDEBUG
		fprintf(stdout, "Compiling...\n\n%s\n", shaderCode.c_str());
#endif

		IDxcBlobEncoding* textBlob = nullptr;
		D3D12_CHECK(device->getDxcLibrary()->CreateBlobWithEncodingFromPinned((LPBYTE)shaderCode.c_str(), (uint32_t)shaderCode.size(), 0, &textBlob));

		std::vector<LPCWSTR> arguments;
		arguments.push_back(L"-spirv");
		arguments.push_back(L"-fspv-target-env=vulkan1.2");	
		arguments.push_back(L"-Qstrip_debug");
		arguments.push_back(L"-Qstrip_reflect");

		IDxcOperationResult *result = nullptr;
		D3D12_CHECK(device->getDxcCompiler()->Compile(textBlob, L"", entryName.c_str(), profile.c_str(), arguments.data(), (UINT32)(arguments.size()), nullptr, 0, nullptr, &result));

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

		D3D12_CHECK(result->GetResult(shaderBlob));
	}
};

// Public

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