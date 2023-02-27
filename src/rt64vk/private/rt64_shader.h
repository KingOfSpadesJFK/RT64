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
                VkShaderModule vertexModule = VK_NULL_HANDLE;
                VkShaderModule fragmentModule = VK_NULL_HANDLE;
                VkPipeline pipeline = VK_NULL_HANDLE;
                VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
                VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
                VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
                std::string vertexShaderName;
                std::string pixelShaderName;
            };

            struct HitGroup {
                uint32_t id = 0;
                VkPipelineShaderStageCreateInfo shaderInfo {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                VkShaderModule shaderModule = VK_NULL_HANDLE;
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
            bool hitGroupInit = false;
            bool rasterGroupInit = false;
            unsigned int descriptorSetIndex = 0;
            unsigned int samplerRegisterIndex = 0;
            VkDescriptorSet rtDescriptorSet = VK_NULL_HANDLE;
            VkSampler sampler = VK_NULL_HANDLE;
            
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
            void generateRasterDescriptorSetLayout(Filter filter, bool useGParams, AddressingMode hAddr, AddressingMode vAddr, uint32_t samplerRegisterIndex, VkDescriptorSetLayout& descriptorSetLayout, VkDescriptorSet& descriptorSet);
            void generateHitDescriptorSetLayout(Filter filter, AddressingMode hAddr, AddressingMode vAddr, uint32_t samplerRegisterIndex, bool hitBuffers, VkDescriptorSetLayout& descriptorSetLayout, VkDescriptorSet& descriptorSet);
            void compileShaderCode(const std::string& shaderCode, VkShaderStageFlagBits stage, const std::string& entryName, const std::wstring& profile, VkPipelineShaderStageCreateInfo& shaderStage, VkShaderModule& shaderModule, uint32_t setIndex);
        public:
            Shader(Device* device, unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr, int flags);
            ~Shader();
            RasterGroup& getRasterGroup();
            HitGroup getSurfaceHitGroup();
            HitGroup getShadowHitGroup();
            VkDescriptorSet& getRTDescriptorSet();
            VkSampler& getSampler();
            uint32_t getFlags() const;
            bool has3DRaster() const;
            bool hasRasterGroup() const;
            bool hasHitGroups() const;
            uint32_t hitGroupCount() const;
            bool hasRTDescriptorSet() const;
            unsigned int getSamplerRegisterIndex() const;
            void allocateRasterDescriptorSet();
            void allocateRTDescriptorSet();
        };
};