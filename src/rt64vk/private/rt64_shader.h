/*
*   RT64VK
*/

#pragma once

#include "rt64_common.h"

namespace RT64 {
	class Device;

	class Shader {
	    public:
            enum class Filter : int {
                Point,
                Linear
            };

            enum class AddressingMode : int {
                Wrap,
                Mirror,
                Clamp
            };

            struct RasterGroup {
                VkShaderModule *blobVS = nullptr;
                VkShaderModule *blobPS = nullptr;
                VkPipeline *pipeline = nullptr;
                VkPipelineLayout *pipelineLayout = nullptr;
                std::wstring vertexShaderName;
                std::wstring pixelShaderName;
            };

            struct HitGroup {
                void *id = nullptr;
                VkShaderModule *blob = nullptr;
                VkPipelineLayout *pipelineLayout = nullptr;
                std::wstring hitGroupName;
                std::wstring closestHitName;
                std::wstring anyHitName;
            };
    };
};