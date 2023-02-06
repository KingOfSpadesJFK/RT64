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
                uint32_t id = 0;
                VkPipelineShaderStageCreateInfo vertexInfo {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                VkPipelineShaderStageCreateInfo fragmentInfo {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                VkShaderModule vertexModule {};
                VkShaderModule fragmentModule {};
                VkPipeline pipeline;
                VkPipelineLayout pipelineLayout;
                VkDescriptorSetLayout descriptorSetLayout;
                VkDescriptorPool descriptorPool;
                VkDescriptorSet descriptorSet;
                std::string vertexShaderName;
                std::string pixelShaderName;
            };

            struct HitGroup {
                uint32_t id = 0;
                VkPipelineShaderStageCreateInfo shaderInfo {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                VkShaderModule shaderModule {};
                std::string hitGroupName;
                std::string closestHitName;
                std::string anyHitName;
            };

        private:
            Device* device;
            RasterGroup rasterGroup {};
            HitGroup surfaceHitGroup {};
            HitGroup shadowHitGroup {};
            uint32_t flags;
            bool descriptorBound = false;
            unsigned int samplerRegisterIndex = 0;
            VkDescriptorSetLayout rtDescriptorSetLayout {};
            VkDescriptorPool rtDescriptorPool {};
            VkDescriptorSet rtDescriptorSet {};
            unsigned int descriptorSetIndex = 0;
            
            unsigned int uniqueSamplerRegisterIndex(Filter filter, AddressingMode hAddr, AddressingMode vAddr);
            void generateRasterGroup(unsigned int shaderId, 
                Filter filter, 
                bool use3DTransforms,
                AddressingMode hAddr, 
                AddressingMode vAddr, 
                const std::string &vertexShaderName, 
                const std::string &pixelShaderName
            );
            void generateSurfaceHitGroup(unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr, bool normalMapEnabled, bool specularMapEnabled, const std::string& hitGroupName, const std::string& closestHitName, const std::string& anyHitName);
            void generateShadowHitGroup(unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr, const std::string& hitGroupName, const std::string& closestHitName, const std::string& anyHitName);
            // void fillSamplerDesc(D3D12_STATIC_SAMPLER_DESC &desc, Filter filter, AddressingMode hAddr, AddressingMode vAddr, unsigned int samplerRegisterIndex);
            // ID3D12RootSignature *generateRasterRootSignature(Filter filter, AddressingMode hAddr, AddressingMode vAddr, unsigned int samplerRegisterIndex);
            // ID3D12RootSignature *generateHitRootSignature(Filter filter, AddressingMode hAddr, AddressingMode vAddr, unsigned int samplerRegisterIndex, bool hitBuffers);
            void generateRasterDescriptorSetLayout(Filter filter, AddressingMode hAddr, AddressingMode vAddr, uint32_t samplerRegisterIndex, VkDescriptorSetLayout& descriptorSetLayout, VkDescriptorPool& descriptorPool, VkDescriptorSet& descriptorSet);
            void generateHitDescriptorSetLayout(Filter filter, AddressingMode hAddr, AddressingMode vAddr, uint32_t samplerRegisterIndex, bool hitBuffers, VkDescriptorSetLayout& descriptorSetLayout, VkDescriptorPool& descriptorPool, VkDescriptorSet& descriptorSet);
            void compileShaderCode(const std::string& shaderCode, VkShaderStageFlagBits stage, const std::string& entryName, const std::wstring& profile, VkPipelineShaderStageCreateInfo& shaderStage, VkShaderModule& shaderModule, uint32_t setIndex);
        public:
            Shader(Device* device, unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr, int flags);
            ~Shader();
            void updateDescriptorSet(VkWriteDescriptorSet* data, VkDeviceSize size);
            const RasterGroup& getRasterGroup() const;
            HitGroup getSurfaceHitGroup();
            HitGroup getShadowHitGroup();
            VkDescriptorSetLayout& getRTDescriptorSetLayout();
            VkDescriptorPool& getRTDescriptorPool();
            VkDescriptorSet& getRTDescriptorSet();
            uint32_t getFlags() const;
            bool hasRasterGroup() const;
            bool hasHitGroups() const;
            uint32_t  hitGroupCount() const;
            bool isDescriptorBound() const;
            unsigned int getSamplerRegisterIndex() const;
        };
};