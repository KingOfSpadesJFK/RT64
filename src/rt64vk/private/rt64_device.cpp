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
// TEST SHADER
#include "shaders/TestRayGen.hlsl.h"

// Pixel shaders
#include "shaders/ComposePS.hlsl.h"
#include "shaders/HelloTrianglePS.hlsl.h"
// Vertex shaders
#include "shaders/FullScreenVS.hlsl.h"
#include "shaders/HelloTriangleVS.hlsl.h"

// The blue noise
#include "res/bluenoise/LDR_64_64_64_RGB1.h"

namespace RT64
{

    Device::Device(GLFWwindow* glfwWindow) {
	    RT64_LOG_OPEN("rt64.log");

#ifndef RT64_MINIMAL
        window = glfwWindow;
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
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
        createRenderPass();
        createCommandPool();

        createCommandBuffers();
        createSyncObjects();

        createDxcCompiler();

        loadAssets();

        initRayTracing();
        createRayTracingPipeline();
#endif
    }

    void Device::createVkInstanceNV() {
        // Vulkan required extensions
        assert(glfwVulkanSupported() == 1);
        uint32_t count{0};
        auto     reqExtensions = glfwGetRequiredInstanceExtensions(&count);

        // Requesting Vulkan extensions and layers
        nvvk::ContextCreateInfo contextInfo;
        contextInfo.setVersion(1, 3);                       // Using Vulkan 1.2
        for(uint32_t ext_id = 0; ext_id < count; ext_id++)  // Adding required extensions (surface, win32, linux, ..)
            contextInfo.addInstanceExtension(reqExtensions[ext_id]);
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

        // Creating Vulkan base application
        vkctx.initInstance(contextInfo);
        // Find all compatible devices
        auto compatibleDevices = vkctx.getCompatibleDevices(contextInfo);
        assert(!compatibleDevices.empty());
        // Use a compatible device
        vkctx.initDevice(compatibleDevices[0], contextInfo);
    }

    // Loads the appropriate assets for rendering, such as 
    //  descriptor sets for the rasterization pipeline and
    //  blue noise
    void Device::loadAssets() {
	    RT64_LOG_PRINTF("Asset loading started");
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

        // Pipeline info
        VkGraphicsPipelineCreateInfo pipelineInfo { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        RT64_LOG_PRINTF("Creating a generic image sampler for the compose shader");
        {
            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
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
            samplerInfo.maxLod = 0.0f;
            vkCreateSampler(vkDevice, &samplerInfo, nullptr, &composeSampler);
        }

	    RT64_LOG_PRINTF("Creating the composition descriptor set");
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

            allocateDescriptorSet(bindings, flags, rtComposeDescriptorSetLayout, rtComposeDescriptorPool, rtComposeDescriptorSet);
        }

        RT64_LOG_PRINTF("Creating the composition pipeline");
        {
            // Create the shader modules and shader stages
            std::vector<VkPipelineShaderStageCreateInfo> composeStages;
            createShaderModule(FullScreenVS_SPIRV, sizeof(FullScreenVS_SPIRV), VS_ENTRY, VK_SHADER_STAGE_VERTEX_BIT, fullscreenVSStage, fullscreenVSModule, &composeStages);
            createShaderModule(ComposePS_SPIRV, sizeof(ComposePS_SPIRV), PS_ENTRY, VK_SHADER_STAGE_FRAGMENT_BIT, composePSStage, composePSModule, &composeStages);

            // Bind the vertex inputs
            VkPipelineVertexInputStateCreateInfo vertexInputInfo{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
            vertexInputInfo.vertexBindingDescriptionCount = 0;
            vertexInputInfo.vertexAttributeDescriptionCount = 0;

		    // Create the pipeline layout
            VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            pipelineLayoutInfo.setLayoutCount = 1;
            pipelineLayoutInfo.pSetLayouts = &rtComposeDescriptorSetLayout;
            VK_CHECK(vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr, &rtComposePipelineLayout));

            // Create the pipeline
            pipelineInfo.pVertexInputState = &vertexInputInfo;
            pipelineInfo.stageCount = composeStages.size();
            pipelineInfo.pStages = composeStages.data();
            pipelineInfo.layout = rtComposePipelineLayout;
            VK_CHECK(vkCreateGraphicsPipelines(vkDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &rtComposePipeline));
        }

        loadBlueNoise();

    }

    // Creates the ray tracing pipeline
    void Device::createRayTracingPipeline() {
	    RT64_LOG_PRINTF("Raytracing pipeline creation started");

        // A vector for the shader stages
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

	    RT64_LOG_PRINTF("Loading the ray generation modules...");

        // Create the main shaders
        if (!mainRtShadersCreated) {
            createShaderModule(PrimaryRayGen_SPIRV,     sizeof(PrimaryRayGen_SPIRV),    "PrimaryRayGen",    VK_SHADER_STAGE_RAYGEN_BIT_KHR, primaryRayGenStage,         primaryRayGenModule, &shaderStages);
            createShaderModule(DirectRayGen_SPIRV,      sizeof(DirectRayGen_SPIRV),     "DirectRayGen",     VK_SHADER_STAGE_RAYGEN_BIT_KHR, directRayGenStage,          directRayGenModule, &shaderStages);
            createShaderModule(IndirectRayGen_SPIRV,    sizeof(IndirectRayGen_SPIRV),   "IndirectRayGen",   VK_SHADER_STAGE_RAYGEN_BIT_KHR, indirectRayGenStage,        indirectRayGenModule, &shaderStages);
            createShaderModule(ReflectionRayGen_SPIRV,  sizeof(ReflectionRayGen_SPIRV), "ReflectionRayGen", VK_SHADER_STAGE_RAYGEN_BIT_KHR, reflectionRayGenStage,      reflectionRayGenModule, &shaderStages);
            createShaderModule(RefractionRayGen_SPIRV,  sizeof(RefractionRayGen_SPIRV), "RefractionRayGen", VK_SHADER_STAGE_RAYGEN_BIT_KHR, refractionRayGenStage,      refractionRayGenModule, &shaderStages);
            createShaderModule(PrimaryRayGen_SPIRV,     sizeof(PrimaryRayGen_SPIRV),    "SurfaceMiss",      VK_SHADER_STAGE_MISS_BIT_KHR, surfaceMissStage,             surfaceMissModule, &shaderStages);
            createShaderModule(PrimaryRayGen_SPIRV,     sizeof(PrimaryRayGen_SPIRV),    "ShadowMiss",       VK_SHADER_STAGE_MISS_BIT_KHR, shadowMissStage,              shadowMissModule, &shaderStages);
        } else {
            // Push the main shaders into the shader stages
            shaderStages.push_back(primaryRayGenStage);
            shaderStages.push_back(directRayGenStage);
            shaderStages.push_back(indirectRayGenStage);
            shaderStages.push_back(reflectionRayGenStage);
            shaderStages.push_back(refractionRayGenStage);
            shaderStages.push_back(surfaceMissStage);
            shaderStages.push_back(shadowMissStage);
        }

	    RT64_LOG_PRINTF("Loading the hit modules...");
        // Add the generated hit shaders to the shaderStages vector
         for (Shader* s : shaders) {
            if (s->hasHitGroups()) {
                Shader::HitGroup surfaceHitGroup = s->getSurfaceHitGroup();
                shaderStages.push_back(surfaceHitGroup.shaderInfo);
                Shader::HitGroup shadowHitGroup = s->getShadowHitGroup();
                shaderStages.push_back(shadowHitGroup.shaderInfo);
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
        
	    RT64_LOG_PRINTF("Creating the RT descriptor set layouts and pools...");
        // A vector for the descriptor set layouts
        if (!mainRtShadersCreated) {
            // Don't create the descriptor layout and pool if this already happend
            generateRayTracingDescriptorSetLayout();
            mainRtShadersCreated = true;
        }
        
        // Push the main RT descriptor set layout into the layouts vector
        rtDescriptorSetLayouts.clear();
        rtDescriptorSetLayouts.push_back(raygenDescriptorSetLayout);
        for (Shader* s : shaders) {
            if (s->hasHitGroups()) {
                rtDescriptorSetLayouts.push_back(s->getRTDescriptorSetLayout());
            }
        }

	    RT64_LOG_PRINTF("Creating the RT pipeline...");
        // Create the pipeline layout create info
        VkPipelineLayoutCreateInfo layoutInfo {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = rtDescriptorSetLayouts.size();
        layoutInfo.pSetLayouts = rtDescriptorSetLayouts.data();
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
        rtDescriptorSets.push_back(raygenDescriptorSet);
        for (Shader* s : shaders) {
            if (s->hasHitGroups()) {
                rtDescriptorSets.push_back(s->getRTDescriptorSet());
            }
        }

	    RT64_LOG_PRINTF("Raytracing pipeline created!");
    }

    void Device::generateRayTracingDescriptorSetLayout() {
		VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
        VkShaderStageFlags defaultStageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
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
            {0 + SAMPLER_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLER, 1, defaultStageFlags, nullptr},
            {1 + SAMPLER_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLER, 1, defaultStageFlags, nullptr}
        };

		allocateDescriptorSet(bindings, flags, raygenDescriptorSetLayout, raygenDescriptorPool, raygenDescriptorSet);
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

        if (rtStateDirty) {
            vkDestroyPipeline(vkDevice, rtPipeline, nullptr);
            vkDestroyPipelineLayout(vkDevice, rtPipelineLayout, nullptr);
            createRayTracingPipeline();
            rtStateDirty = false;
        }

        if (!framebufferCreated) {
            createFramebuffers();
            framebufferCreated = true;
        }

        vkWaitForFences(vkDevice, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

        VkResult result = vkAcquireNextImageKHR(vkDevice, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &framebufferIndex);

        // Handle resizing
        if (updateSize(result, "failed to acquire swap chain image!")) {
            // don't draw the image if resized
            return;
        }

        // Update the scenes....
        updateScenes();

        vkResetFences(vkDevice, 1, &inFlightFences[currentFrame]);
        vkResetCommandBuffer(commandBuffers[currentFrame], 0);

        // Draw the scenes!
        for (Scene* s : scenes) {
            s->render(delta);
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

        VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;
        VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]));

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        VkSwapchainKHR swapChains[] = {swapChain};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &framebufferIndex;
        presentInfo.pResults = nullptr; // Optional
        // Now pop it on the screen!
        result = vkQueuePresentKHR(presentQueue, &presentInfo); 
        
        // Now wait for GPU
        vkWaitForFences(vkDevice, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

        // Handle resizing again
        updateSize(result, "failed to present swap chain image!");
        
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
        std::cout << "============================================\n";
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
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
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
            for (int j = 0; j < depthViews.size(); j++) {  
                attachments.push_back(*depthViews[j]);
            }

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = attachments.size();
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = swapChainExtent.width;
            framebufferInfo.height = swapChainExtent.height;
            framebufferInfo.layers = 1;

            VK_CHECK(vkCreateFramebuffer(vkDevice, &framebufferInfo, nullptr, &swapChainFramebuffers[i]));
        }

    }

    void Device::createRenderPass() {
	    RT64_LOG_PRINTF("Render pass creation started");
        // Describe the render pass
        // TODO: Add the rest of the color/depth buffers later on
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapChainImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;        // Not gonna worry about stencil buffers
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

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
        subpass.pDepthStencilAttachment = &depthAttachmentRef;

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
        renderPassInfo.attachmentCount = 2;
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

        vkGetSwapchainImagesKHR(vkDevice, swapChain, &imageCount, nullptr);
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
        int w, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        while (w == 0 || h == 0) {
            glfwGetFramebufferSize(window, &w, &h);
            glfwWaitEvents();
        }

        vkDeviceWaitIdle(vkDevice);

        cleanupSwapChain();

        createSwapChain();
        createImageViews();
        resizeScenes();
        createFramebuffers();
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
        VkWin32SurfaceCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        createInfo.hwnd = glfwGetWin32Window(window);
        createInfo.hinstance = GetModuleHandle(nullptr);
#endif

        VK_CHECK(glfwCreateWindowSurface(vkInstance, window, nullptr, &vkSurface));
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
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
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
            glfwGetFramebufferSize(window, &width, &height);

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
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

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
    void Device::framebufferResizeCallback(GLFWwindow* glfwWindow, int width, int height) {
        auto rt64Device = reinterpret_cast<Device *>(glfwGetWindowUserPointer(glfwWindow));
        rt64Device->framebufferResized = true;
        rt64Device->width = width;
        rt64Device->height = height;
    }
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

        cleanupSwapChain();
        vkDestroySurfaceKHR(vkInstance, vkSurface, nullptr);
        vkDestroyRenderPass(vkDevice, renderPass, nullptr);
        vkDestroyCommandPool(vkDevice, commandPool, nullptr);
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(vkDevice, imageAvailableSemaphores[i], nullptr);
            vkDestroySemaphore(vkDevice, renderFinishedSemaphores[i], nullptr);
            vkDestroyFence(vkDevice, inFlightFences[i], nullptr);
        }

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
            delete sh;
        }
        
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
        vkDestroyShaderModule(vkDevice, composePSModule, nullptr);
        vkDestroySampler(vkDevice, composeSampler, nullptr);

        // Destroy RT pipeline and descriptor set/pool
        vkDestroyPipeline(vkDevice, rtPipeline, nullptr);
        vkDestroyPipelineLayout(vkDevice, rtPipelineLayout, nullptr);
        vkDestroyDescriptorPool(vkDevice, raygenDescriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(vkDevice, raygenDescriptorSetLayout, nullptr);
        // Destroy compose pipeline and descriptor set/pool
        vkDestroyPipeline(vkDevice, rtComposePipeline, nullptr);
        vkDestroyPipelineLayout(vkDevice, rtComposePipelineLayout, nullptr);
        vkDestroyDescriptorPool(vkDevice, rtComposeDescriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(vkDevice, rtComposeDescriptorSetLayout, nullptr);
#endif
        if (enableValidationLayers) {
            DestroyDebugUtilsMessengerEXT(vkInstance, debugMessenger, nullptr);
        }
        vkDestroyDevice(vkDevice, nullptr);
        vkDestroyInstance(vkInstance, nullptr);
        // Destroy the window
        glfwDestroyWindow(window);
        glfwTerminate();
        RT64_LOG_CLOSE();
    }

    // Public

#ifndef RT64_MINIMAL

    /********************** Getters **********************/

    // Returns Vulkan device
    VkDevice& Device::getVkDevice() { return vkDevice; }
    // Returns physical device
    VkPhysicalDevice& Device::getPhysicalDevice() { return physicalDevice; }
    // Returns VMA allocator
    VmaAllocator& Device::getMemAllocator() { return allocator; }
    // Returns RT allocator
	nvvk::ResourceAllocator& Device::getRTAllocator() { return rtAllocator; }

    VkRenderPass& Device::getRenderPass() { return renderPass; };
    VkExtent2D& Device::getSwapchainExtent() { return swapChainExtent; }
	VkViewport& Device::getViewport() { return vkViewport; }
	VkRect2D& Device::getScissors() { return vkScissorRect; }
	int Device::getWidth() { return swapChainExtent.width; }
	int Device::getHeight() { return swapChainExtent.height; }
	double Device::getAspectRatio() { return (double)swapChainExtent.width / (double)swapChainExtent.height; }
	int Device::getCurrentFrameIndex() { return currentFrame; }

	VkCommandBuffer& Device::getCurrentCommandBuffer() { return commandBuffers[currentFrame]; }
    VkFramebuffer& Device::getCurrentSwapchainFramebuffer() { return swapChainFramebuffers[framebufferIndex]; };
    IDxcCompiler* Device::getDxcCompiler() { return d3dDxcCompiler; }
    IDxcLibrary* Device::getDxcLibrary() { return d3dDxcLibrary; }
    VkPipeline& Device::getRTPipeline() { return rtPipeline; }
    VkPipelineLayout& Device::getRTPipelineLayout() { return rtPipelineLayout; }
    VkDescriptorSet& Device::getRayGenDescriptorSet() { return raygenDescriptorSet; }
    std::vector<VkDescriptorSet>& Device::getRTDescriptorSets() { return rtDescriptorSets; }
    VkPipeline& Device::getComposePipeline() { return rtComposePipeline; }
    VkPipelineLayout& Device::getComposePipelineLayout() { return rtComposePipelineLayout; }
    VkDescriptorSet& Device::getComposeDescriptorSet() { return rtComposeDescriptorSet; }
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR Device::getRTProperties() const { return rtProperties; }
    Texture* Device::getBlueNoise() const { return blueNoise; }
    VkSampler& Device::getComposeSampler() { return composeSampler; }
    uint32_t Device::getHitShaderCount() const { return hitShaderCount; }
    uint32_t Device::getRasterShaderCount() const { return rasterShaderCount; }
    VkFence& Device::getCurrentFence() { return inFlightFences[currentFrame]; }

    // Shader getters
    VkPipelineShaderStageCreateInfo Device::getPrimaryShaderStage() const { return primaryRayGenStage; }
    VkPipelineShaderStageCreateInfo Device::getDirectShaderStage() const { return directRayGenStage; }
    VkPipelineShaderStageCreateInfo Device::getIndirectShaderStage() const { return indirectRayGenStage; }
    VkPipelineShaderStageCreateInfo Device::getReflectionShaderStage() const { return reflectionRayGenStage; }
    VkPipelineShaderStageCreateInfo Device::getRefractionShaderStage() const { return refractionRayGenStage; }

    void Device::initRTBuilder(nvvk::RaytracingBuilderKHR& rtBuilder) {
        rtBuilder.setup(vkDevice, &rtAllocator, vkctx.m_queueC);
    }

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

    // Adds a mesh to the device
    void Device::addTexture(Texture* texture) {
        assert(texture != nullptr);
        textures.push_back(texture);
    }
    
    // Removes a mesh from the device
    void Device::removeTexture(Texture* texture) {
        assert(texture != nullptr);
        textures.erase(std::remove(textures.begin(), textures.end(), texture), textures.end());
    }

    // Adds a scene to the device
    void Device::addShader(Shader* shader) {
        assert(shader != nullptr);
        if (shaders.size() > 0 && shaders[firstShaderNullSpot] == nullptr) {
            shaders[firstShaderNullSpot] = shader;
            for (int i = firstShaderNullSpot+1; i < shaders.size(); i++) {
                if (shaders[i] == nullptr) {
                    firstShaderNullSpot = i;
                    break;
                }
            }
        } else {
            shaders.push_back(shader);
        }

        if (shader->hasHitGroups()) {
            hitShaderCount += shader->hitGroupCount();
            overallShaderCount += shader->hitGroupCount();
            rtStateDirty = true;
        }
        if (shader->hasRasterGroup()) {
            overallShaderCount++;
            rasterShaderCount++;
        }
    }

    // Removes a shader from the device
    void Device::removeShader(Shader* shader) {
        assert(shader != nullptr && shaders.size() > 0 );
        if (shaders[shaders.size()-1] == shader) {
            shaders.pop_back();
        } else {
            for (int i = 0; i < shaders.size(); i++) {
                if (shaders[i] == shader) {
                    shaders[i] = nullptr;
                    if (i < firstShaderNullSpot) {
                        firstShaderNullSpot = i;
                    }
                    break;
                }
            }
        }

        if (shader->hasHitGroups()) {
            hitShaderCount -= shader->hitGroupCount();
            overallShaderCount -= shader->hitGroupCount();
            rtStateDirty = true;
        }
        if (shader->hasRasterGroup()) {
            overallShaderCount--;
            rasterShaderCount--;
        }
    }

    uint32_t Device::getFirstAvailableRasterShaderID() const {
        uint32_t id = 0;
        for (int i = 0; i < shaders.size(); i++) {
            if (shaders[i] == nullptr) {
                break;
            } else {
                if (shaders[i]->hasRasterGroup()) {
                    id++;
                } else {
                    break;
                }
            }
        }
        return id;
    }

    uint32_t Device::getFirstAvailableHitDescriptorSetIndex() const {
        uint32_t index = 1;
        for (int i = 0; i < shaders.size(); i++) {
            if (shaders[i] == nullptr) {
                break;
            } else {
                if (shaders[i]->getRTDescriptorSet() != nullptr) {
                    index++;
                } else {
                    break;
                }
            }
        }
        return index;
    }

    uint32_t Device::getFirstAvailableHitShaderID() const {
        uint32_t id = 0;
        for (int i = 0; i < shaders.size(); i++) {
            if (shaders[i] == nullptr) {
                break;
            } else {
                if (shaders[i]->hasHitGroups()) {
                    id += shaders[i]->hitGroupCount();
                } else {
                    break;
                }
            }
        }
        return id;
    }

    void Device::addDepthImageView(VkImageView* depthImageView) {
        assert(depthImageView != nullptr);
        depthViews.push_back(depthImageView);
    }

    void Device::removeDepthImageView(VkImageView* depthImageView) {
        assert(depthImageView != nullptr);
        depthViews.erase(std::remove(depthViews.begin(), depthViews.end(), depthImageView), depthViews.end());
    }

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
        imageInfo.flags = 0; // Optional
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

        image.transitionLayout(commandBuffer, image.getPieplineStage(), newStage, barrier, newLayout);

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

	void Device::allocateDescriptorSet(std::vector<VkDescriptorSetLayoutBinding>& bindings, VkDescriptorBindingFlags& flags, VkDescriptorSetLayout& descriptorSetLayout,  VkDescriptorPool& descriptorPool, VkDescriptorSet& descriptorSet) {
		std::vector<VkDescriptorPoolSize> poolSizes{};
		poolSizes.resize(bindings.size());
		for (int i = 0; i < bindings.size(); i++) {
			poolSizes[i].type = bindings[i].descriptorType;
			poolSizes[i].descriptorCount = bindings[i].descriptorCount;
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

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = poolSizes.size();
        poolInfo.pPoolSizes = poolSizes.data();
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        poolInfo.maxSets = 1;
		VK_CHECK(vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &descriptorPool));

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;
        VK_CHECK(vkAllocateDescriptorSets(vkDevice, &allocInfo, &descriptorSet));
	}
    #endif

}

// Library exports

DLEXPORT RT64_DEVICE* RT64_CreateDevice(void* glfwWindow) {
	try {
		return (RT64_DEVICE*)(new RT64::Device(static_cast<GLFWwindow*>(glfwWindow)));
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

#endif