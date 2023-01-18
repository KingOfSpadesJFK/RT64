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
                VkShaderModule* blobVS = nullptr;
                VkShaderModule* blobPS = nullptr;
                VkPipeline* pipeline = nullptr;
                VkPipelineLayout* rasterPipelinLayout = nullptr;
                VkDescriptorSetLayout* descriptorSetLayout = nullptr;
                VkDescriptorSet* descriptorSet = nullptr;
                std::wstring vertexShaderName;
                std::wstring pixelShaderName;
            };

            struct HitGroup {
                void* id = nullptr;
                VkShaderModule* blob = nullptr;
                VkPipelineLayout* rasterPipelinLayout = nullptr;
                VkDescriptorSetLayout* descriptorSetLayout = nullptr;
                VkDescriptorSet* descriptorSet = nullptr;
                std::wstring hitGroupName;
                std::wstring closestHitName;
                std::wstring anyHitName;
            };

        private:
            Device* device;
            RasterGroup rasterGroup;
            HitGroup surfaceHitGroup;
            HitGroup shadowHitGroup;

            unsigned int uniqueSamplerRegisterIndex(Filter filter, AddressingMode hAddr, AddressingMode vAddr);
            void generateRasterGroup(unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr, const std::string &vertexShaderName, const std::string &pixelShaderName);
            // void generateSurfaceHitGroup(unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr, bool normalMapEnabled, bool specularMapEnabled, const std::string &hitGroupName, const std::string &closestHitName, const std::string &anyHitName);
            // void generateShadowHitGroup(unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr, const std::string &hitGroupName, const std::string &closestHitName, const std::string &anyHitName);
            // void fillSamplerDesc(D3D12_STATIC_SAMPLER_DESC &desc, Filter filter, AddressingMode hAddr, AddressingMode vAddr, unsigned int samplerRegisterIndex);
            // ID3D12RootSignature *generateRasterRootSignature(Filter filter, AddressingMode hAddr, AddressingMode vAddr, unsigned int samplerRegisterIndex);
            // ID3D12RootSignature *generateHitRootSignature(Filter filter, AddressingMode hAddr, AddressingMode vAddr, unsigned int samplerRegisterIndex, bool hitBuffers);
            void compileShaderCode(const std::string& shaderCode, const std::wstring& entryName, const std::wstring& profile, VkShaderModule** shaderBlob);
        public:
            Shader(Device *device, unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr, int flags);
            ~Shader();
            const RasterGroup &getRasterGroup() const;
            HitGroup &getSurfaceHitGroup();
            HitGroup &getShadowHitGroup();
            bool hasRasterGroup() const;
            bool hasHitGroups() const;
        };
};