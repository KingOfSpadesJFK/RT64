/*
*   RT64VK
*/

#include <cassert>

#include "rt64_device.h"
#include <cstring>
#include <iostream>
#include <set>
#include <unordered_set>
#include <map>
#include <cstdint>
#include <limits>
#include <algorithm>

#include <stb_image.h>

// ##############################################
// SPIR-V blobs

// Raygen shaders
#include "shaders/PrimaryRayGen.hlsl.h"
#include "shaders/DirectRayGen.hlsl.h"
#include "shaders/IndirectRayGen.hlsl.h"
#include "shaders/ReflectionRayGen.hlsl.h"
#include "shaders/RefractionRayGen.hlsl.h"

// Vertex shaders
#include "shaders/FullScreenVS.hlsl.h"
#include "shaders/Im3DVS.hlsl.h"

// Pixel shaders
#include "shaders/ComposePS.hlsl.h"
#include "shaders/TonemappingPS.hlsl.h"
#include "shaders/PostProcessPS.hlsl.h"
#include "shaders/DebugPS.hlsl.h"
#include "shaders/Im3DPS.hlsl.h"

// Compute shaders
#include "shaders/GaussianFilterRGB3x3CS.hlsl.h"

// Geometry shaders
#include "shaders/Im3DGSLines.hlsl.h"
#include "shaders/Im3DGSPoints.hlsl.h"

// The blue noise
#include "res/bluenoise/LDR_64_64_64_RGB1.h"

namespace RT64
{

    Device::Device(RT64_WINDOW inWindow) {
	    RT64_LOG_OPEN("rt64.log");

#ifndef RT64_MINIMAL
        window = inWindow;
    #ifndef _WIN32
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    #endif
#endif

        createVkInstanceNV();
        // createVKInstance();
        // setupDebugMessenger();
#ifndef RT64_MINIMAL
        createSurface();
#endif
        // pickPhysicalDevice();
        // createLogicalDevice();

#ifndef RT64_MINIMAL
        createMemoryAllocator();
        createSwapChain();
        updateViewport();
        createImageViews();
        createRenderPass(presentRenderPass, false, swapChainImageFormat, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        createCommandPool();

        createCommandBuffers();
        createSyncObjects();

        createDxcCompiler();

        generateSamplers();
        loadAssets();

        initRayTracing();
        createDescriptorPool();
        createRayTracingPipeline();

        inspector.init(this);
        fencesUp.fill(false);
#endif
    }

    void Device::createVkInstanceNV() {
        nvvk::ContextCreateInfo contextInfo;
        contextInfo.setVersion(1, 3);               

        // Vulkan required extensions
#ifndef _WIN32
        assert(glfwVulkanSupported() == 1);
        uint32_t count{0};
        auto     reqExtensions = glfwGetRequiredInstanceExtensions(&count);

        // Requesting Vulkan extensions and layers        // Using Vulkan 1.3
        for(uint32_t ext_id = 0; ext_id < count; ext_id++)  // Adding required extensions (surface, win32, linux, ..)
            contextInfo.addInstanceExtension(reqExtensions[ext_id]);
#else
        contextInfo.addInstanceExtension(VK_KHR_SURFACE_EXTENSION_NAME);              // Extension for surfaces
        contextInfo.addInstanceExtension(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);        // Extension for win32 surfaces

#endif
        contextInfo.addInstanceLayer("VK_LAYER_LUNARG_monitor", true);              // FPS in titlebar
        contextInfo.addInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, true);  // Allow debug names
        contextInfo.addDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);            // Enabling ability to present rendering

        // #VKRay: Activate the ray tracing extension
        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
        contextInfo.addDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, false, &accelFeature);  // To build acceleration structures
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
        contextInfo.addDeviceExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, false, &rtPipelineFeature);  // To use vkCmdTraceRaysKHR
        contextInfo.addDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);  // Required by ray tracing pipeline
        contextInfo.addDeviceExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME);  // Required by ray tracing pipeline
        contextInfo.addDeviceExtension(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);
        contextInfo.addDeviceExtension(VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME);
        contextInfo.addDeviceExtension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
        contextInfo.addDeviceExtension(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);

        // Creating Vulkan base application
        vkctx.initInstance(contextInfo);
        // Find all compatible devices
        auto compatibleDevices = vkctx.getCompatibleDevices(contextInfo);
        assert(!compatibleDevices.empty());
        // Use a compatible device
        vkctx.initDevice(compatibleDevices[0], contextInfo);
    }

    void Device::generateSamplers() {
        RT64_LOG_PRINTF("Creating the samplers...");
        float samplerAnisotropy = std::min(anisotropy, physDeviceProperties.limits.maxSamplerAnisotropy);
        VkSamplerCreateInfo samplerInfo { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        samplerInfo.anisotropyEnable = samplerAnisotropy > 1.0f ? VK_TRUE : VK_FALSE;
        samplerInfo.maxAnisotropy = samplerAnisotropy;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = FLT_MAX;

        for (int filter = 0; filter < 2; filter++) {
            samplerInfo.magFilter = filter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
            samplerInfo.minFilter = filter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
            for (int hAddr = 0; hAddr < 3; hAddr++) {
                for (int vAddr = 0; vAddr < 3; vAddr++) {
                    samplerInfo.addressModeU = (VkSamplerAddressMode)hAddr;
                    samplerInfo.addressModeV = (VkSamplerAddressMode)vAddr;
                    VkSampler sampler;
                    vkCreateSampler(vkDevice, &samplerInfo, nullptr, &sampler);
                    samplers.emplace(uniqueSamplerRegisterIndex(filter, hAddr, vAddr), sampler);
                }
            }
        }
        RT64_LOG_PRINTF("Sampler creation finished!");
    }

    // Loads the appropriate assets for rendering, such as the 
    //  the statically loaded shader modules and descriptor set
    //  layouts and blue noise
    void Device::loadAssets() {
	    RT64_LOG_PRINTF("Asset loading started");
        vkGetPhysicalDeviceProperties(physicalDevice, &physDeviceProperties);

        // Create the off-screen render pass
        RT64_LOG_PRINTF("Creating offscreen render pass");
        createRenderPass(offscreenRenderPass, false, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();
        
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // Viewport state
        VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        // Rasterizer
        VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        // Color blending
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
        colorBlending.blendConstants[0] = 0.0f;
        colorBlending.blendConstants[1] = 0.0f;
        colorBlending.blendConstants[2] = 0.0f;
        colorBlending.blendConstants[3] = 0.0f;

		// Depth stencil
        VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
        // depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.stencilTestEnable = VK_FALSE;

        // Bind the vertex inputs
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vertexInputInfo.vertexBindingDescriptionCount = 0;
        vertexInputInfo.vertexAttributeDescriptionCount = 0;

        // Pipeline info
        VkGraphicsPipelineCreateInfo pipelineInfo { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.subpass = 0;

        RT64_LOG_PRINTF("Creating a generic image sampler for the fragment/compute shaders");
        {
            VkSamplerCreateInfo samplerInfo { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.anisotropyEnable = VK_FALSE;
            samplerInfo.maxAnisotropy = 1.0f;
            samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
            samplerInfo.unnormalizedCoordinates = VK_FALSE;
            samplerInfo.compareEnable = VK_FALSE;
            samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.mipLodBias = 0.0f;
            samplerInfo.minLod = 0.0f;
            samplerInfo.maxLod = std::numeric_limits<float>::max();
            vkCreateSampler(vkDevice, &samplerInfo, nullptr, &composeSampler);
            vkCreateSampler(vkDevice, &samplerInfo, nullptr, &tonemappingSampler);
            vkCreateSampler(vkDevice, &samplerInfo, nullptr, &postProcessSampler);
            vkCreateSampler(vkDevice, &samplerInfo, nullptr, &gaussianSampler);
        }

        RT64_LOG_PRINTF("Creating the raygen/miss modules"); 
        {
            createShaderModule(PrimaryRayGen_SPIRV,     sizeof(PrimaryRayGen_SPIRV),    "PrimaryRayGen",    VK_SHADER_STAGE_RAYGEN_BIT_KHR, primaryRayGenStage,         primaryRayGenModule, nullptr);
            createShaderModule(DirectRayGen_SPIRV,      sizeof(DirectRayGen_SPIRV),     "DirectRayGen",     VK_SHADER_STAGE_RAYGEN_BIT_KHR, directRayGenStage,          directRayGenModule, nullptr);
            createShaderModule(IndirectRayGen_SPIRV,    sizeof(IndirectRayGen_SPIRV),   "IndirectRayGen",   VK_SHADER_STAGE_RAYGEN_BIT_KHR, indirectRayGenStage,        indirectRayGenModule, nullptr);
            createShaderModule(ReflectionRayGen_SPIRV,  sizeof(ReflectionRayGen_SPIRV), "ReflectionRayGen", VK_SHADER_STAGE_RAYGEN_BIT_KHR, reflectionRayGenStage,      reflectionRayGenModule, nullptr);
            createShaderModule(RefractionRayGen_SPIRV,  sizeof(RefractionRayGen_SPIRV), "RefractionRayGen", VK_SHADER_STAGE_RAYGEN_BIT_KHR, refractionRayGenStage,      refractionRayGenModule, nullptr);
            createShaderModule(PrimaryRayGen_SPIRV,     sizeof(PrimaryRayGen_SPIRV),    "SurfaceMiss",      VK_SHADER_STAGE_MISS_BIT_KHR, surfaceMissStage,             surfaceMissModule, nullptr);
            createShaderModule(PrimaryRayGen_SPIRV,     sizeof(PrimaryRayGen_SPIRV),    "ShadowMiss",       VK_SHADER_STAGE_MISS_BIT_KHR, shadowMissStage,              shadowMissModule, nullptr);
            generateRTDescriptorSetLayout();
        }

	    RT64_LOG_PRINTF("Creating the composition descriptor set layout");
        {
		    VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
            std::vector<VkDescriptorSetLayoutBinding> bindings {
                {0 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {1 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {2 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {3 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {4 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {5 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {6 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {0 + SAMPLER_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {0 + CBV_SHIFT, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            };
            generateDescriptorSetLayout(bindings, flags, composeDescriptorSetLayout);
        }

        RT64_LOG_PRINTF("Creating the composition pipeline");
        {
            // Create the shader modules and shader stages
            std::vector<VkPipelineShaderStageCreateInfo> composeStages;
            createShaderModule(FullScreenVS_SPIRV, sizeof(FullScreenVS_SPIRV), VS_ENTRY, VK_SHADER_STAGE_VERTEX_BIT, fullscreenVSStage, fullscreenVSModule, &composeStages);
            createShaderModule(ComposePS_SPIRV, sizeof(ComposePS_SPIRV), PS_ENTRY, VK_SHADER_STAGE_FRAGMENT_BIT, composePSStage, composePSModule, &composeStages);

		    // Create the pipeline layout
            VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            pipelineLayoutInfo.setLayoutCount = 1;
            pipelineLayoutInfo.pSetLayouts = &composeDescriptorSetLayout;
            VK_CHECK(vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr, &composePipelineLayout));

            // Create the pipeline
            pipelineInfo.pVertexInputState = &vertexInputInfo;
            pipelineInfo.stageCount = composeStages.size();
            pipelineInfo.pStages = composeStages.data();
            pipelineInfo.layout = composePipelineLayout;
            pipelineInfo.renderPass = offscreenRenderPass;
            VK_CHECK(vkCreateGraphicsPipelines(vkDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &composePipeline));
        }

        RT64_LOG_PRINTF("Creating the post processing descriptor set layout");
        {
		    VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
            std::vector<VkDescriptorSetLayoutBinding> bindings {
                {0 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {1 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {0 + SAMPLER_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {CBV_INDEX(gParams) + CBV_SHIFT, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            };
            generateDescriptorSetLayout(bindings, flags, postProcessDescriptorSetLayout);
        }

        RT64_LOG_PRINTF("Creating the post process pipeline");
        {
            // Create the shader modules and shader stages
            std::vector<VkPipelineShaderStageCreateInfo> postStages;
            postStages.push_back(fullscreenVSStage);
            createShaderModule(PostProcessPS_SPIRV, sizeof(PostProcessPS_SPIRV), PS_ENTRY, VK_SHADER_STAGE_FRAGMENT_BIT, postProcessPSStage, postProcessPSModule, &postStages);

		    // Create the pipeline layout
            VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            pipelineLayoutInfo.setLayoutCount = 1;
            pipelineLayoutInfo.pSetLayouts = &postProcessDescriptorSetLayout;
            VK_CHECK(vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr, &postProcessPipelineLayout));

            // Create the pipeline
            pipelineInfo.pVertexInputState = &vertexInputInfo;
            pipelineInfo.stageCount = postStages.size();
            pipelineInfo.pStages = postStages.data();
            pipelineInfo.layout = postProcessPipelineLayout;
            pipelineInfo.renderPass = presentRenderPass;
            VK_CHECK(vkCreateGraphicsPipelines(vkDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &postProcessPipeline));
        }

        RT64_LOG_PRINTF("Creating the color correction descriptor set layout");
        {
		    VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
            std::vector<VkDescriptorSetLayoutBinding> bindings {
                {0 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {0 + SAMPLER_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {CBV_INDEX(gParams) + CBV_SHIFT, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            };
            generateDescriptorSetLayout(bindings, flags, tonemappingDescriptorSetLayout);
        }

        RT64_LOG_PRINTF("Creating the color correction pipeline");
        {
            // Create the shader modules and shader stages
            std::vector<VkPipelineShaderStageCreateInfo> colorStages;
            colorStages.push_back(fullscreenVSStage);
            createShaderModule(TonemappingPS_SPIRV, sizeof(TonemappingPS_SPIRV), PS_ENTRY, VK_SHADER_STAGE_FRAGMENT_BIT, tonemappingPSStage, tonemappingPSModule, &colorStages);

		    // Create the pipeline layout
            VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            pipelineLayoutInfo.setLayoutCount = 1;
            pipelineLayoutInfo.pSetLayouts = &tonemappingDescriptorSetLayout;
            VK_CHECK(vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr, &tonemappingPipelineLayout));

            // Create the pipeline
            pipelineInfo.pVertexInputState = &vertexInputInfo;
            pipelineInfo.stageCount = colorStages.size();
            pipelineInfo.pStages = colorStages.data();
            pipelineInfo.layout = tonemappingPipelineLayout;
            pipelineInfo.renderPass = offscreenRenderPass;
            VK_CHECK(vkCreateGraphicsPipelines(vkDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &tonemappingPipeline));
        }

        RT64_LOG_PRINTF("Creating the debug descriptor set layout");
        {
		    VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
            std::vector<VkDescriptorSetLayoutBinding> bindings {
                {UAV_SHIFT + UAV_INDEX(gShadingPosition), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {UAV_SHIFT + UAV_INDEX(gShadingNormal), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {UAV_SHIFT + UAV_INDEX(gShadingSpecular), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                {UAV_SHIFT + UAV_INDEX(gDiffuse), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                {UAV_SHIFT + UAV_INDEX(gInstanceId), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {UAV_SHIFT + UAV_INDEX(gDirectLightAccum), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {UAV_SHIFT + UAV_INDEX(gIndirectLightAccum), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {UAV_SHIFT + UAV_INDEX(gFilteredDirectLight), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {UAV_SHIFT + UAV_INDEX(gFilteredIndirectLight), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {UAV_SHIFT + UAV_INDEX(gReflection), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {UAV_SHIFT + UAV_INDEX(gRefraction), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {UAV_SHIFT + UAV_INDEX(gTransparent), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {UAV_SHIFT + UAV_INDEX(gFlow), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {UAV_SHIFT + UAV_INDEX(gReactiveMask), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {UAV_SHIFT + UAV_INDEX(gLockMask), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {UAV_SHIFT + UAV_INDEX(gDepth), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {CBV_SHIFT + CBV_INDEX(gParams), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
            };
            generateDescriptorSetLayout(bindings, flags, debugDescriptorSetLayout);
        }

        RT64_LOG_PRINTF("Creating the debug pipeline");
        {
            // Create the shader modules and shader stages
            std::vector<VkPipelineShaderStageCreateInfo> debugStages;
            debugStages.push_back(fullscreenVSStage);
            createShaderModule(DebugPS_SPIRV, sizeof(DebugPS_SPIRV), PS_ENTRY, VK_SHADER_STAGE_FRAGMENT_BIT, debugPSStage, debugPSModule, &debugStages);

            // Bind the vertex inputs
            VkPipelineVertexInputStateCreateInfo vertexInputInfo{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
            vertexInputInfo.vertexBindingDescriptionCount = 0;
            vertexInputInfo.vertexAttributeDescriptionCount = 0;

		    // Create the pipeline layout
            VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            pipelineLayoutInfo.setLayoutCount = 1;
            pipelineLayoutInfo.pSetLayouts = &debugDescriptorSetLayout;
            VK_CHECK(vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr, &debugPipelineLayout));

            // Create the pipeline
            pipelineInfo.pVertexInputState = &vertexInputInfo;
            pipelineInfo.stageCount = debugStages.size();
            pipelineInfo.pStages = debugStages.data();
            pipelineInfo.layout = debugPipelineLayout;
            pipelineInfo.renderPass = presentRenderPass;
            VK_CHECK(vkCreateGraphicsPipelines(vkDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &debugPipeline));
        }

        RT64_LOG_PRINTF("Creating the Gaussian blur descriptor set");
        {
		    VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
            VkShaderStageFlags stages = VK_SHADER_STAGE_COMPUTE_BIT;
            std::vector<VkDescriptorSetLayoutBinding> bindings {
                {0 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, stages, nullptr},
                {0 + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, stages, nullptr},
                {0 + CBV_SHIFT, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, stages, nullptr},
                {0 + SAMPLER_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLER, 1, stages, nullptr}
            };
            generateDescriptorSetLayout(bindings, flags, gaussianFilterRGB3x3DescriptorSetLayout);
        }

        RT64_LOG_PRINTF("Creating the Gaussian blur pipeline");
        {
            createShaderModule(GaussianFilterRGB3x3CS_SPIRV, sizeof(GaussianFilterRGB3x3CS_SPIRV), CS_ENTRY, VK_SHADER_STAGE_COMPUTE_BIT, gaussianFilterRGB3x3CSStage, gaussianFilterRGB3x3CSModule, nullptr);

            // Bind the vertex inputs
            VkPipelineVertexInputStateCreateInfo vertexInputInfo{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
            vertexInputInfo.vertexBindingDescriptionCount = 0;
            vertexInputInfo.vertexAttributeDescriptionCount = 0;

		    // Create the pipeline layout
            VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            pipelineLayoutInfo.setLayoutCount = 1;
            pipelineLayoutInfo.pSetLayouts = &gaussianFilterRGB3x3DescriptorSetLayout;
            VK_CHECK(vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr, &gaussianFilterRGB3x3PipelineLayout));

            // Create the pipeline
            VkComputePipelineCreateInfo computePipelineInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
            computePipelineInfo.stage = gaussianFilterRGB3x3CSStage;
            computePipelineInfo.layout = gaussianFilterRGB3x3PipelineLayout;
            VK_CHECK(vkCreateComputePipelines(vkDevice, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &gaussianFilterRGB3x3Pipeline));
        }

        if (!disableMipmaps) {
            mipmaps = new Mipmaps(this);
        }

        RT64_LOG_PRINTF("Creating the Im3d descriptor set");
        {
		    VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
            VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            std::vector<VkDescriptorSetLayoutBinding> bindings {
                {UAV_INDEX(gHitDistAndFlow) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1, stages, nullptr},
                {UAV_INDEX(gHitColor) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1, stages, nullptr},
                {UAV_INDEX(gHitNormal) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1, stages, nullptr},
                {UAV_INDEX(gHitSpecular) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1, stages, nullptr},
                {UAV_INDEX(gHitInstanceId) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1, stages, nullptr},
                {CBV_INDEX(gParams) + CBV_SHIFT, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, stages, nullptr},
            };
            generateDescriptorSetLayout(bindings, flags, im3dDescriptorSetLayout);
        }

        RT64_LOG_PRINTF("Creating the Im3d pipeline");
        {
            // Create the shader modules and shader stages
            std::vector<VkPipelineShaderStageCreateInfo> im3dStages;
            createShaderModule(Im3DVS_SPIRV, sizeof(Im3DVS_SPIRV), VS_ENTRY, VK_SHADER_STAGE_VERTEX_BIT, im3dVSStage, im3dVSModule, &im3dStages);
            createShaderModule(Im3DPS_SPIRV, sizeof(Im3DPS_SPIRV), PS_ENTRY, VK_SHADER_STAGE_FRAGMENT_BIT, im3dPSStage, im3dPSModule, &im3dStages);

            // Define the vertex layout.
            VkVertexInputBindingDescription vertexBind{};
            vertexBind.binding = 0;
            vertexBind.stride = sizeof(float) * 8;
            vertexBind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            // Create the attributes for the vertex inputs
            std::vector<VkVertexInputAttributeDescription> attributes;
    		attributes.push_back({0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0});
	    	attributes.push_back({1, 0, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float)});

            // Bind the vertex inputs
            VkPipelineVertexInputStateCreateInfo im3dVertexInputInfo{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
            im3dVertexInputInfo.vertexBindingDescriptionCount = 1;
            im3dVertexInputInfo.pVertexBindingDescriptions = &vertexBind;
            im3dVertexInputInfo.vertexAttributeDescriptionCount = attributes.size();
            im3dVertexInputInfo.pVertexAttributeDescriptions = attributes.data();

            VkPipelineColorBlendAttachmentState im3dColorBlendAttachment{};
            im3dColorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            im3dColorBlendAttachment.blendEnable = VK_TRUE;
            im3dColorBlendAttachment.colorBlendOp = VK_BLEND_OP_OVERLAY_EXT;
            im3dColorBlendAttachment.alphaBlendOp = VK_BLEND_OP_MAX;
            im3dColorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
            im3dColorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
            im3dColorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            im3dColorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;

            VkPipelineColorBlendStateCreateInfo im3dColorBlending{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
            colorBlending.logicOpEnable = VK_TRUE;
            colorBlending.logicOp = VK_LOGIC_OP_COPY;
            colorBlending.attachmentCount = 1;
            colorBlending.pAttachments = &im3dColorBlendAttachment;
            colorBlending.blendConstants[0] = 0.0f;
            colorBlending.blendConstants[1] = 0.0f;
            colorBlending.blendConstants[2] = 0.0f;
            colorBlending.blendConstants[3] = 0.0f;

            // Just a function for pipeline (layout) creation
            auto createPipeline = [this, &im3dStages, &im3dVertexInputInfo, im3dColorBlending]
            (VkGraphicsPipelineCreateInfo pipelineInfo, VkDescriptorSetLayout& descLayout, VkPipelineLayout& pipelineLayout, VkPipeline& pipeline) {
                // Create the pipeline layout
                VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
                pipelineLayoutInfo.setLayoutCount = 1;
                pipelineLayoutInfo.pSetLayouts = &im3dDescriptorSetLayout;
                VK_CHECK(vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout));

                // Create the pipeline
                pipelineInfo.pVertexInputState = &im3dVertexInputInfo;
                // pipelineInfo.pColorBlendState = &im3dColorBlending;
                pipelineInfo.stageCount = im3dStages.size();
                pipelineInfo.pStages = im3dStages.data();
                pipelineInfo.layout = pipelineLayout;
                VK_CHECK(vkCreateGraphicsPipelines(vkDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline));
            };

            createPipeline(pipelineInfo, im3dDescriptorSetLayout, im3dPipelineLayout, im3dPipeline);
            im3dStages.pop_back();

            // Create the shader module for the geo points shader
            createShaderModule(Im3DGSPoints_SPIRV, sizeof(Im3DGSPoints_SPIRV), GS_ENTRY, VK_SHADER_STAGE_GEOMETRY_BIT, im3dGSPointsStage, im3dGSPointsModule, &im3dStages);
            im3dStages.push_back(im3dPSStage);
            // Create the pipeline (layout) for the points shader
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            createPipeline(pipelineInfo, im3dDescriptorSetLayout, im3dPointsPipelineLayout, im3dPointsPipeline);
            im3dStages.pop_back();
            im3dStages.pop_back();

            // Create the shader module for the geo lines shader
            createShaderModule(Im3DGSLines_SPIRV, sizeof(Im3DGSLines_SPIRV), GS_ENTRY, VK_SHADER_STAGE_GEOMETRY_BIT, im3dGSLinesStage, im3dGSLinesModule, &im3dStages);
            im3dStages.push_back(im3dPSStage);
            // Create the pipeline (layout) for the lines shader
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            createPipeline(pipelineInfo, im3dDescriptorSetLayout, im3dLinesPipelineLayout, im3dLinesPipeline);
            im3dStages.clear();
        }

        loadBlueNoise();
        createFramebuffers();

    }

    // Creates the ray tracing pipeline
    void Device::createRayTracingPipeline() {
	    RT64_LOG_PRINTF("Raytracing pipeline creation started");

        // A vector for the shader stages
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

	    RT64_LOG_PRINTF("Loading the ray generation modules...");

        // Push the main shaders into the shader stages
        shaderStages.push_back(primaryRayGenStage);
        shaderStages.push_back(directRayGenStage);
        shaderStages.push_back(indirectRayGenStage);
        shaderStages.push_back(reflectionRayGenStage);
        shaderStages.push_back(refractionRayGenStage);
        shaderStages.push_back(surfaceMissStage);
        shaderStages.push_back(shadowMissStage);

	    RT64_LOG_PRINTF("Loading the hit modules...");
        // Add the generated hit shaders to the shaderStages vector
        int i = 0;
        for (Shader* s : shaders) {
            if (s != nullptr && s->hasHitGroups()) {
                auto surfaceHitGroup = s->getSurfaceHitGroup();
                shaderStages.push_back(surfaceHitGroup.shaderInfo);
                auto shadowHitGroup = s->getShadowHitGroup();
                shaderStages.push_back(shadowHitGroup.shaderInfo);
                // Set the SBT index of the hit shaders
                s->setSurfaceSBTIndex(i++);
                s->setShadowSBTIndex(i++);
            }
        }

	    RT64_LOG_PRINTF("Grouping the shaders...");

        // Shader groups
        std::vector<VkRayTracingShaderGroupCreateInfoKHR> rtShaderGroups;
        VkRayTracingShaderGroupCreateInfoKHR group
            {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
        group.anyHitShader = VK_SHADER_UNUSED_KHR;
        group.closestHitShader = VK_SHADER_UNUSED_KHR;
        group.generalShader = VK_SHADER_UNUSED_KHR;
        group.intersectionShader = VK_SHADER_UNUSED_KHR;

        // Group the shaders
        group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        group.generalShader = SHADER_INDEX(primaryRayGen);
        rtShaderGroups.push_back(group);
        group.generalShader = SHADER_INDEX(directRayGen);
        rtShaderGroups.push_back(group);
        group.generalShader = SHADER_INDEX(indirectRayGen);
        rtShaderGroups.push_back(group);
        group.generalShader = SHADER_INDEX(reflectionRayGen);
        rtShaderGroups.push_back(group);
        group.generalShader = SHADER_INDEX(refractionRayGen);
        rtShaderGroups.push_back(group);
        group.generalShader = SHADER_INDEX(surfaceMiss);
        rtShaderGroups.push_back(group);
        group.generalShader = SHADER_INDEX(shadowMiss);
        rtShaderGroups.push_back(group);

        group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        group.generalShader = VK_SHADER_UNUSED_KHR;
        for (int i = SHADER_INDEX(MAX); i < shaderStages.size(); i++) {
            group.anyHitShader = i;
            rtShaderGroups.push_back(group);
        }
        
	    RT64_LOG_PRINTF("Gathering the descriptor set layouts...");        
        // Push the main RT descriptor set layout into the layouts vector
        rtDescriptorSetLayouts.clear();
        rtDescriptorSetLayouts.reserve(shaders.size() + 1);
        rtDescriptorSetLayouts.push_back(rtDescriptorSetLayout);
        
	    RT64_LOG_PRINTF("Creating the RT pipeline...");
		// Set up the push constnants
		VkPushConstantRange pushConstant;
		pushConstant.offset = 0;
		pushConstant.size = sizeof(RaygenPushConstant);
		pushConstant.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        // Create the pipeline layout create info
        VkPipelineLayoutCreateInfo layoutInfo {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &rtDescriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstant;
        vkCreatePipelineLayout(vkDevice, &layoutInfo, nullptr, &rtPipelineLayout);

        // Assemble the shader stages and recursion depth info into the ray tracing pipeline
        VkRayTracingPipelineInterfaceCreateInfoKHR interfaceInfo { VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_INTERFACE_CREATE_INFO_KHR };
        interfaceInfo.maxPipelineRayHitAttributeSize = 2 * sizeof(float);
        interfaceInfo.maxPipelineRayPayloadSize = 13 * sizeof(float);
        VkRayTracingPipelineCreateInfoKHR rayPipelineInfo{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
        rayPipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());  // Stages are shaders
        rayPipelineInfo.pStages = shaderStages.data();
        rayPipelineInfo.groupCount = static_cast<uint32_t>(rtShaderGroups.size());
        rayPipelineInfo.pGroups = rtShaderGroups.data();
        rayPipelineInfo.maxPipelineRayRecursionDepth = 1;       // Ray depth
        rayPipelineInfo.layout = rtPipelineLayout;
        rayPipelineInfo.pLibraryInterface = &interfaceInfo;
        vkCreateRayTracingPipelinesKHR(vkDevice, {}, {}, 1, &rayPipelineInfo, nullptr, &rtPipeline);

        rtDescriptorSets.clear();
        rtDescriptorSets.push_back(rtDescriptorSet);

	    RT64_LOG_PRINTF("Raytracing pipeline created!");
    }

    void Device::generateRTDescriptorSetLayout() {
		VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
        VkShaderStageFlags defaultStageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
        std::vector<VkDescriptorSetLayoutBinding> bindings = {
            {UAV_INDEX(gViewDirection) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gShadingPosition) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gShadingNormal) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gShadingSpecular) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gDiffuse) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gInstanceId) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gDirectLightAccum) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gIndirectLightAccum) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gReflection) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gRefraction) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gTransparent) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gFlow) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gReactiveMask) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gLockMask) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gNormal) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gDepth) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gPrevNormal) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gPrevDepth) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gPrevDirectLightAccum) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gPrevIndirectLightAccum) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gFilteredDirectLight) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gFilteredIndirectLight) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gHitDistAndFlow) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gHitColor) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gHitNormal) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gHitSpecular) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1, defaultStageFlags, nullptr},
            {UAV_INDEX(gHitInstanceId) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1, defaultStageFlags, nullptr},

            {SRV_INDEX(gBackground) + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, defaultStageFlags, nullptr},
            {SRV_INDEX(SceneBVH) + SRV_SHIFT, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, defaultStageFlags, nullptr},
            {SRV_INDEX(SceneLights) + SRV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, defaultStageFlags, nullptr},
            {SRV_INDEX(instanceTransforms) + SRV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, defaultStageFlags, nullptr},
            {SRV_INDEX(instanceMaterials) + SRV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, defaultStageFlags, nullptr},
            {SRV_INDEX(gBlueNoise) + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, defaultStageFlags, nullptr},
            {SRV_INDEX(gTextures) + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, SRV_TEXTURES_MAX, defaultStageFlags, nullptr},

            {CBV_INDEX(gParams) + CBV_SHIFT, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, defaultStageFlags, nullptr},
            {0 + SAMPLER_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLER, 1, defaultStageFlags, nullptr}
        };

        for (std::pair<unsigned int, VkSampler> samplerPair : samplers) {
            bindings.push_back({samplerPair.first + SAMPLER_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLER, 1, defaultStageFlags, nullptr});
        }

		generateDescriptorSetLayout(bindings, flags, rtDescriptorSetLayout);
    }

    // Creates the descriptor pool and allocates the descriptor sets to the pool
    void Device::createDescriptorPool() {
	    RT64_LOG_PRINTF("Creating the descriptor pool...");
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = descriptorPoolBindings.size();
        poolInfo.pPoolSizes = descriptorPoolBindings.data();
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        // Change the constant if you add more descriptor sets
        //  Just bellow the RT64_LOG_PRINTF(), count the lines of allocateDescriptorSet() calls
        #define DESCRIPTOR_SETS_IN_DEVICE   6
        #define DESCRIPTOR_SETS_IN_VIEW     2
        poolInfo.maxSets = (shaders.size() * 3) + (scenes.size() * DESCRIPTOR_SETS_IN_VIEW ) + DESCRIPTOR_SETS_IN_DEVICE;      
		VK_CHECK(vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &descriptorPool));

	    RT64_LOG_PRINTF("Allocating the descriptor sets to the pool...");
        allocateDescriptorSet(rtDescriptorSetLayout, rtDescriptorSet);
        allocateDescriptorSet(composeDescriptorSetLayout, composeDescriptorSet);
        allocateDescriptorSet(tonemappingDescriptorSetLayout, tonemappingDescriptorSet);
        allocateDescriptorSet(postProcessDescriptorSetLayout, postProcessDescriptorSet);
        allocateDescriptorSet(debugDescriptorSetLayout, debugDescriptorSet);
        allocateDescriptorSet(im3dDescriptorSetLayout, im3dDescriptorSet);
        
        for (Scene* s : scenes) {
            if (s != nullptr) {
                for (View* v : s->getViews()) {
                    v->allocateDescriptorSets();
                }
            }
        }

        for (Shader* s : shaders) {
            if (s == nullptr) { continue; }
            if (s->hasRasterGroup()) {
                s->allocateRasterDescriptorSet();
            }
        }
    }

    /*
        More notes about how vulkan works just for me...
            Semaphores act like signals. They signal commands to execute
                - Command A and B are commands and there's a semaphore.
                - Command A starts.
                - While command A is being worked on, command B is waiting.
                - Once command A finishes, command A tells the semaphore that
                   its done
                - After the semaphore signals its signal, command B does its
                   thing
            Fences are like that but for the CPU
                - While a command is working, when vkWaitForFence() is called,
                   the CPU can't really do anything until the command is finished
                - When the command is finished, it signals the fence.
                - The CPU can go back to lolygagging after all that
    */

    // Just pasting this here as a reminder...
    // From the official vulkan tutorial:
    /*
        At a high level, rendering a frame in Vulkan consists of a common set of steps:
            - Wait for the previous frame to finish
            - Acquire an image from the swap chain
            - Record a command buffer which draws the scene onto that image
                - The command to draw basically just puts the pipeline in action
                - And the pipeline has the shaders
            - Submit the recorded command buffer
            - Present the swap chain image
    */
#ifndef RT64_MINIMAL
    void Device::draw(int vsyncInterval, double delta) {
        RT64_LOG_PRINTF("Device drawing started");

        // Recreate the samplers if the anisotropy level were to change
        if (recreateSamplers) {
            for (auto sampler : samplers) {
                vkDestroySampler(vkDevice, sampler.second, nullptr);
            }
            samplers.clear();
            generateSamplers();
            recreateSamplers = false;
        }
        if (descPoolDirty) {
            vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
            createDescriptorPool();
            descPoolDirty = false;
        }
        if (rtStateDirty) {
            vkDestroyPipeline(vkDevice, rtPipeline, nullptr);
            vkDestroyPipelineLayout(vkDevice, rtPipelineLayout, nullptr);
            createRayTracingPipeline();
            rtStateDirty = false;
        }

        VkResult result = vkAcquireNextImageKHR(vkDevice, swapChain, UINT32_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &framebufferIndex);
        waitForGPU();

        // Handle resizing
        if (updateSize(result, "failed to acquire swap chain image!")) {
            // don't draw the image if resized
#ifdef _WIN32
            RedrawWindow(window, NULL, NULL, RDW_INVALIDATE);
#endif
            return;
        }

        // Update the scenes....
        updateScenes();

        vkResetCommandBuffer(commandBuffers[currentFrame], 0);
        vkResetFences(vkDevice, 1, &inFlightFences[currentFrame]);
        fencesUp[currentFrame] = false;

        // Draw the scenes!
        View* activeView = nullptr;
        for (Scene* s : scenes) {
            s->render(delta);
        }

        // Prepare submitting the semaphores
        VkSemaphore waitSemaphores[] = { imageAvailableSemaphores[currentFrame] };
        VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[currentFrame] };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

        VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;
        submitInfo.commandBufferCount = 0;
        submitInfo.pCommandBuffers = nullptr;

        if (commandBufferActive) {
            // Get the cursor position
            double mouseX, mouseY;
            activeView = scenes[0]->getViews()[0];
#ifndef _WIN32
            glfwGetCursorPos(window, &mouseX, &mouseY);
#else
            POINT cursorPos = {};
            GetCursorPos(&cursorPos);
            ScreenToClient(window, &cursorPos);
            mouseX = cursorPos.x;
            mouseY = cursorPos.y;
#endif
            if (inspector.init(this) && showInspector) {
                inspector.render(activeView, mouseX, mouseY);
            }
            if (oldInspectors.empty()) {
                inspector.controlCamera(activeView, mouseX, mouseY);
            } else {
                for (Inspector* i : oldInspectors) {
                    i->render(activeView, mouseX, mouseY);
                }
            }

            // End the command buffer
            endCommandBuffer();

            // Prepare submitting the command buffer
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffers[currentFrame];
        }

        // Submit the queue
        VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]));
        fencesUp[currentFrame] = true;

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        VkSwapchainKHR swapChains[] = { swapChain };
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &framebufferIndex;
        presentInfo.pResults = nullptr; // Optional
        // Now pop it on the screen!
        result = vkQueuePresentKHR(presentQueue, &presentInfo);

        // Now wait for GPU
        waitForGPU();

        // Handle resizing again
        updateSize(result, "failed to present swap chain image!");
        
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
#ifdef RT64_DEBUG
        std::cout << "============================================\n";
#endif
#ifdef _WIN32
        RedrawWindow(window, NULL, NULL, RDW_INVALIDATE);
#endif
    }

    void Device::updateViewport() {
        vkViewport.x = 0.0f;
        vkViewport.y = 0.0f;
        vkViewport.width = static_cast<float>(swapChainExtent.width);
        vkViewport.height = static_cast<float>(swapChainExtent.height);
        vkViewport.minDepth = 0.0f;
        vkViewport.maxDepth = 1.0f;
        vkScissorRect.offset = {0, 0};
        vkScissorRect.extent = swapChainExtent;
    }

    // Recreates the swapchain and what not if the resolution gets resized
    // Returns true if the size got updated, false if not
    //  Also returns a secret third thing if something goes wrong (a runtime error)
    bool Device::updateSize(VkResult result, const char* error) {
        if (result == VK_TIMEOUT || result == VK_NOT_READY || result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
            recreateSwapChain();
            updateViewport();
            framebufferResized = false;
            return true;
        } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error(error);
        }
        return false;
    }

    // The things needed for syncing the CPU and GPU lmao
    void Device::createSyncObjects() {
        imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            VK_CHECK(vkCreateSemaphore(vkDevice, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]));
            VK_CHECK(vkCreateSemaphore(vkDevice, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]));
            VK_CHECK(vkCreateFence(vkDevice, &fenceInfo, nullptr, &inFlightFences[i]));
        }
    }
    
    // I used the DX12 to destroy the DX12
    void Device::createDxcCompiler() {
        RT64_LOG_PRINTF("Compiler creation started");
        D3D12_CHECK(DxcCreateInstance(CLSID_DxcCompiler, __uuidof(IDxcCompiler), (void **)&d3dDxcCompiler));
        D3D12_CHECK(DxcCreateInstance(CLSID_DxcLibrary, __uuidof(IDxcLibrary), (void **)&d3dDxcLibrary));
        RT64_LOG_PRINTF("Compiler creation finished");
    }

    void Device::createCommandBuffers() {
        commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        // Primary command buffers are called in a queue, but secondary command
        //  buffers cannot call primary command buffers
        // Secondary command buffers can't be submitted directly into a queue, but 
        //  primary commmand buffers can call them
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();
        VK_CHECK(vkAllocateCommandBuffers(vkDevice, &allocInfo, commandBuffers.data()));
    }

    void Device::createCommandPool() {
	    RT64_LOG_PRINTF("Command pool creation started");
        QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
        
        VK_CHECK(vkCreateCommandPool(vkDevice, &poolInfo, nullptr, &commandPool));
    }

    void Device::createFramebuffers() {
	    RT64_LOG_PRINTF("Framebuffer creation started");
        swapChainFramebuffers.resize(swapChainImageViews.size());

        for (size_t i = 0; i < swapChainImageViews.size(); i++) {
            std::vector<VkImageView> attachments = {
                swapChainImageViews[i]
            };
            // for (int j = 0; j < depthViews.size(); j++) {  
            //     attachments.push_back(*depthViews[j]);
            // }

            VkFramebufferCreateInfo framebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            framebufferInfo.renderPass = presentRenderPass;
            framebufferInfo.attachmentCount = attachments.size();
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = swapChainExtent.width;
            framebufferInfo.height = swapChainExtent.height;
            framebufferInfo.layers = 1;

            VK_CHECK(vkCreateFramebuffer(vkDevice, &framebufferInfo, nullptr, &swapChainFramebuffers[i]));
        }
    }

    void Device::waitForGPU() {
        if (fencesUp[currentFrame]) {
            vkWaitForFences(vkDevice, 1, &inFlightFences[currentFrame], VK_TRUE, UINT32_MAX);
        }
    }

    void Device::createFramebuffer(VkFramebuffer& framebuffer, VkRenderPass& renderPass, VkImageView& imageView, VkImageView* depthView, VkExtent2D extent) {
        std::vector<VkImageView> attachments = { imageView };
        if (depthView != nullptr) {
            attachments.push_back(*depthView);
        }
        VkFramebufferCreateInfo framebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = attachments.size();
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;
        VK_CHECK(vkCreateFramebuffer(vkDevice, &framebufferInfo, nullptr, &framebuffer));
    }

    void Device::createRenderPass(VkRenderPass& renderPass, bool useDepth, VkFormat imageFormat, VkImageLayout finalLayout) {
	    RT64_LOG_PRINTF("Render pass creation started");
        // Describe the render pass
        // TODO: Make this function, like, less hard coded
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = imageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;        // Not gonna worry about stencil buffers
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = finalLayout;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = VK_FORMAT_D32_SFLOAT;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // TODO: Do some subpass trickery later on
        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = useDepth ? &depthAttachmentRef : nullptr;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkAttachmentDescription attachments[] = {colorAttachment, depthAttachment};
        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = useDepth ? 2 : 1;     // Set it to two when ready
        renderPassInfo.pAttachments = attachments;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        VK_CHECK(vkCreateRenderPass(vkDevice, &renderPassInfo, nullptr, &renderPass));
    }

    void Device::createShaderModule(const void* code, size_t size, const char* entryName, VkShaderStageFlagBits stage, VkPipelineShaderStageCreateInfo& shaderStageInfo, VkShaderModule& shader, std::vector<VkPipelineShaderStageCreateInfo>* shaderStages) {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = size;
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code);

        VK_CHECK(vkCreateShaderModule(vkDevice, &createInfo, nullptr, &shader));
        // Create the shader stage info
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.module = shader;
        shaderStageInfo.pName = entryName;
        shaderStageInfo.stage = stage;

        if (shaderStages) {
            shaderStages->push_back(shaderStageInfo);
        }
    }

    void Device::createSwapChain() {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);

        VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
        VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
        VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);
        
        uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
        if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
            imageCount = swapChainSupport.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = vkSurface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
        uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

        if (indices.graphicsFamily != indices.presentFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0; // Optional
            createInfo.pQueueFamilyIndices = nullptr; // Optional
        }
        createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        VK_CHECK(vkCreateSwapchainKHR(vkDevice, &createInfo, nullptr, &swapChain));

        swapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(vkDevice, swapChain, &imageCount, swapChainImages.data());
        swapChainImageFormat = surfaceFormat.format;
        swapChainExtent = extent;
    }

    void Device::cleanupSwapChain() {
        for (auto imageView : swapChainImageViews) {
            vkDestroyImageView(vkDevice, imageView, nullptr);
        }
        for (auto framebuffer : swapChainFramebuffers) {
            vkDestroyFramebuffer(vkDevice, framebuffer, nullptr);
        }
        vkDestroySwapchainKHR(vkDevice, swapChain, nullptr);
    }

    // Y'know, if u ever wanna like resize ur window
    void Device::recreateSwapChain() {
        RT64_LOG_PRINTF("Starting device size update");
#ifndef _WIN32
        int w, h = 0;
        glfwGetFramebufferSize(&window, &w, &h);
        while (w == 0 || h == 0) {
            glfwGetFramebufferSize(&window, &w, &h);
            glfwWaitEvents();
        }
#else

        RECT rect;
        GetClientRect(window, &rect);
        int w = rect.right - rect.left;
        int h = rect.bottom - rect.top;
        while (w == 0 || h == 0) {
            GetClientRect(window, &rect);
            w = rect.right - rect.left;
            h = rect.bottom - rect.top;
        }
#endif

        vkDeviceWaitIdle(vkDevice);

        cleanupSwapChain();

        createSwapChain();
        createImageViews();
        resizeScenes();
        createFramebuffers();
        if (showInspector) {
            inspector.resize();
        } else {
            for (Inspector* i : oldInspectors) {
                i->resize();
            }
        }
        RT64_LOG_PRINTF("Finished device size update");
    }

    void Device::updateScenes() {
        for (Scene* s : scenes) {
            s->update();
        }
    }

    void Device::resizeScenes() {
        for (Scene* s : scenes) {
            s->resize();
        }
    }

    void Device::loadBlueNoise() {
        blueNoise = new Texture(this);
        blueNoise->setRGBA8((void*)LDR_64_64_64_RGB1_BGRA8, sizeof(LDR_64_64_64_RGB1_BGRA8), 512, 512, 512 * 4, false);
    }

#endif

    void Device::createMemoryAllocator() {
        RT64_LOG_PRINTF("Creating memory allocator");
        VmaVulkanFunctions vulkanFunctions = {};
        vulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
        vulkanFunctions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;
        
        VmaAllocatorCreateInfo allocatorCreateInfo = {};
        allocatorCreateInfo.vulkanApiVersion = RT64_VULKAN_VERSION;
        allocatorCreateInfo.physicalDevice = physicalDevice;
        allocatorCreateInfo.device = vkDevice;
        allocatorCreateInfo.instance = vkInstance;
        allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;
        allocatorCreateInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        VK_CHECK(vmaCreateAllocator(&allocatorCreateInfo, &allocator));

        rtAllocator.init(vkInstance, vkDevice, physicalDevice);
    }

	void Device::createVKInstance() 
    {
	    RT64_LOG_PRINTF("Creating Vulkan instance");
        
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "RT64VK Application";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = RT64_VULKAN_VERSION;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        auto extensions = getInstanceExtensions();
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo;
        if (enableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();

            populateDebugMessengerCreateInfo(debugCreateInfo);
            createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debugCreateInfo;
        } else {
            createInfo.enabledLayerCount = 0;
            createInfo.pNext = nullptr;
        }

        VK_CHECK(vkCreateInstance(&createInfo, nullptr, &vkInstance));

        hasGflwRequiredInstanceExtensions();
    }

#ifndef RT64_MINIMAL
    void Device::createSurface() 
    { 

#ifdef _WIN32
        VkWin32SurfaceCreateInfoKHR createInfo { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
        createInfo.hwnd = window;
        createInfo.hinstance = GetModuleHandle(nullptr);
        vkCreateWin32SurfaceKHR(vkInstance, &createInfo, nullptr, &vkSurface);
#else
        VK_CHECK(glfwCreateWindowSurface(vkInstance, window, nullptr, &vkSurface));
#endif
    }
#endif

    void Device::pickPhysicalDevice() 
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(vkInstance, &deviceCount, nullptr);
        if (deviceCount == 0) {
            throw std::runtime_error("failed to find GPUs with Vulkan support!");
        }
        
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(vkInstance, &deviceCount, devices.data());
        
        for (const auto& device : devices) {
            if (isDeviceSuitable(device)) {
                physicalDevice = device;
                break;
            }
        }

        if (physicalDevice == VK_NULL_HANDLE) {
            throw std::runtime_error("failed to find a suitable GPU!");
        }
    }

    void Device::createLogicalDevice() {
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};

        float queuePriority = 1.0f;
        for (uint32_t queueFamily : uniqueQueueFamilies) {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }
        
        VkPhysicalDeviceFeatures deviceFeatures{};
        VkPhysicalDeviceFeatures2 deviceFeatures2{};
        VkPhysicalDeviceVulkan11Features deviceFeatures11{};
        VkPhysicalDeviceVulkan12Features deviceFeatures12{};
        deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        deviceFeatures11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        deviceFeatures12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        deviceFeatures2.features = deviceFeatures;
        deviceFeatures2.pNext = &deviceFeatures12;
        deviceFeatures12.pNext = &deviceFeatures11;
        vkGetPhysicalDeviceFeatures2(physicalDevice, &deviceFeatures2);

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        // createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.pNext = &deviceFeatures2;

        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();
        if (enableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();
        } else {
            createInfo.enabledLayerCount = 0;
        }

        VK_CHECK(vkCreateDevice(physicalDevice, &createInfo, nullptr, &vkDevice));

        vkGetDeviceQueue(vkDevice, indices.graphicsFamily.value(), 0, &graphicsQueue);
        vkGetDeviceQueue(vkDevice, indices.presentFamily.value(), 0, &presentQueue);
    }

    SwapChainSupportDetails Device::querySwapChainSupport(VkPhysicalDevice device) {
        SwapChainSupportDetails details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, vkSurface, &details.capabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, vkSurface, &formatCount, nullptr);
        if (formatCount != 0) {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, vkSurface, &formatCount, details.formats.data());
        }

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, vkSurface, &presentModeCount, nullptr);
        if (presentModeCount != 0) {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, vkSurface, &presentModeCount, details.presentModes.data());
        }

        return details;
    }

    VkSurfaceFormatKHR Device::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
    {
        for (const auto& availableFormat : availableFormats) {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return availableFormat;
            }
        }
        return availableFormats[0];
    }

    VkPresentModeKHR Device::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) 
    {
        for (const auto& availablePresentMode : availablePresentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return availablePresentMode;
            }
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }
    
    VkExtent2D Device::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) 
    {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        } else {
            int width, height;
#ifndef _WIN32
            glfwGetFramebufferSize(window, &width, &height);
#else
            RECT rect;
            GetClientRect(window, &rect);
            width = rect.right - rect.left;
            height = rect.bottom - rect.top;
#endif

            VkExtent2D actualExtent = {
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)
            };

            actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

            return actualExtent;
        }
    }

    void Device::initRayTracing()
    {
        // Requesting ray tracing properties
        VkPhysicalDeviceProperties2 prop2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        prop2.pNext = &rtProperties;
        vkGetPhysicalDeviceProperties2(physicalDevice, &prop2);
    }

    void Device::createImageViews() {
        swapChainImageViews.resize(swapChainImages.size());
        for (size_t i = 0; i < swapChainImages.size(); i++) {
            swapChainImageViews[i] = createImageView(swapChainImages[i], swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
        }
    }

    bool Device::isDeviceSuitable(VkPhysicalDevice device) {
        QueueFamilyIndices indices = findQueueFamilies(device);
        bool extensionsSupported = checkDeviceExtensionSupport(device);
        bool swapChainAdequate = false;
        if (extensionsSupported) {
            SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
            swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
        }
        return indices.isComplete() && extensionsSupported && swapChainAdequate;

    }

    bool Device::checkDeviceExtensionSupport(VkPhysicalDevice device) {
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

        std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

        for (const auto& extension : availableExtensions) {
            requiredExtensions.erase(extension.extensionName);
        }

        return requiredExtensions.empty();
    }

    // local callback functions
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
        void *pUserData) 
    {
        std::cerr << "********************************************************" << std::endl;
        std::cerr << "[VALIDATION LAYER] " << pCallbackData->pMessage << std::endl;
        std::cerr << std::endl;
        return VK_FALSE;
    }

    VkResult CreateDebugUtilsMessengerEXT(
        VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
        const VkAllocationCallbacks *pAllocator,
        VkDebugUtilsMessengerEXT *pDebugMessenger) 
    {
        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance,
            "vkCreateDebugUtilsMessengerEXT");
        if (func != nullptr) {
            return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
        } else {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }

    void Device::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo) 
    {
        createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = debugCallback;
        createInfo.pUserData = nullptr;  // Optional
    }

    void Device::setupDebugMessenger() {
        if (!enableValidationLayers) return;
        VkDebugUtilsMessengerCreateInfoEXT createInfo;
        populateDebugMessengerCreateInfo(createInfo);
        VK_CHECK(CreateDebugUtilsMessengerEXT(vkInstance, &createInfo, nullptr, &debugMessenger));
    }

    QueueFamilyIndices Device::findQueueFamilies(VkPhysicalDevice device)
    {
        QueueFamilyIndices indices;
        // Assign index to queue families that could be found
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        int i = 0;
        for (const auto& queueFamily : queueFamilies) {
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphicsFamily = i;
            }
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, vkSurface, &presentSupport);
            if (presentSupport) {
                indices.presentFamily = i;
            }
            if (indices.isComplete()) {
                break;
            }
            i++;
        }

        return indices;
    }

    std::vector<const char*> Device::getInstanceExtensions() 
    {
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions;
        // glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

        if (enableValidationLayers) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        return extensions;
    }

    void Device::hasGflwRequiredInstanceExtensions() 
    {
        uint32_t extensionCount = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

        std::cout << "available extensions:" << std::endl;
        std::unordered_set<std::string> available;
        for (const auto &extension : extensions) {
            std::cout << "\t" << extension.extensionName << std::endl;
            available.insert(extension.extensionName);
        }

        std::cout << "required extensions:" << std::endl;
        auto requiredExtensions = getInstanceExtensions();
        for (const auto &required : requiredExtensions) {
            std::cout << "\t" << required << std::endl;
            if (available.find(required) == available.end()) {
                throw std::runtime_error("Missing required glfw extension");
            }
        }
    }

#ifndef RT64_MINIMAL
#ifndef _WIN32
    void Device::framebufferResizeCallback(GLFWwindow* glfwWindow, int width, int height) {
        auto rt64Device = reinterpret_cast<Device*>(glfwGetWindowUserPointer(glfwWindow));
        rt64Device->framebufferResized = true;
        rt64Device->width = width;
        rt64Device->height = height;
    }
#endif
#endif

    void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) {
            func(instance, debugMessenger, pAllocator);
        }
    }

    Device::~Device() {
#ifndef RT64_MINIMAL
        vkDeviceWaitIdle(vkDevice);

        // Destroy the scenes
        auto scenesCopy = scenes;
        for (Scene* s : scenesCopy) {
            delete s;
        }
        // Destroy the meshes
        auto meshesCopy = meshes;
        for (Mesh* m : meshesCopy) {
            delete m;
        }
        // Destroy the textures
        auto texturesCopy = textures;
        for (Texture* t : texturesCopy) {
            delete t;
        }
        // Destroy the shaders
        auto shadersCopy = shaders;
        for (Shader* sh : shadersCopy) {
            if (sh != nullptr) {
                delete sh;
            }
        }
        // Destroy the old inspectors
        auto inspectorsCopy = oldInspectors;
        for (Inspector* i : inspectorsCopy) {
            delete i;
        }
        // Destroy the samplers
        for (auto sampler : samplers) {
            vkDestroySampler(vkDevice, sampler.second, nullptr);
        }

        // Destroy the mipmap generator
        if (!disableMipmaps) {
            delete mipmaps;
        }

        cleanupSwapChain();
        vkDestroySurfaceKHR(vkInstance, vkSurface, nullptr);
        vkDestroyRenderPass(vkDevice, presentRenderPass, nullptr);
        vkDestroyRenderPass(vkDevice, offscreenRenderPass, nullptr);
        vkDestroyCommandPool(vkDevice, commandPool, nullptr);
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(vkDevice, imageAvailableSemaphores[i], nullptr);
            vkDestroySemaphore(vkDevice, renderFinishedSemaphores[i], nullptr);
            vkDestroyFence(vkDevice, inFlightFences[i], nullptr);
        }

        // Destroy the inspector
        inspector.destroy();
        
        rtAllocator.deinit();
		vmaDestroyAllocator(allocator);

        // Destroy shader modules
        vkDestroyShaderModule(vkDevice, primaryRayGenModule, nullptr);
        vkDestroyShaderModule(vkDevice, directRayGenModule, nullptr);
        vkDestroyShaderModule(vkDevice, indirectRayGenModule, nullptr);
        vkDestroyShaderModule(vkDevice, reflectionRayGenModule, nullptr);
        vkDestroyShaderModule(vkDevice, refractionRayGenModule, nullptr);
        vkDestroyShaderModule(vkDevice, surfaceMissModule, nullptr);
        vkDestroyShaderModule(vkDevice, shadowMissModule, nullptr);
        vkDestroyShaderModule(vkDevice, fullscreenVSModule, nullptr);
        vkDestroyShaderModule(vkDevice, gaussianFilterRGB3x3CSModule, nullptr);
        vkDestroyShaderModule(vkDevice, composePSModule, nullptr);
        vkDestroyShaderModule(vkDevice, tonemappingPSModule, nullptr);
        vkDestroyShaderModule(vkDevice, postProcessPSModule, nullptr);
        vkDestroyShaderModule(vkDevice, debugPSModule, nullptr);
        vkDestroyShaderModule(vkDevice, im3dVSModule, nullptr);
        vkDestroyShaderModule(vkDevice, im3dPSModule, nullptr);
        vkDestroyShaderModule(vkDevice, im3dGSPointsModule, nullptr);
        vkDestroyShaderModule(vkDevice, im3dGSLinesModule, nullptr);
        vkDestroySampler(vkDevice, gaussianSampler, nullptr);
        vkDestroySampler(vkDevice, composeSampler, nullptr);
        vkDestroySampler(vkDevice, postProcessSampler, nullptr);
        vkDestroySampler(vkDevice, tonemappingSampler, nullptr);

        // Destroy the descriptor pool
        vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
        // Destroy RT pipeline and descriptor set
        vkDestroyPipeline(vkDevice, rtPipeline, nullptr);
        vkDestroyPipelineLayout(vkDevice, rtPipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(vkDevice, rtDescriptorSetLayout, nullptr);
        // Destroy compose pipeline and descriptor set
        vkDestroyPipeline(vkDevice, composePipeline, nullptr);
        vkDestroyPipelineLayout(vkDevice, composePipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(vkDevice, composeDescriptorSetLayout, nullptr);
        // Destroy tonemapping pipeline and descriptor set
        vkDestroyPipeline(vkDevice, tonemappingPipeline, nullptr);
        vkDestroyPipelineLayout(vkDevice, tonemappingPipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(vkDevice, tonemappingDescriptorSetLayout, nullptr);
        // Destroy post process pipeline and descriptor set
        vkDestroyPipeline(vkDevice, postProcessPipeline, nullptr);
        vkDestroyPipelineLayout(vkDevice, postProcessPipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(vkDevice, postProcessDescriptorSetLayout, nullptr);
        // Destroy debug pipeline and descriptor set
        vkDestroyPipeline(vkDevice, debugPipeline, nullptr);
        vkDestroyPipelineLayout(vkDevice, debugPipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(vkDevice, debugDescriptorSetLayout, nullptr);
        // Destroy im3d pipeline and descriptor set
        vkDestroyPipeline(vkDevice, im3dPipeline, nullptr);
        vkDestroyPipeline(vkDevice, im3dLinesPipeline, nullptr);
        vkDestroyPipeline(vkDevice, im3dPointsPipeline, nullptr);
        vkDestroyPipelineLayout(vkDevice, im3dPipelineLayout, nullptr);
        vkDestroyPipelineLayout(vkDevice, im3dLinesPipelineLayout, nullptr);
        vkDestroyPipelineLayout(vkDevice, im3dPointsPipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(vkDevice, im3dDescriptorSetLayout, nullptr);
        // Destroy gaussian blur pipeline and descriptor set
        vkDestroyPipeline(vkDevice, gaussianFilterRGB3x3Pipeline, nullptr);
        vkDestroyPipelineLayout(vkDevice, gaussianFilterRGB3x3PipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(vkDevice, gaussianFilterRGB3x3DescriptorSetLayout, nullptr);
#endif
        if (enableValidationLayers) {
            DestroyDebugUtilsMessengerEXT(vkInstance, debugMessenger, nullptr);
        }
        vkctx.deinit();
        // Destroy the window
#ifndef _WIN32
        glfwDestroyWindow(window);
        glfwTerminate();
#endif
        RT64_LOG_CLOSE();
    }

    // Public

#ifndef RT64_MINIMAL

    /********************** Getters **********************/
    // Returns the device's Window
    RT64_WINDOW Device::getWindow() { return window; }
    // Returns Vulkan instance
    VkInstance& Device::getVkInstance() { return vkInstance; }
    // Returns Vulkan device
    VkDevice& Device::getVkDevice() { return vkDevice; }
    // Returns physical device
    VkPhysicalDevice& Device::getPhysicalDevice() { return physicalDevice; }
    // Returns VMA allocator
    VmaAllocator& Device::getMemAllocator() { return allocator; }
    // Returns RT allocator
	nvvk::ResourceAllocator& Device::getRTAllocator() { return rtAllocator; }

    VkRenderPass& Device::getPresentRenderPass() { return presentRenderPass; };
    VkRenderPass& Device::getOffscreenRenderPass() { return offscreenRenderPass; };
    VkExtent2D& Device::getSwapchainExtent() { return swapChainExtent; }
	VkViewport& Device::getViewport() { return vkViewport; }
	VkRect2D& Device::getScissors() { return vkScissorRect; }
	int Device::getWidth() { return swapChainExtent.width; }
	int Device::getHeight() { return swapChainExtent.height; }
	double Device::getAspectRatio() { return (double)swapChainExtent.width / (double)swapChainExtent.height; }
	int Device::getCurrentFrameIndex() { return currentFrame; }

	VkCommandBuffer& Device::getCurrentCommandBuffer() { return commandBuffers[currentFrame]; }
    VkDescriptorPool& Device::getDescriptorPool() { return descriptorPool; }
    VkFramebuffer& Device::getCurrentSwapchainFramebuffer() { return swapChainFramebuffers[framebufferIndex]; };
    IDxcCompiler* Device::getDxcCompiler() { return d3dDxcCompiler; }
    IDxcLibrary* Device::getDxcLibrary() { return d3dDxcLibrary; }
    VkPipeline& Device::getRTPipeline() { return rtPipeline; }
    VkPipelineLayout&   Device::getRTPipelineLayout() { return rtPipelineLayout; }
    VkDescriptorSet&    Device::getRTDescriptorSet() { return rtDescriptorSet; }
    VkDescriptorSetLayout& Device::getRTDescriptorSetLayout() { return rtDescriptorSetLayout; }
    std::vector<VkDescriptorSet>& Device::getRTDescriptorSets() { return rtDescriptorSets; }
    VkPipeline&             Device::getComposePipeline()                            { return composePipeline; }
    VkPipelineLayout&       Device::getComposePipelineLayout()                      { return composePipelineLayout; }
    VkDescriptorSet&        Device::getComposeDescriptorSet()                       { return composeDescriptorSet; }
    VkSampler&              Device::getComposeSampler()                             { return composeSampler; }
    VkPipeline&             Device::getTonemappingPipeline()                        { return tonemappingPipeline; }
    VkPipelineLayout&       Device::getTonemappingPipelineLayout()                  { return tonemappingPipelineLayout; }
    VkDescriptorSet&        Device::getTonemappingDescriptorSet()                   { return tonemappingDescriptorSet; }
    VkSampler&              Device::getTonemappingSampler()                         { return tonemappingSampler; }
    VkPipeline&             Device::getPostProcessPipeline()                        { return postProcessPipeline; }
    VkPipelineLayout&       Device::getPostProcessPipelineLayout()                  { return postProcessPipelineLayout; }
    VkDescriptorSet&        Device::getPostProcessDescriptorSet()                   { return postProcessDescriptorSet; }
    VkSampler&              Device::getPostProcessSampler()                         { return postProcessSampler; }
    VkPipeline&             Device::getDebugPipeline()                              { return debugPipeline; }
    VkPipelineLayout&       Device::getDebugPipelineLayout()                        { return debugPipelineLayout; }
    VkDescriptorSet&        Device::getDebugDescriptorSet()                         { return debugDescriptorSet; }
    VkPipeline&             Device::getIm3dPipeline()                               { return im3dPipeline; }
    VkPipelineLayout&       Device::getIm3dPipelineLayout()                         { return im3dPipelineLayout; }
    VkPipeline&             Device::getIm3dPointsPipeline()                         { return im3dPointsPipeline; }
    VkPipelineLayout&       Device::getIm3dPointsPipelineLayout()                   { return im3dPointsPipelineLayout; }
    VkPipeline&             Device::getIm3dLinesPipeline()                          { return im3dLinesPipeline; }
    VkPipelineLayout&       Device::getIm3dLinesPipelineLayout()                    { return im3dLinesPipelineLayout; }
    VkDescriptorSet&        Device::getIm3dDescriptorSet()                          { return im3dDescriptorSet; }
    VkPipeline&             Device::getGaussianFilterRGB3x3Pipeline()               { return gaussianFilterRGB3x3Pipeline; }
    VkPipelineLayout&       Device::getGaussianFilterRGB3x3PipelineLayout()         { return gaussianFilterRGB3x3PipelineLayout; }
    VkDescriptorSetLayout&  Device::getGaussianFilterRGB3x3DescriptorSetLayout()    { return gaussianFilterRGB3x3DescriptorSetLayout; }
    VkSampler&          Device::getGaussianSampler()                    { return gaussianSampler; }
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR Device::getRTProperties() const { return rtProperties; }
    Texture* Device::getBlueNoise() const { return blueNoise; }
    uint32_t Device::getHitGroupCount() const { return hitGroupCount; }
    uint32_t Device::getRasterGroupCount() const { return rasterGroupCount; }
    VkFence& Device::getCurrentFence() { return inFlightFences[currentFrame]; }
    Inspector& Device::getInspector() { return inspector; }
    Mipmaps* Device::getMipmaps() { return mipmaps; }
    float Device::getAnisotropyLevel() { return anisotropy; }
    VkPhysicalDeviceProperties Device::getPhysicalDeviceProperties() { return physDeviceProperties; }

    void Device::setAnisotropyLevel(float level) {
        if (level != anisotropy) {
            anisotropy = level;
            recreateSamplers = true;
        }
    }

    // Shader getters
    VkPipelineShaderStageCreateInfo Device::getPrimaryShaderStage() const { return primaryRayGenStage; }
    VkPipelineShaderStageCreateInfo Device::getDirectShaderStage() const { return directRayGenStage; }
    VkPipelineShaderStageCreateInfo Device::getIndirectShaderStage() const { return indirectRayGenStage; }
    VkPipelineShaderStageCreateInfo Device::getReflectionShaderStage() const { return reflectionRayGenStage; }
    VkPipelineShaderStageCreateInfo Device::getRefractionShaderStage() const { return refractionRayGenStage; }

    void Device::initRTBuilder(nvvk::RaytracingBuilderKHR& rtBuilder) {
        rtBuilder.setup(vkDevice, &rtAllocator, vkctx.m_queueC);
    }

    void Device::setInspectorVisibility(bool v) { showInspector = v; }

    // Adds a scene to the device
    void Device::addScene(Scene* scene) {
        assert(scene != nullptr);
        scenes.push_back(scene);
    }
    
    // Removes a scene from the device
    void Device::removeScene(Scene *scene) {
        assert(scene != nullptr);
        scenes.erase(std::remove(scenes.begin(), scenes.end(), scene), scenes.end());
    }

    // Adds a mesh to the device
    void Device::addMesh(Mesh* mesh) {
        assert(mesh != nullptr);
        meshes.push_back(mesh);
    }
    
    // Removes a mesh from the device
    void Device::removeMesh(Mesh* mesh) {
        assert(mesh != nullptr);
        meshes.erase(std::remove(meshes.begin(), meshes.end(), mesh), meshes.end());
    }

    // Adds a texture to the device
    void Device::addTexture(Texture* texture) {
        assert(texture != nullptr);
        textures.push_back(texture);
    }
    
    // Removes a texture from the device
    void Device::removeTexture(Texture* texture) {
        assert(texture != nullptr);
        textures.erase(std::remove(textures.begin(), textures.end(), texture), textures.end());
    }

    // Adds an old-style inspector to the device
    void Device::addInspectorOld(Inspector* inspector) {
        assert(inspector != nullptr);
        oldInspectors.push_back(inspector);
    }
    
    // Removes an old-style inspector from the device
    void Device::removeInspectorOld(Inspector* inspector) {
        assert(inspector != nullptr);
        oldInspectors.erase(std::remove(oldInspectors.begin(), oldInspectors.end(), inspector), oldInspectors.end());
    }


    ImGui_ImplVulkan_InitInfo Device::generateImguiInitInfo() {
        ImGui_ImplVulkan_InitInfo info {};
        info.Instance = vkInstance;
        info.Device = vkDevice;
        info.PhysicalDevice = physicalDevice;
        info.Queue = vkctx.m_queueGCT;
        info.QueueFamily = vkctx.m_queueGCT.familyIndex;
        info.MinImageCount = MAX_FRAMES_IN_FLIGHT;
        info.ImageCount = MAX_FRAMES_IN_FLIGHT;
        return info;
    }

    // Adds a scene to the device
    void Device::addShader(Shader* shader) {
        assert(shader != nullptr);
        shaders.emplace(shader);
        if (shader->hasHitGroups()) {
            hitGroupCount += shader->hitGroupCount();
            shaderGroupCount += shader->hitGroupCount();
            rtStateDirty = true;
        }
        if (shader->hasRasterGroup()) {
            shaderGroupCount++;
            rasterGroupCount++;
        }
        descPoolDirty = true;
    }

    std::unordered_map<unsigned int, VkSampler>& Device::getSamplerMap() { return samplers; } 
    VkSampler& Device::getSampler(unsigned int index) { return samplers[index]; }

    // Removes a shader from the device
    void Device::removeShader(Shader* shader) {
        assert(shader != nullptr && shaders.size() > 0 );
        shaders.erase(shader);
        if (shader->hasHitGroups()) {
            hitGroupCount -= shader->hitGroupCount();
            shaderGroupCount -= shader->hitGroupCount();
            rtStateDirty = true;
        }
        if (shader->hasRasterGroup()) {
            shaderGroupCount--;
            rasterGroupCount--;
        }
        descPoolDirty = true;
    }

    void Device::addDepthImageView(VkImageView* depthImageView) {
        assert(depthImageView != nullptr);
        depthViews.push_back(depthImageView);
    }

    void Device::removeDepthImageView(VkImageView* depthImageView) {
        assert(depthImageView != nullptr);
        depthViews.erase(std::remove(depthViews.begin(), depthViews.end(), depthImageView), depthViews.end());
    }

    // Sets the descPoolDirty state to true.
    // Call this anytime you have a new descriptor set
    void Device::dirtyDescriptorPool() { descPoolDirty = true; }

    // Creates an allocated buffer. You must pass in a pointer to an AllocatedResource. Once the function does its thing, the pointer will point to the newly created AllocatedBuffer with the buffer
    VkResult Device::allocateBuffer(
            VkDeviceSize bufferSize, 
            VkBufferUsageFlags bufferUsage, 
            VmaMemoryUsage memUsage, 
            VmaAllocationCreateFlags allocProperties, 
            AllocatedBuffer* alre
        ) 
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = bufferUsage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.usage = memUsage;
        allocCreateInfo.flags = allocProperties;
        VmaAllocationInfo allocInfo = {};

        VK_CHECK(alre->init(&allocator, bufferInfo, allocCreateInfo, allocInfo));
        return VK_SUCCESS;
    }

    // Creates an allocated image. You must pass in a pointer to an AllocatedResource. Once the function does its thing, the pointer will point to the newly created AllocatedImage with the image
    VkResult Device::allocateImage(
        uint32_t width, uint32_t height, 
        VkImageType imageType, 
        VkFormat imageFormat, 
        VkImageTiling imageTiling, 
        VkImageLayout initLayout, 
        VkImageUsageFlags imageUsage, 
        VmaMemoryUsage memUsage, 
        VmaAllocationCreateFlags allocProperties, 
        AllocatedImage* alre
    ) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = imageType;
        imageInfo.format = imageFormat;
        imageInfo.tiling = imageTiling;
        imageInfo.initialLayout = initLayout;
        imageInfo.usage = imageUsage;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;             // Maybe if I were to like
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.flags = 0;
        return allocateImage(alre, imageInfo, memUsage, allocProperties);
    }

    
    VkResult Device::allocateImage(
        AllocatedImage* alre,
        VkImageCreateInfo createInfo, 
        VmaMemoryUsage memUsage, 
        VmaAllocationCreateFlags allocProperties
    ) {
        VkResult res = VK_SUCCESS;
        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.usage = memUsage;
        allocCreateInfo.flags = allocProperties;
        VmaAllocationInfo allocInfo = {};

        VK_CHECK(alre->init(&allocator, createInfo, allocCreateInfo, allocInfo));
        return VK_SUCCESS;;
    }

    // Copies a buffer into another
    //  If a pointer to a command buffer is passed into the function, this function will use the passed-in command buffer
    //  Otherwise, it would create a new command buffer just for this
    void Device::copyBuffer(VkBuffer src, VkBuffer dest, VkDeviceSize size, VkCommandBuffer* commandBuffer) {
        bool oneTime = !commandBuffer;
        if (!commandBuffer) {
            commandBuffer = beginSingleTimeCommands();
        }

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0; // Optional
        copyRegion.dstOffset = 0; // Optional
        copyRegion.size = size;
        vkCmdCopyBuffer(*commandBuffer, src, dest, 1, &copyRegion);

        if (oneTime) {
            endSingleTimeCommands(commandBuffer);
        }
    }

    // Copies an image into another
    //  If a pointer to a command buffer is passed into the function, this function will use the passed-in command buffer
    //  Otherwise, it would create a new command buffer just for this
    void Device::copyImage(AllocatedImage& src, AllocatedImage& dest, VkExtent3D dimensions, VkImageAspectFlags srcFlags, VkImageAspectFlags destFlags, VkCommandBuffer* commandBuffer) {
        bool oneTime = !commandBuffer;
        if (!commandBuffer) {
            commandBuffer = beginSingleTimeCommands();
        }
        
        VkImageCopy copyRegion {};

        copyRegion.extent = dimensions;
        copyRegion.srcSubresource.layerCount = 1;
        copyRegion.srcSubresource.mipLevel = 0;
        copyRegion.srcSubresource.aspectMask = srcFlags;
        copyRegion.dstSubresource.layerCount = 1;
        copyRegion.dstSubresource.mipLevel = 0;
        copyRegion.dstSubresource.aspectMask = destFlags;
        vkCmdCopyImage(*commandBuffer, src.getImage(), src.getLayout(), dest.getImage(), dest.getLayout(), 1, &copyRegion);

        if (oneTime) {
            endSingleTimeCommands(commandBuffer);
        }
    }

    // Matches an image layout with an access match and pipeline stage
    //  inLayout is the VkImageLayout to be matched with
    //  outMask and outStages are variables that will be modified to match inLayout
    void Device::matchLayoutToAccessMask(VkImageLayout inLayout, VkAccessFlags& outMask) 
    {
        switch (inLayout) {
            case VK_IMAGE_LAYOUT_UNDEFINED:
                outMask = 0;
                break;
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                outMask = VK_ACCESS_SHADER_READ_BIT;
                break;
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                outMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                break;
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                outMask = VK_ACCESS_SHADER_READ_BIT;
                break;
            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
                outMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                break;
            case VK_IMAGE_LAYOUT_GENERAL:
                outMask = VK_ACCESS_SHADER_WRITE_BIT;
                break;
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                outMask = VK_ACCESS_SHADER_WRITE_BIT;
                break;
            default:
                throw std::invalid_argument("unsupported layout transition!");
        }

    }

    // Creates a global memory barrier
    //  If a pointer to a command buffer is passed into the function, this function will use the passed-in command buffer
    //  Otherwise, it would create a new command buffer just for this
    void Device::memoryBarrier(VkAccessFlags oldMask, VkAccessFlags newMask, VkPipelineStageFlags oldStage, VkPipelineStageFlags newStage, VkCommandBuffer* commandBuffer) {
        VkMemoryBarrier barrier { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        barrier.srcAccessMask = oldMask;
        barrier.dstAccessMask = newMask;
        vkCmdPipelineBarrier(*commandBuffer, oldStage, newStage, 0,1, &barrier, 0, nullptr, 0, nullptr);
    }


    // Creates a memory barrior for the allocated buffer. Gives it new access flags and pipeline stage
    //  If a pointer to a command buffer is passed into the function, this function will use the passed-in command buffer
    //  Otherwise, it would create a new command buffer just for this
    void Device::bufferMemoryBarrier(AllocatedBuffer& buffer, VkAccessFlags newMask, VkPipelineStageFlags newStage, VkCommandBuffer* commandBuffer) {
        bool oneTime = !commandBuffer;
        if (!commandBuffer) {
            commandBuffer = beginSingleTimeCommands();
        }
        
        VkBufferMemoryBarrier barrier { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.srcAccessMask = buffer.getAccessFlags();
        barrier.dstAccessMask = newMask;
        barrier.buffer = buffer.getBuffer();
        barrier.size = buffer.getSize();
        barrier.offset = 0;

        buffer.memoryBarrier(barrier, newStage, commandBuffer);

        if (oneTime) {
            endSingleTimeCommands(commandBuffer);
        }
    }

    // Turns the layout of one image into another
    //  If a pointer to a command buffer is passed into the function, this function will use the passed-in command buffer
    //  Otherwise, it would create a new command buffer just for this
    void Device::transitionImageLayout(AllocatedImage& image, VkImageLayout newLayout, VkAccessFlags newMask, VkPipelineStageFlags newStage, VkCommandBuffer* commandBuffer) {
        bool oneTime = !commandBuffer;
        if (!commandBuffer) {
            commandBuffer = beginSingleTimeCommands();
        }

        VkImageLayout oldLayout = image.getLayout();
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.getImage();
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = image.getAccessFlags();
        barrier.dstAccessMask = newMask;

        image.transitionLayout(commandBuffer, image.getPieplineStage(), newStage, barrier);

        if (oneTime) {
            // The fact that you passed on passing in a pointer to a
            //  command buffer kinda implies that's all you wanna do
            //  with the command buffer :/
            endSingleTimeCommands(commandBuffer);
        }
    }

    // Turns multiple images of one layout into another
    //  If a pointer to a command buffer is passed into the function, this function will use the passed-in command buffer
    //  Otherwise, it would create a new command buffer just for this
    void Device::transitionImageLayout(AllocatedImage** images, uint32_t imageCount, VkImageLayout newLayout, VkAccessFlags newMask, VkPipelineStageFlags oldStage, VkPipelineStageFlags newStage, VkCommandBuffer* commandBuffer) {
        bool oneTime = !commandBuffer;
        if (!commandBuffer) {
            commandBuffer = beginSingleTimeCommands();
        }

        std::vector<VkImageMemoryBarrier> barriers;
        barriers.reserve(imageCount);
        for (int i = 0; i < imageCount; i++) {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = images[i]->getLayout();
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = images[i]->getImage();
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = images[i]->getAccessFlags();
            barrier.dstAccessMask = newMask;
            barriers.push_back(barrier);
        }

        AllocatedImage::transitionLayouts(images, imageCount, commandBuffer, oldStage, newStage, barriers.data(), newLayout);

        if (oneTime) {
            endSingleTimeCommands(commandBuffer);
        }
    }

    //  If a pointer to a command buffer is passed into the function, this function will use the passed-in command buffer
    //  Otherwise, it would create a new command buffer just for this
    void Device::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, VkCommandBuffer* commandBuffer) {
        bool oneTime = !commandBuffer;
        if (!commandBuffer) {
            commandBuffer = beginSingleTimeCommands();
        }

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {
            width,
            height,
            1
        };

        vkCmdCopyBufferToImage(
            *commandBuffer,
            buffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &region
        );

        if (oneTime) {
            endSingleTimeCommands(commandBuffer);
        }
    }

    VkImageView Device::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspectFlags;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView imageView;
        VK_CHECK(vkCreateImageView(vkDevice, &viewInfo, nullptr, &imageView));
        return imageView;
    }

    VkBufferView Device::createBufferView(VkBuffer& buffer, VkFormat format, VkBufferViewCreateFlags flags, VkDeviceSize size) {
        VkBufferViewCreateInfo viewInfo {};
        viewInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
        viewInfo.buffer = buffer;
        viewInfo.format = format;
        viewInfo.flags = flags;
        viewInfo.offset = 0;
        viewInfo.range = size;

        VkBufferView bufferView;
        VK_CHECK(vkCreateBufferView(vkDevice, &viewInfo, nullptr, &bufferView));
        return bufferView;
    }

    // Begin the use of a command buffer that runs temporarily
    // Returns the pointer to the new command buffer
    VkCommandBuffer* Device::beginSingleTimeCommands() { return beginSingleTimeCommands(nullptr); }
    // Begin the use of the passed in command buffer that runs temporarily
    VkCommandBuffer* Device::beginSingleTimeCommands(VkCommandBuffer* commandBuffer) {
        // If no command buffer is passed in, create one
        if (!commandBuffer) {
            commandBuffer = new VkCommandBuffer;
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandPool = commandPool;
            allocInfo.commandBufferCount = 1;
            vkAllocateCommandBuffers(vkDevice, &allocInfo, commandBuffer);
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(*commandBuffer, &beginInfo);

        return commandBuffer;
    }
    // Ends the passed in command buffer and destroys the command buffer
    void Device::endSingleTimeCommands(VkCommandBuffer* commandBuffer) {
        vkEndCommandBuffer(*commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = commandBuffer;

        vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);

        vkFreeCommandBuffers(vkDevice, commandPool, 1, commandBuffer);
    }

    // Starts and returns the current main command buffer
    VkCommandBuffer& Device::beginCommandBuffer() {
        if (!commandBufferActive) {
            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = 0; // Optional
            beginInfo.pInheritanceInfo = nullptr; // Optional
            VK_CHECK(vkBeginCommandBuffer(commandBuffers[currentFrame], &beginInfo));
            commandBufferActive = true;
        }
        return commandBuffers[currentFrame];
    }

    // Ends the current main command buffer if active
    void Device::endCommandBuffer() {
        if (commandBufferActive) {
            VK_CHECK(vkEndCommandBuffer(commandBuffers[currentFrame]));
            commandBufferActive = false;
        }
    }

    // Generates a descriptor set layout and pushes its bindings to the pool 
    void Device::generateDescriptorSetLayout(std::vector<VkDescriptorSetLayoutBinding>& bindings, VkDescriptorBindingFlags& flags, VkDescriptorSetLayout& descriptorSetLayout) {
		for (int i = 0; i < bindings.size(); i++) {
            VkDescriptorPoolSize poolSize;
			poolSize.type = bindings[i].descriptorType;
			poolSize.descriptorCount = bindings[i].descriptorCount;
            descriptorPoolBindings.push_back(poolSize);
		}

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
        VK_CHECK(vkCreateDescriptorSetLayout(vkDevice, &layoutInfo, nullptr, &descriptorSetLayout));
    }

    // Allocates the descriptor set layout and descriptor set to the current descriptor pool
	void Device::allocateDescriptorSet(VkDescriptorSetLayout& descriptorSetLayout, VkDescriptorSet& descriptorSet) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;
        allocInfo.descriptorPool = descriptorPool;
        VK_CHECK(vkAllocateDescriptorSets(vkDevice, &allocInfo, &descriptorSet));
	}
    #endif

}

// Library exports

DLEXPORT RT64_DEVICE* RT64_CreateDevice(void* window) {
	try {
		return (RT64_DEVICE*)(new RT64::Device(static_cast<RT64_WINDOW>(window)));
	}
	RT64_CATCH_EXCEPTION();
	return nullptr;
}

DLEXPORT void RT64_DestroyDevice(RT64_DEVICE* devicePtr) {
	assert(devicePtr != nullptr);
	try {
		delete (RT64::Device*)(devicePtr);
	}
	RT64_CATCH_EXCEPTION();
}

#ifndef RT64_MINIMAL

DLEXPORT void RT64_DrawDevice(RT64_DEVICE* devicePtr, int vsyncInterval, double delta) {
	assert(devicePtr != nullptr);
	try {
		RT64::Device* device = (RT64::Device*)(devicePtr);
		device->draw(vsyncInterval, delta);
	}
	RT64_CATCH_EXCEPTION();
}

#ifdef _WIN32
DLEXPORT bool RT64VK_HandleMessageInspector(RT64_DEVICE* devicePtr, UINT msg, WPARAM wParam, LPARAM lParam) {
    assert(devicePtr != nullptr);
    RT64::Device* device = (RT64::Device*)(devicePtr);
    RT64::Inspector& inspector = device->getInspector();
    return inspector.handleMessage(msg, wParam, lParam);
}
#endif

DLEXPORT void RT64VK_SetSceneInspector(RT64_DEVICE* devicePtr, RT64_SCENE_DESC* sceneDesc) {
    assert(devicePtr != nullptr);
	RT64::Device* device = (RT64::Device*)(devicePtr);
    RT64::Inspector& inspector = device->getInspector();
    inspector.setSceneDescription(sceneDesc);
}

DLEXPORT void RT64VK_SetMaterialInspector(RT64_DEVICE* devicePtr, RT64_MATERIAL* material, const char* materialName) {
    assert(devicePtr != nullptr);
	RT64::Device* device = (RT64::Device*)(devicePtr);
    RT64::Inspector& inspector = device->getInspector();
    inspector.setMaterial(material, std::string(materialName));
}

DLEXPORT void RT64VK_SetLightsInspector(RT64_DEVICE* devicePtr, RT64_LIGHT* lights, int* lightCount, int maxLightCount) {
    assert(devicePtr != nullptr);
	RT64::Device* device = (RT64::Device*)(devicePtr);
    RT64::Inspector& inspector = device->getInspector();
    inspector.setLights(lights, lightCount, maxLightCount);
}

DLEXPORT void RT64VK_PrintClearInspector(RT64_DEVICE* devicePtr) {
    assert(devicePtr != nullptr);
	RT64::Device* device = (RT64::Device*)(devicePtr);
    RT64::Inspector& inspector = device->getInspector();
    inspector.printClear();
}

DLEXPORT void RT64VK_PrintMessageInspector(RT64_DEVICE* devicePtr, const char* message) {
    assert(devicePtr != nullptr);
	RT64::Device* device = (RT64::Device*)(devicePtr);
    RT64::Inspector& inspector = device->getInspector();
    std::string messageStr(message);
    inspector.printMessage(messageStr);
}

DLEXPORT void RT64VK_SetInspectorVisibility(RT64_DEVICE* devicePtr, bool showInspector) {
    assert(devicePtr != nullptr);
	RT64::Device* device = (RT64::Device*)(devicePtr);
    device->setInspectorVisibility(showInspector);
}

#endif