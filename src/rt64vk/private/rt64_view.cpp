/*
*   RT64VK
*/

#ifndef RT64_MINIMAL

#include "../public/rt64.h"

#include <map>
#include <set>
#include <chrono>

#include "rt64_view.h"
// #include "rt64_dlss.h"
#include "rt64_instance.h"
#include "rt64_mesh.h"
#include "rt64_scene.h"
#include "rt64_shader.h"
// #include "rt64_texture.h"

#include "im3d/im3d.h"
// #include "xxhash/xxhash32.h"

namespace RT64
{
    View::View(Scene* scene) {
        this->scene = scene;
        this->device = scene->getDevice();

        // Init global params data
        globalParamsData.motionBlurStrength = 0.0f;
        globalParamsData.skyPlaneTexIndex = -1;
        globalParamsData.randomSeed = 0;
        globalParamsData.diSamples = 1;
        globalParamsData.giSamples = 1;
        globalParamsData.giBounces = 0;
        globalParamsData.maxLights = 12;
        globalParamsData.motionBlurSamples = 32;
        globalParamsData.tonemapMode = 1;
        globalParamsData.tonemapExposure = 1.0f;
        globalParamsData.tonemapGamma = 1.0f;
        globalParamsData.tonemapWhite = 1.0f;
        globalParamsData.tonemapBlack = 0.0f;
        globalParamsData.visualizationMode = 0;
        globalParamsData.frameCount = 0;
        rtSwap = false;
        rtWidth = 0;
        rtHeight = 0;
        maxReflections = 2;
        // rtUpscaleActive = false;
        // rtRecreateBuffers = false;
        rtSkipReprojection = false;
        resolutionScale = 1.0f;
        denoiserEnabled = false;
        // rtUpscaleMode = UpscaleMode::Bilinear;
        perspectiveControlActive = false;
        perspectiveCanReproject = true;
        im3dVertexCount = 0;
        rtFirstInstanceIdRowWidth = 0;
        rtFirstInstanceIdReadbackUpdated = false;
        skyPlaneTexture = nullptr;
        scissorApplied = false;
        viewportApplied = false;
        device->initRTBuilder(rtBuilder);

        // Create the sky plane sampler
		VkSamplerCreateInfo samplerInfo { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
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
        vkCreateSampler(device->getVkDevice(), &samplerInfo, nullptr, &skyPlaneSampler);

        createOutputBuffers();
        createGlobalParamsBuffer();
	    createFilterParamsBuffer();

	    scene->addView(this);
        device->dirtyDescriptorPool();
    }

    void View::createOutputBuffers() {
        
        // If the image buffers have already been made, then destroy them
        if (imageBuffersInit) {
            destroyOutputBuffers();
        }

        rtSkipReprojection = false;

        int screenWidth = scene->getDevice()->getWidth();
        int screenHeight = scene->getDevice()->getHeight();
        
		rtWidth = lround(screenWidth * resolutionScale);
		rtHeight = lround(screenHeight * resolutionScale);

        globalParamsData.resolution.x = (float)(rtWidth);
        globalParamsData.resolution.y = (float)(rtHeight);
        globalParamsData.resolution.z = (float)(screenWidth);
        globalParamsData.resolution.w = (float)(screenHeight);
        
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.extent.width = screenWidth;
        imageInfo.extent.height = screenHeight;
        imageInfo.extent.depth = 1;
        imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        imageInfo.arrayLayers = 1;
        imageInfo.mipLevels = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        VmaAllocationCreateFlags allocFlags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT;

        // Create image for raster output
        device->allocateImage(&rasterBg, imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);

        // Create buffers for raytracing output.
        imageInfo.extent.width = rtWidth;
        imageInfo.extent.height = rtHeight;
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
	    device->allocateImage(&rtOutput[0], imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        device->allocateImage(&rtOutput[1], imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        device->allocateImage(&rtOutputTonemapped, imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);

        // Shading position
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        device->allocateImage(&rtShadingPosition, imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        device->allocateImage(&rtShadingPositionSecondary, imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);

        // Diffuse
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        device->allocateImage(&rtDiffuse, imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);

        // Normal
        imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        device->allocateImage(&rtNormal[0], imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        device->allocateImage(&rtNormal[1], imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        device->allocateImage(&rtShadingNormal, imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        device->allocateImage(&rtShadingNormalSecondary, imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);

        // Flow
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        imageInfo.format = VK_FORMAT_R16G16_SFLOAT;
        device->allocateImage(&rtFlow, imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);

        // Lock mask
        imageInfo.format = VK_FORMAT_R8_UNORM;
        device->allocateImage(&rtReactiveMask, imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        device->allocateImage(&rtLockMask, imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);

        // Depth buffers
        imageInfo.format = VK_FORMAT_R32_SFLOAT;
        device->allocateImage(&rtDepth[0], imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        device->allocateImage(&rtDepth[1], imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        {
            VkImageCreateInfo depthInfo = imageInfo;
            depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            depthInfo.format = VK_FORMAT_D32_SFLOAT;
            device->allocateImage(&rasterDepth, depthInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        }

        // And everything else
	    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        device->allocateImage(&rtViewDirection, imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        device->allocateImage(&rtShadingSpecular, imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        device->allocateImage(&rtDirectLightAccum[0], imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        device->allocateImage(&rtDirectLightAccum[1], imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        device->allocateImage(&rtIndirectLightAccum[0], imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        device->allocateImage(&rtIndirectLightAccum[1], imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        device->allocateImage(&rtFilteredDirectLight[0], imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        device->allocateImage(&rtFilteredDirectLight[1], imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        device->allocateImage(&rtFilteredIndirectLight[0], imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        device->allocateImage(&rtFilteredIndirectLight[1], imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        device->allocateImage(&rtReflection, imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        device->allocateImage(&rtRefraction, imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        device->allocateImage(&rtTransparent, imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);

        imageInfo.format = VK_FORMAT_R32_SINT;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT| VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        device->allocateImage(&rtInstanceId, imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        device->allocateImage(&rtFirstInstanceId, imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        
        // Create a buffer big enough to read the resource back.
        uint32_t rowPadding;
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        CalculateTextureRowWidthPadding((uint32_t)(imageInfo.extent.width * 4), rtFirstInstanceIdRowWidth, rowPadding);
        device->allocateBuffer( rtFirstInstanceIdRowWidth * imageInfo.extent.height, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, allocFlags, 
            &rtFirstInstanceIdReadback );

        imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        if (rtUpscaleActive) {
            imageInfo.extent.width = screenWidth;
            imageInfo.extent.height = screenHeight;
            device->allocateImage(&rtOutputUpscaled, imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
        }

        // Create the hit buffers
        uint64_t hitCountBufferSizeOne = rtWidth * rtHeight;
        uint64_t hitCountBufferSizeAll = hitCountBufferSizeOne * MAX_QUERIES;
        device->allocateBuffer( hitCountBufferSizeAll * 16, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags,
            &rtHitDistAndFlow );
        device->allocateBuffer( hitCountBufferSizeAll * 4, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags,
            &rtHitColor );
        device->allocateBuffer( hitCountBufferSizeAll * 8, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags,
            &rtHitNormal );
        device->allocateBuffer( hitCountBufferSizeAll * 4, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags,
            &rtHitSpecular );
        device->allocateBuffer( hitCountBufferSizeAll * 2, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags,
            &rtHitInstanceId );
        
        // Now transition every image into a new layout
        AllocatedImage* transRightsBitch[] = {
            &rasterBg,
            &rtOutput[0], &rtOutput[1],
            &rtOutputTonemapped,
            &rtViewDirection,
            &rtShadingPosition,
            &rtShadingPositionSecondary,
            &rtShadingNormal,
            &rtShadingNormalSecondary,
            &rtShadingSpecular,
            &rtDiffuse,
            &rtInstanceId,
            // &rtFirstInstanceId,
            // AllocatedBuffer &rtFirstInstanceIdReadback,
            &rtDirectLightAccum[0], &rtDirectLightAccum[1],
            &rtFilteredDirectLight[0], &rtFilteredDirectLight[1],
            &rtIndirectLightAccum[0], &rtIndirectLightAccum[1],
            &rtFilteredIndirectLight[0], &rtFilteredIndirectLight[1],
            &rtReflection,
            &rtRefraction,
            &rtTransparent,
            &rtFlow,
            &rtReactiveMask,
            &rtLockMask,
            &rtNormal[0], &rtNormal[1],
        };
        device->transitionImageLayout(transRightsBitch, sizeof(transRightsBitch) / sizeof(AllocatedImage*), 
            VK_IMAGE_LAYOUT_GENERAL, 
            VK_ACCESS_NONE, 
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
            nullptr);
        if (rtUpscaleActive) {
            device->transitionImageLayout(rtOutputUpscaled, 
                VK_IMAGE_LAYOUT_GENERAL, 
                VK_ACCESS_NONE, 
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
                nullptr);
        }

        // Memory barrier for the hit buffers
        VkCommandBuffer* cmd = device->beginSingleTimeCommands();
        device->bufferMemoryBarrier(rtHitDistAndFlow, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, cmd);
        device->bufferMemoryBarrier(rtHitColor, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, cmd);
        device->bufferMemoryBarrier(rtHitNormal, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, cmd);
        device->bufferMemoryBarrier(rtHitSpecular, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, cmd);
        device->bufferMemoryBarrier(rtHitInstanceId, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, cmd);
        device->endSingleTimeCommands(cmd);

#ifndef NDEBUG
        rasterBg.setAllocationName("rasterBg");
        rtOutput[0].setAllocationName("rtOutput[0]");
        rtOutput[1].setAllocationName("rtOutput[1]");
        rtOutputTonemapped.setAllocationName("rtOutputTonemapped");
        rtViewDirection.setAllocationName("rtViewDirection");
        rtShadingPosition.setAllocationName("rtShadingPosition");
        rtShadingPositionSecondary.setAllocationName("rtShadingPositionSecondary");
        rtShadingNormal.setAllocationName("rtShadingNormal");
        rtShadingNormalSecondary.setAllocationName("rtShadingNormalSecondary");
        rtShadingSpecular.setAllocationName("rtShadingSpecular");
        rtDiffuse.setAllocationName("rtDiffuse");
        rtNormal[0].setAllocationName("rtNormal[0]");
        rtNormal[1].setAllocationName("rtNormal[1]");
        rtInstanceId.setAllocationName("rtInstanceId");
        rtFirstInstanceId.setAllocationName("rtFirstInstanceId");
        rtFirstInstanceIdReadback.setAllocationName("rtFirstInstanceIdReadback");
        rtDirectLightAccum[0].setAllocationName("rtDirectLightAccum[0]");
        rtDirectLightAccum[1].setAllocationName("rtDirectLightAccum[1]");
        rtIndirectLightAccum[0].setAllocationName("rtIndirectLightAccum[0]");
        rtIndirectLightAccum[1].setAllocationName("rtIndirectLightAccum[1]");
        rtFilteredDirectLight[0].setAllocationName("rtFilteredDirectLight[0]");
        rtFilteredDirectLight[1].setAllocationName("rtFilteredDirectLight[1]");
        rtFilteredIndirectLight[0].setAllocationName("rtFilteredIndirectLight[0]");
        rtFilteredIndirectLight[1].setAllocationName("rtFilteredIndirectLight[1]");
        rtReflection.setAllocationName("rtReflection");
        rtRefraction.setAllocationName("rtRefraction");
        rtTransparent.setAllocationName("rtTransparent");
        rtFlow.setAllocationName("rtFlow");
        rtReactiveMask.setAllocationName("rtReactiveMask");
        rtLockMask.setAllocationName("rtLockMask");
        rtDepth[0].setAllocationName("rtDepth[0]");
        rtDepth[1].setAllocationName("rtDepth[1]");
        rasterDepth.setAllocationName("rasterDepth");
        rtHitDistAndFlow.setAllocationName("rtHitDistAndFlow");
        rtHitColor.setAllocationName("rtHitColor");
        rtHitNormal.setAllocationName("rtHitNormal");
        rtHitSpecular.setAllocationName("rtHitSpecular");
        rtHitInstanceId.setAllocationName("rtHitInstanceId");
        rtOutputUpscaled.setAllocationName("rtOutputUpscaled");
#endif
        // Create the image/buffer views
        rasterBg.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtOutput[0].createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtOutput[1].createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtOutputTonemapped.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtViewDirection.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtShadingPosition.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtShadingPositionSecondary.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtShadingNormal.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtShadingNormalSecondary.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtShadingSpecular.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtDiffuse.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtNormal[0].createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtNormal[1].createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtInstanceId.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtFirstInstanceId.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtDirectLightAccum[0].createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtDirectLightAccum[1].createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtIndirectLightAccum[0].createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtIndirectLightAccum[1].createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtFilteredDirectLight[0].createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtFilteredDirectLight[1].createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtFilteredIndirectLight[0].createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtFilteredIndirectLight[1].createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtReflection.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtRefraction.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtTransparent.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtFlow.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtReactiveMask.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtLockMask.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtDepth[0].createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rtDepth[1].createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
        rasterDepth.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT);
        rtHitDistAndFlow.createBufferView(VK_FORMAT_R32G32B32A32_SFLOAT);
        rtHitColor.createBufferView(VK_FORMAT_R8G8B8A8_UNORM);
        rtHitNormal.createBufferView(VK_FORMAT_R16G16B16A16_SNORM);
        rtHitSpecular.createBufferView(VK_FORMAT_R8G8B8A8_UNORM);
        rtHitInstanceId.createBufferView(VK_FORMAT_R16_UINT);
        rtOutputUpscaled.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);

        // Create the render pass for the raster diffuse framebuffer
        device->createRenderPass(rasterPass, true, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // Create the framebuffers needed for rendering
        device->createFramebuffer(rtOutputFB[0], device->getOffscreenRenderPass(), rtOutput[0].getImageView(), nullptr, {(unsigned int)rtWidth, (unsigned int)rtHeight});
        device->createFramebuffer(rtOutputFB[1], device->getOffscreenRenderPass(), rtOutput[1].getImageView(), nullptr, {(unsigned int)rtWidth, (unsigned int)rtHeight});
        device->createFramebuffer(rtOutputTonemappedFB, device->getOffscreenRenderPass(), rtOutputTonemapped.getImageView(), nullptr, {(unsigned int)rtWidth, (unsigned int)rtHeight});
        device->createFramebuffer(diffuseFB, rasterPass, rtDiffuse.getImageView(), &rasterDepth.getImageView(), {(unsigned int)rtWidth, (unsigned int)rtHeight});

        imageBuffersInit = true;
    }

    void View::destroyOutputBuffers() {
        rasterBg.destroyResource();
        rtOutput[0].destroyResource();
        rtOutput[1].destroyResource();
        rtOutputTonemapped.destroyResource();
        rtViewDirection.destroyResource();
        rtShadingPosition.destroyResource();
        rtShadingPositionSecondary.destroyResource();
        rtShadingNormal.destroyResource();
        rtShadingNormalSecondary.destroyResource();
        rtShadingSpecular.destroyResource();
        rtDiffuse.destroyResource();
        rtNormal[0].destroyResource();
        rtNormal[1].destroyResource();
        rtInstanceId.destroyResource();
        rtFirstInstanceId.destroyResource();
        rtFirstInstanceIdReadback.destroyResource();
        rtDirectLightAccum[0].destroyResource();
        rtDirectLightAccum[1].destroyResource();
        rtIndirectLightAccum[0].destroyResource();
        rtIndirectLightAccum[1].destroyResource();
        rtFilteredDirectLight[0].destroyResource();
        rtFilteredDirectLight[1].destroyResource();
        rtFilteredIndirectLight[0].destroyResource();
        rtFilteredIndirectLight[1].destroyResource();
        rtReflection.destroyResource();
        rtRefraction.destroyResource();
        rtTransparent.destroyResource();
        rtFlow.destroyResource();
        rtReactiveMask.destroyResource();
        rtLockMask.destroyResource();
        rtDepth[0].destroyResource();
        rtDepth[1].destroyResource();
        rasterDepth.destroyResource();
        rtHitDistAndFlow.destroyResource();
        rtHitColor.destroyResource();
        rtHitNormal.destroyResource();
        rtHitSpecular.destroyResource();
        rtHitInstanceId.destroyResource();
        rtOutputUpscaled.destroyResource();

        // Destroy the render pass
        vkDestroyRenderPass(device->getVkDevice(), rasterPass, nullptr);

        // Destroy the raytraced framebuffers
        vkDestroyFramebuffer(device->getVkDevice(), rtOutputFB[0], nullptr);
        vkDestroyFramebuffer(device->getVkDevice(), rtOutputFB[1], nullptr);
        vkDestroyFramebuffer(device->getVkDevice(), rtOutputTonemappedFB, nullptr);
        vkDestroyFramebuffer(device->getVkDevice(), diffuseFB, nullptr);
    }

    View::~View() {
        scene->removeView(this);

        destroyOutputBuffers();
        globalParamsBuffer.destroyResource();
        filterParamsBuffer.destroyResource();
        activeInstancesBufferMaterials.destroyResource();
        activeInstancesBufferTransforms.destroyResource();
        shaderBindingTable.destroyResource();
        im3dVertexBuffer.destroyResource();
        vkDestroySampler(device->getVkDevice(), skyPlaneSampler, nullptr);
        vkFreeDescriptorSets(device->getVkDevice(), device->getDescriptorPool(), 2, indirectFilterDescriptorSets);

        rtBuilder.destroyTlas();
    }

    void View::createGlobalParamsBuffer() {
        globalParamsSize = ROUND_UP(sizeof(GlobalParams), CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        device->allocateBuffer(
            globalParamsSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
            VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            &globalParamsBuffer
        );
    }

    void View::updateGlobalParamsBuffer() {
        // Update with the latest scene description.
        RT64_SCENE_DESC desc = scene->getDescription();
        globalParamsData.ambientBaseColor = ToVector4(desc.ambientBaseColor, 0.0f);
        globalParamsData.ambientNoGIColor = ToVector4(desc.ambientNoGIColor, 0.0f);
        globalParamsData.eyeLightDiffuseColor = ToVector4(desc.eyeLightDiffuseColor, 0.0f);
        globalParamsData.eyeLightSpecularColor = ToVector4(desc.eyeLightSpecularColor, 0.0f);
        globalParamsData.skyDiffuseMultiplier = ToVector4(desc.skyDiffuseMultiplier, 0.0f);
        globalParamsData.skyHSLModifier = ToVector4(desc.skyHSLModifier, 0.0f);
        globalParamsData.skyYawOffset = desc.skyYawOffset;
        globalParamsData.giDiffuseStrength = desc.giDiffuseStrength;
        globalParamsData.giSkyStrength = desc.giSkyStrength;

        // Previous and current view and projection matrices and their inverse.
        if (perspectiveCanReproject) {
            globalParamsData.prevViewI = globalParamsData.viewI;
            globalParamsData.prevViewProj = globalParamsData.viewProj;
        }

        globalParamsData.viewI = glm::inverse(globalParamsData.view);
        globalParamsData.projectionI = glm::inverse(globalParamsData.projection);
        globalParamsData.viewProj = globalParamsData.projection * globalParamsData.view;

        if (!perspectiveCanReproject) {
            globalParamsData.prevViewI = globalParamsData.viewI;
            globalParamsData.prevViewProj = globalParamsData.viewProj;
        }

        // Pinhole camera vectors to generate non-normalized ray direction.
        // TODO: Make a fake target and focal distance at the midpoint of the near/far planes
        // until the game sends that data in some way in the future.
        const float FocalDistance = (nearDist + farDist) / 2.0f;
        const float AspectRatio = scene->getDevice()->getAspectRatio();
        const RT64_VECTOR3 Up = { 0.0f, 1.0f, 0.0f };
        const RT64_VECTOR3 Pos = getViewPosition();
        const RT64_VECTOR3 Target = Pos + getViewDirection() * FocalDistance;
        RT64_VECTOR3 cameraW = Normalize(Target - Pos) * FocalDistance;
        RT64_VECTOR3 cameraU = Normalize(Cross(cameraW, Up));
        RT64_VECTOR3 cameraV = Normalize(Cross(cameraU, cameraW));
        const float ulen = FocalDistance * std::tan(fovRadians * 0.5f) * AspectRatio;
        const float vlen = FocalDistance * std::tan(fovRadians * 0.5f);
        cameraU = cameraU * ulen;
        cameraV = cameraV * vlen;
        globalParamsData.cameraU = ToVector4(cameraU, 0.0f);
        globalParamsData.cameraV = ToVector4(cameraV, 0.0f);
        globalParamsData.cameraW = ToVector4(cameraW, 0.0f);

	// Enable light reprojection if denoising is enabled.
#ifdef DI_REPROJECTION_SUPPORT
	    globalParamsData.diReproject = !rtSkipReprojection && denoiserEnabled && (globalParamsBufferData.diSamples > 0) ? 1 : 0;
#else
	    globalParamsData.diReproject = 0;
#endif
        globalParamsData.giReproject = !rtSkipReprojection && denoiserEnabled && (globalParamsData.giSamples > 0) ? 1 : 0;
        // globalParamsData.binaryLockMask = (rtUpscaleMode != UpscaleMode::FSR);

        // Use the total frame count as the random seed.
        globalParamsData.randomSeed = globalParamsData.frameCount;

        globalParamsBuffer.setData(&globalParamsData, sizeof(globalParamsData));
    }

    void View::createInstanceTransformsBuffer() {
        uint32_t totalInstances = static_cast<uint32_t>(rtInstances.size() + rasterBgInstances.size() + rasterFgInstances.size() + rasterUiInstances.size());
        uint32_t newBufferSize = totalInstances * sizeof(InstanceTransforms);
        if (activeInstancesBufferTransformsSize != newBufferSize) {
            activeInstancesBufferTransforms.destroyResource();
            scene->getDevice()->allocateBuffer(
                newBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                &activeInstancesBufferTransforms
            );
            activeInstancesBufferTransformsSize = newBufferSize;
        }
    }

    void View::updateInstanceTransformsBuffer() {
        InstanceTransforms* current = nullptr;
        activeInstancesBufferTransforms.mapMemory(reinterpret_cast<void**>(&current));
        // D3D12_CHECK(activeInstancesBufferTransforms.Get()->Map(0, &readRange, reinterpret_cast<void **>(&current)));

        auto storeTransforms = [this, &current](const RenderInstance& inst) {
            // Store world transform.
            current->objectToWorld = convertNVMATHtoGLMMatrix(inst.transform);
            current->objectToWorldPrevious = convertNVMATHtoGLMMatrix(inst.transformPrevious);

            // Store matrix to transform normal.
            glm::mat4 upper3x3 = current->objectToWorld;
            upper3x3[0][3] = 0.f;
            upper3x3[1][3] = 0.f;
            upper3x3[2][3] = 0.f;
            upper3x3[3][0] = 0.f;
            upper3x3[3][1] = 0.f;
            upper3x3[3][2] = 0.f;
            upper3x3[3][3] = 1.f;

            current->objectToWorldNormal = glm::transpose(glm::inverse(upper3x3));
        };

        // Store the transforms
        for (const RenderInstance &inst : rtInstances) {
            storeTransforms(inst);
            current++;
        } 
        for (const RenderInstance &inst : rasterBgInstances) {
            storeTransforms(inst);
            current++;
        } 
        for (const RenderInstance &inst : rasterFgInstances) {
            storeTransforms(inst);
            current++;
        } 
        for (const RenderInstance &inst : rasterUiInstances) {
            storeTransforms(inst);
            current++;
        } 

        activeInstancesBufferTransforms.unmapMemory();
    }

    void View::createInstanceMaterialsBuffer() {
        uint32_t totalInstances = static_cast<uint32_t>(rtInstances.size() + rasterBgInstances.size() + rasterFgInstances.size());
        uint32_t newBufferSize = totalInstances * sizeof(RT64_MATERIAL);
        if (activeInstancesBufferMaterialsSize != newBufferSize) {
            activeInstancesBufferMaterials.destroyResource();
            device->allocateBuffer(
                newBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                &activeInstancesBufferMaterials
            );
            activeInstancesBufferMaterialsSize = newBufferSize;
        }
    }

    void View::updateInstanceMaterialsBuffer() {
        RT64_MATERIAL* current = nullptr;
        void* pData = activeInstancesBufferMaterials.mapMemory(reinterpret_cast<void**>(&current));

        for (const RenderInstance& inst : rtInstances) {
            *current = inst.material;
            current++;
        }

        for (const RenderInstance& inst : rasterBgInstances) {
            *current = inst.material;
            current++;
        } 

        for (const RenderInstance& inst : rasterFgInstances) {
            *current = inst.material;
            current++;
        }

        for (const RenderInstance& inst : rasterUiInstances) {
            *current = inst.material;
            current++;
        }

        activeInstancesBufferMaterials.unmapMemory();
    }

    struct alignas(16) FilterCB {
        uint32_t TextureSize[2];
        glm::vec2 TexelSize;
    };

    void View::createFilterParamsBuffer() {
        filterParamsSize = sizeof(FilterCB);
        device->allocateBuffer(filterParamsSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
            VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            &filterParamsBuffer
        );
    }

    void View::updateFilterParamsBuffer() {
        FilterCB cb;
        cb.TextureSize[0] = rtWidth;
        cb.TextureSize[1] = rtHeight;
        cb.TexelSize.x = 1.0f / cb.TextureSize[0];
        cb.TexelSize.y = 1.0f / cb.TextureSize[1];

        filterParamsBuffer.setData(&cb, sizeof(FilterCB));
    }

    void View::updateShaderDescriptorSets(bool updateDescriptors) { 
	    assert(usedTextures.size() <= SRV_TEXTURES_MAX);
        std::vector<VkWriteDescriptorSet> descriptorWrites;

        // Create the descriptor infos for the textures;
        std::vector<VkDescriptorImageInfo> texture_infos;
        texture_infos.resize(usedTextures.size());
        for (int i = 0; i < usedTextures.size(); i++) {
            texture_infos[i] = usedTextures[i]->getTexture().getDescriptorInfo();
            usedTextures[i]->setCurrentIndex(-1);
        }

        // Update the descriptor sets for the raygen shaders
        {
            VkDescriptorSet& descriptorSet = device->getRayGenDescriptorSet();
            // The "UAVs"
            VkWriteDescriptorSet write {};
            descriptorWrites.push_back(rtViewDirection.generateDescriptorWrite(1, UAV_INDEX(gViewDirection) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtShadingPosition.generateDescriptorWrite(1, UAV_INDEX(gShadingPosition) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtShadingNormal.generateDescriptorWrite(1, UAV_INDEX(gShadingNormal) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtShadingSpecular.generateDescriptorWrite(1, UAV_INDEX(gShadingSpecular) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtDiffuse.generateDescriptorWrite(1, UAV_INDEX(gDiffuse) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtInstanceId.generateDescriptorWrite(1, UAV_INDEX(gInstanceId) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtDirectLightAccum[rtSwap ? 1 : 0].generateDescriptorWrite(1, UAV_INDEX(gDirectLightAccum) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtIndirectLightAccum[rtSwap ? 1 : 0].generateDescriptorWrite(1, UAV_INDEX(gIndirectLightAccum) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtReflection.generateDescriptorWrite(1, UAV_INDEX(gReflection) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtRefraction.generateDescriptorWrite(1, UAV_INDEX(gRefraction) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtTransparent.generateDescriptorWrite(1, UAV_INDEX(gTransparent) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtFlow.generateDescriptorWrite(1, UAV_INDEX(gFlow) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtReactiveMask.generateDescriptorWrite(1, UAV_INDEX(gReactiveMask) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtLockMask.generateDescriptorWrite(1, UAV_INDEX(gLockMask) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtNormal[rtSwap ? 1 : 0].generateDescriptorWrite(1, UAV_INDEX(gNormal) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtDepth[rtSwap ? 1 : 0].generateDescriptorWrite(1, UAV_INDEX(gDepth) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtNormal[rtSwap ? 0 : 1].generateDescriptorWrite(1, UAV_INDEX(gPrevNormal) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtDepth[rtSwap ? 0 : 1].generateDescriptorWrite(1, UAV_INDEX(gPrevDepth) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtDirectLightAccum[rtSwap ? 0 : 1].generateDescriptorWrite(1, UAV_INDEX(gPrevDirectLightAccum) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtIndirectLightAccum[rtSwap ? 0 : 1].generateDescriptorWrite(1, UAV_INDEX(gPrevIndirectLightAccum) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtFilteredDirectLight[1].generateDescriptorWrite(1, UAV_INDEX(gFilteredDirectLight) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtFilteredIndirectLight[1].generateDescriptorWrite(1, UAV_INDEX(gFilteredIndirectLight) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtHitDistAndFlow.generateDescriptorWrite(1, UAV_INDEX(gHitDistAndFlow) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, descriptorSet));
            descriptorWrites.push_back(rtHitColor.generateDescriptorWrite(1, UAV_INDEX(gHitColor) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, descriptorSet));
            descriptorWrites.push_back(rtHitNormal.generateDescriptorWrite(1, UAV_INDEX(gHitNormal) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, descriptorSet));
            descriptorWrites.push_back(rtHitSpecular.generateDescriptorWrite(1, UAV_INDEX(gHitSpecular) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, descriptorSet));
            descriptorWrites.push_back(rtHitInstanceId.generateDescriptorWrite(1, UAV_INDEX(gHitInstanceId) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, descriptorSet));
            
            // The "SRVs"
            descriptorWrites.push_back(rasterBg.generateDescriptorWrite(1, SRV_INDEX(gBackground) + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, descriptorSet));

            // The top level AS
            VkWriteDescriptorSet tlasWrite {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            VkAccelerationStructureKHR tlas = rtBuilder.getAccelerationStructure();
            VkWriteDescriptorSetAccelerationStructureKHR tlas_INFO {
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
                nullptr, 1, &tlas
            };
            tlasWrite.descriptorCount = 1;
            tlasWrite.dstBinding = SRV_INDEX(SceneBVH) + SRV_SHIFT;
            tlasWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            tlasWrite.pNext = &tlas_INFO;
            tlasWrite.dstSet = descriptorSet;
            descriptorWrites.push_back(tlasWrite);

            if (scene->getLightsCount() > 0) {
                descriptorWrites.push_back(scene->getLightsBuffer().generateDescriptorWrite(1, SRV_INDEX(SceneLights) + SRV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet));
            }
            descriptorWrites.push_back(activeInstancesBufferTransforms.generateDescriptorWrite(1, SRV_INDEX(instanceTransforms) + SRV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet));
            descriptorWrites.push_back(activeInstancesBufferMaterials.generateDescriptorWrite(1, SRV_INDEX(instanceMaterials) + SRV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet));
            descriptorWrites.push_back(device->getBlueNoise()->getTexture().generateDescriptorWrite(1, SRV_INDEX(gBlueNoise) + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, descriptorSet));
            
            // Add the textures
            VkWriteDescriptorSet textureWrite {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            textureWrite.descriptorCount = texture_infos.size();
            textureWrite.dstBinding = SRV_INDEX(gTextures) + SRV_SHIFT;
            textureWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            textureWrite.pImageInfo = texture_infos.data();
            textureWrite.dstSet = descriptorSet;
            descriptorWrites.push_back(textureWrite);

            // Add the background samplers
            VkDescriptorImageInfo samplerInfo { };
            samplerInfo.sampler = skyPlaneSampler;
            VkWriteDescriptorSet samplerWrite { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            samplerWrite.descriptorCount = 1;
            samplerWrite.dstSet = descriptorSet;
            samplerWrite.dstBinding = 0 + SAMPLER_SHIFT;
            samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            samplerWrite.pImageInfo = &samplerInfo;
            descriptorWrites.push_back(samplerWrite);
            samplerWrite.dstBinding = 1 + SAMPLER_SHIFT;
            descriptorWrites.push_back(samplerWrite);

            // Add the globalParamsBuffer
            descriptorWrites.push_back(globalParamsBuffer.generateDescriptorWrite(1, CBV_INDEX(gParams) + CBV_SHIFT, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, descriptorSet));

            // Write to the raygen descriptor set
            vkUpdateDescriptorSets(device->getVkDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
            descriptorWrites.clear();
        }

        // A function to bind descriptors to the raster descriptor sets
        auto writeRasterDescriptors = [this, texture_infos](Shader* shader, VkDescriptorSet& descriptorSet, std::vector<VkWriteDescriptorSet>& descriptorWrites) {
            // Only bind the global params buffer if the rasterizer is capable of 3D transforms
            if (shader->has3DRaster()) {
                descriptorWrites.push_back(globalParamsBuffer.generateDescriptorWrite(1, CBV_INDEX(gParams) + CBV_SHIFT, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, descriptorSet));
            }
            // First, allocate the shader's descriptor set
            // shader->allocateRasterDescriptorSet();

            descriptorWrites.push_back(activeInstancesBufferTransforms.generateDescriptorWrite(1, SRV_INDEX(instanceTransforms) + SRV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet));
            descriptorWrites.push_back(activeInstancesBufferMaterials.generateDescriptorWrite(1, SRV_INDEX(instanceMaterials) + SRV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet));

            // Add the textures
            VkWriteDescriptorSet textureWrite {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            textureWrite.descriptorCount = texture_infos.size();
            textureWrite.dstBinding = SRV_INDEX(gTextures) + SRV_SHIFT;
            textureWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            textureWrite.pImageInfo = texture_infos.data();
            textureWrite.dstSet = descriptorSet;
            descriptorWrites.push_back(textureWrite);

            // Add the texture sampler
            VkDescriptorImageInfo samplerInfo { };
            samplerInfo.sampler = shader->getSampler();
            VkWriteDescriptorSet samplerWrite { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            samplerWrite.descriptorCount = 1;
            samplerWrite.dstSet = descriptorSet;
            samplerWrite.dstBinding = shader->getSamplerRegisterIndex() + SAMPLER_SHIFT;
            samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            samplerWrite.pImageInfo = &samplerInfo;
            descriptorWrites.push_back(samplerWrite);

            // Update descriptor sets
            vkUpdateDescriptorSets(device->getVkDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
            descriptorWrites.clear();
        };

        // Update the raster instance descriptor sets
        std::unordered_set<Shader*> usedShaders;
        // Background instances
        for (int i = 0; i < rasterBgInstances.size(); i++) {
            Shader* shader = rasterBgInstances[i].shader;
            if (usedShaders.contains(shader)) { continue; }
            writeRasterDescriptors(shader, shader->getRasterGroup().descriptorSet, descriptorWrites);
            usedShaders.emplace(shader);
        }
        usedShaders.clear();

        // Foreground instances
        for (int i = 0; i < rasterFgInstances.size(); i++) {
            Shader* shader = rasterFgInstances[i].shader;
            if (usedShaders.contains(shader)) { continue; }
            writeRasterDescriptors(shader, shader->getRasterGroup().descriptorSet, descriptorWrites);
            usedShaders.emplace(shader);
        }
        usedShaders.clear();

        // UI instances
        for (int i = 0; i < rasterUiInstances.size(); i++) {
            Shader* shader = rasterUiInstances[i].shader;
            if (usedShaders.contains(shader)) { continue; }
            writeRasterDescriptors(shader, shader->getRasterGroup().descriptorSet, descriptorWrites);
            usedShaders.emplace(shader);
        }
        usedShaders.clear();

        // And the RT Instances' descriptor sets
        for (int i = 0; i < rtInstances.size(); i++) {
            Shader* shader = rtInstances[i].shader;
            if (usedShaders.contains(shader)) {
                continue;
            }

            VkDescriptorSet& descriptorSet = shader->getRTDescriptorSet();
            if (shader->getSurfaceHitGroup().shaderModule != nullptr) {
                descriptorWrites.push_back(rtHitDistAndFlow.generateDescriptorWrite(1, UAV_INDEX(gHitDistAndFlow) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, descriptorSet));
                descriptorWrites.push_back(rtHitColor.generateDescriptorWrite(1, UAV_INDEX(gHitColor) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, descriptorSet));
                descriptorWrites.push_back(rtHitNormal.generateDescriptorWrite(1, UAV_INDEX(gHitNormal) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, descriptorSet));
                descriptorWrites.push_back(rtHitSpecular.generateDescriptorWrite(1, UAV_INDEX(gHitSpecular) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, descriptorSet));
                descriptorWrites.push_back(rtHitInstanceId.generateDescriptorWrite(1, UAV_INDEX(gHitInstanceId) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, descriptorSet));
            }
            descriptorWrites.push_back(activeInstancesBufferTransforms.generateDescriptorWrite(1, SRV_INDEX(instanceTransforms) + SRV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet));
            descriptorWrites.push_back(activeInstancesBufferMaterials.generateDescriptorWrite(1, SRV_INDEX(instanceMaterials) + SRV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet));

            // Add the textures
            VkWriteDescriptorSet textureWrite {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            textureWrite.descriptorCount = texture_infos.size();
            textureWrite.dstBinding = SRV_INDEX(gTextures) + SRV_SHIFT;
            textureWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            textureWrite.pImageInfo = texture_infos.data();
            textureWrite.dstSet = descriptorSet;
            descriptorWrites.push_back(textureWrite);

            // Add the texture sampler
            VkDescriptorImageInfo samplerInfo { };
            samplerInfo.sampler = shader->getSampler();
            VkWriteDescriptorSet samplerWrite { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            samplerWrite.descriptorCount = 1;
            samplerWrite.dstSet = descriptorSet;
            samplerWrite.dstBinding = shader->getSamplerRegisterIndex() + SAMPLER_SHIFT;
            samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            samplerWrite.pImageInfo = &samplerInfo;
            descriptorWrites.push_back(samplerWrite);

            // Write to the instances' descriptor sets
            vkUpdateDescriptorSets(device->getVkDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
            descriptorWrites.clear();
            usedShaders.emplace(shader);
        }
        usedShaders.clear();

        // Update the descriptor sest for the indirect filter
        for (int i = 0; i < 2; i++)
        {
            VkDescriptorSet& descriptorSet = indirectFilterDescriptorSets[i];
            
            descriptorWrites.push_back(rtFilteredIndirectLight[i ? 1 : 0].generateDescriptorWrite(1, 0 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtFilteredIndirectLight[i ? 0 : 1].generateDescriptorWrite(1, 0 + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(filterParamsBuffer.generateDescriptorWrite(1, 0 + CBV_SHIFT, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, descriptorSet));

            // Add the texture sampler
            VkDescriptorImageInfo samplerInfo { };
            samplerInfo.sampler = device->getGaussianSampler();
            VkWriteDescriptorSet samplerWrite { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            samplerWrite.descriptorCount = 1;
            samplerWrite.dstSet = descriptorSet;
            samplerWrite.dstBinding = 0 + SAMPLER_SHIFT;
            samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            samplerWrite.pImageInfo = &samplerInfo;
            descriptorWrites.push_back(samplerWrite);

            // Write to the instances' descriptor sets
            vkUpdateDescriptorSets(device->getVkDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
            descriptorWrites.clear();
        }

        // Update the descriptor set for the compose shader
        {
            VkDescriptorSet& descriptorSet = device->getComposeDescriptorSet();
            descriptorWrites.push_back(rtFlow.generateDescriptorWrite(1, 0 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtDiffuse.generateDescriptorWrite(1, 1 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtFilteredDirectLight[1].generateDescriptorWrite(1, 2 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtFilteredIndirectLight[1].generateDescriptorWrite(1, 3 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, descriptorSet));
            // descriptorWrites.push_back(rtDirectLightAccum[rtSwap ? 1 : 0].generateDescriptorWrite(1, 2 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, descriptorSet));
            // descriptorWrites.push_back(rtIndirectLightAccum[rtSwap ? 1 : 0].generateDescriptorWrite(1, 3 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtReflection.generateDescriptorWrite(1, 4 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtRefraction.generateDescriptorWrite(1, 5 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtTransparent.generateDescriptorWrite(1, 6 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, descriptorSet));
            descriptorWrites.push_back(globalParamsBuffer.generateDescriptorWrite(1, CBV_INDEX(gParams) + CBV_SHIFT, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, descriptorSet));

            // Add the compose sampler
            VkDescriptorImageInfo samplerInfo { };
            samplerInfo.sampler = device->getComposeSampler();
            VkWriteDescriptorSet samplerWrite { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            samplerWrite.descriptorCount = 1;
            samplerWrite.dstBinding = 0 + SAMPLER_SHIFT;
            samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            samplerWrite.pImageInfo = &samplerInfo;
            samplerWrite.dstSet = descriptorSet;
            descriptorWrites.push_back(samplerWrite);

            // Write to the compose descriptor sets
            vkUpdateDescriptorSets(device->getVkDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
            descriptorWrites.clear();
        }

        // Update the tonemapping descriptor set
        {
            VkDescriptorSet& descriptorSet = device->getTonemappingDescriptorSet();
            // if (rtUpscaleActive) {
            //  pretend there's something here
            // } else {
                descriptorWrites.push_back(rtOutput[rtSwap ? 1 : 0].generateDescriptorWrite(1, 0 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, descriptorSet));
            // }
            descriptorWrites.push_back(globalParamsBuffer.generateDescriptorWrite(1, CBV_INDEX(gParams) + CBV_SHIFT, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, descriptorSet));

            // Add the tonemapping sampler
            VkDescriptorImageInfo samplerInfo { };
            samplerInfo.sampler = device->getTonemappingSampler();
            VkWriteDescriptorSet samplerWrite { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            samplerWrite.descriptorCount = 1;
            samplerWrite.dstBinding = 0 + SAMPLER_SHIFT;
            samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            samplerWrite.pImageInfo = &samplerInfo;
            samplerWrite.dstSet = descriptorSet;
            descriptorWrites.push_back(samplerWrite);

            // Write to the tonemapping descriptor set
            vkUpdateDescriptorSets(device->getVkDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
            descriptorWrites.clear();
        }

        // Update the post process descriptor set
        {
            VkDescriptorSet& descriptorSet = device->getPostProcessDescriptorSet();
            // if (rtUpscaleActive) {
            //  pretend there's something here
            // } else {
                descriptorWrites.push_back(rtOutputTonemapped.generateDescriptorWrite(1, 0 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, descriptorSet));
            // }
            descriptorWrites.push_back(rtFlow.generateDescriptorWrite(1, 1 + SRV_SHIFT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, descriptorSet));
            descriptorWrites.push_back(globalParamsBuffer.generateDescriptorWrite(1, CBV_INDEX(gParams) + CBV_SHIFT, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, descriptorSet));

            // Add the post process sampler
            VkDescriptorImageInfo samplerInfo { };
            samplerInfo.sampler = device->getPostProcessSampler();
            VkWriteDescriptorSet samplerWrite { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            samplerWrite.descriptorCount = 1;
            samplerWrite.dstBinding = 0 + SAMPLER_SHIFT;
            samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            samplerWrite.pImageInfo = &samplerInfo;
            samplerWrite.dstSet = descriptorSet;
            descriptorWrites.push_back(samplerWrite);

            // Write to the post process descriptor set
            vkUpdateDescriptorSets(device->getVkDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
            descriptorWrites.clear();
        }

        // Update the debug descriptor set
        {
            VkDescriptorSet& descriptorSet = device->getDebugDescriptorSet();
            // The "UAVs"
            VkWriteDescriptorSet write {};
            descriptorWrites.push_back(rtShadingPosition.generateDescriptorWrite(1, UAV_INDEX(gShadingPosition) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtShadingNormal.generateDescriptorWrite(1, UAV_INDEX(gShadingNormal) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtShadingSpecular.generateDescriptorWrite(1, UAV_INDEX(gShadingSpecular) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtDiffuse.generateDescriptorWrite(1, UAV_INDEX(gDiffuse) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtInstanceId.generateDescriptorWrite(1, UAV_INDEX(gInstanceId) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtDirectLightAccum[rtSwap ? 1 : 0].generateDescriptorWrite(1, UAV_INDEX(gDirectLightAccum) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtIndirectLightAccum[rtSwap ? 1 : 0].generateDescriptorWrite(1, UAV_INDEX(gIndirectLightAccum) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtFilteredDirectLight[1].generateDescriptorWrite(1, UAV_INDEX(gFilteredDirectLight) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtFilteredIndirectLight[1].generateDescriptorWrite(1, UAV_INDEX(gFilteredIndirectLight) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtReflection.generateDescriptorWrite(1, UAV_INDEX(gReflection) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtRefraction.generateDescriptorWrite(1, UAV_INDEX(gRefraction) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtTransparent.generateDescriptorWrite(1, UAV_INDEX(gTransparent) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtFlow.generateDescriptorWrite(1, UAV_INDEX(gFlow) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtReactiveMask.generateDescriptorWrite(1, UAV_INDEX(gReactiveMask) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtLockMask.generateDescriptorWrite(1, UAV_INDEX(gLockMask) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(rtDepth[rtSwap ? 1 : 0].generateDescriptorWrite(1, UAV_INDEX(gDepth) + UAV_SHIFT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet));
            descriptorWrites.push_back(globalParamsBuffer.generateDescriptorWrite(1, CBV_INDEX(gParams) + CBV_SHIFT, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, descriptorSet));

            // Write to the debug descriptor set
            vkUpdateDescriptorSets(device->getVkDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
            descriptorWrites.clear();
        }
    }

    void View::update() 
    {
	    RT64_LOG_PRINTF("Started view update");

        // Recreate buffers if necessary for next frame.
        if (recreateRTBuffers) {
            createOutputBuffers();
            recreateRTBuffers = false;
        }

        auto getTextureIndex = [this](Texture *texture) {
            if (texture == nullptr) {
                return -1;
            }

            int currentIndex = texture->getCurrentIndex();
            if (currentIndex < 0) {
                currentIndex = (int)(usedTextures.size());
                texture->setCurrentIndex(currentIndex);
                usedTextures.push_back(texture);
            }

            return currentIndex;
        };

        usedTextures.clear();
        usedTextures.reserve(SRV_TEXTURES_MAX);
	    globalParamsData.skyPlaneTexIndex = getTextureIndex(skyPlaneTexture);

        if (!scene->getInstances().empty()) {
            // Create the active instance vectors.
            RenderInstance renderInstance;
            Mesh* usedMesh = nullptr;
            Texture *usedDiffuse = nullptr;
            size_t totalInstances = scene->getInstances().size();
		    bool updateDescriptors = (totalInstances != (rtInstances.size() + rasterBgInstances.size() + rasterFgInstances.size()));
            unsigned int instFlags = 0;
            unsigned int screenHeight = getHeight();
            unsigned int id = 0;
            rtInstances.clear();
            rasterBgInstances.clear();
            rasterFgInstances.clear();
            rasterUiInstances.clear();

            rtInstances.reserve(totalInstances);
            rasterBgInstances.reserve(totalInstances);
            rasterFgInstances.reserve(totalInstances);
            rasterUiInstances.reserve(totalInstances);

            for (Instance *instance : scene->getInstances()) {
                instFlags = instance->getFlags();
                usedMesh = instance->getMesh();
                renderInstance.instance = instance;
                renderInstance.blas = &usedMesh->getBlas();
                // renderInstance.bottomLevelAS = usedMesh->getBottomLevelASResult();
                renderInstance.transform = convertGLMtoNVMATHMatrix(instance->getTransform());
                renderInstance.transformPrevious = convertGLMtoNVMATHMatrix(instance->getPreviousTransform());
                renderInstance.material = instance->getMaterial();
                renderInstance.shader = instance->getShader();
                renderInstance.indexCount = usedMesh->getIndexCount();
                renderInstance.vertexBuffer = &usedMesh->getVertexBuffer().getBuffer();
                renderInstance.indexBuffer = &usedMesh->getIndexBuffer().getBuffer();
                renderInstance.flags = (instFlags & RT64_INSTANCE_DISABLE_BACKFACE_CULLING) ? VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR : 0;
                renderInstance.material.diffuseTexIndex = getTextureIndex(instance->getDiffuseTexture());
                renderInstance.material.normalTexIndex = getTextureIndex(instance->getNormalTexture());
                renderInstance.material.specularTexIndex = getTextureIndex(instance->getSpecularTexture());
                
                if (!instance->hasScissorRect()) {
                    RT64_RECT rect = instance->getScissorRect();
                    renderInstance.scissorRect.offset.x = rect.x;
                    renderInstance.scissorRect.offset.y = screenHeight - rect.y;
                    renderInstance.scissorRect.extent.width = rect.w;
                    renderInstance.scissorRect.extent.height = screenHeight;
                }
                else {
                    renderInstance.scissorRect = VkRect2D {{0,0}, {0,0}};
                }

                if (instance->hasViewportRect()) {
                    RT64_RECT rect = instance->getViewportRect();
                    renderInstance.viewport = {
                        static_cast<float>(rect.x),
                        static_cast<float>(screenHeight - rect.y - rect.h),
                        static_cast<float>(rect.w),
                        static_cast<float>(rect.h)
                    };
                }
                else {
                    renderInstance.viewport = {0.0f, 0.0f, 0.0f, 0.0f};
                }

                if (rtEnabled && usedMesh->getBlasAddress() != (VkDeviceAddress)nullptr) {
                    rtInstances.push_back(renderInstance);
                } else if (instFlags & RT64_INSTANCE_RASTER_BACKGROUND) {
                    rasterBgInstances.push_back(renderInstance);
                } else if (instFlags & RT64_INSTANCE_RASTER_UI) {
                    rasterUiInstances.push_back(renderInstance);
                } else {
                    rasterFgInstances.push_back(renderInstance);
                }
                id++;
            }

            // Create the acceleration structures used by the raytracer.
            if (rtEnabled && !rtInstances.empty()) {
                createTopLevelAS(rtInstances);
            }

            // Create the instance buffers for the active instances (if necessary).
            createInstanceTransformsBuffer();
            createInstanceMaterialsBuffer();
            
            // Create the buffer containing the raytracing result (or atleast soon), 
            //  and create the descriptor sets referencing the resources used 
            //  by the raytracing, such as the acceleration structure
            updateShaderDescriptorSets(updateDescriptors);
            
            // Create the shader binding table and indicating which shaders
            // are invoked for each instance in the AS.
            if (rtEnabled) {
                createShaderBindingTable();
            }

            // Update the instance buffers for the active instances.
            updateInstanceTransformsBuffer();
            updateInstanceMaterialsBuffer();
        } else {
            rtInstances.clear();
            rasterBgInstances.clear();
            rasterFgInstances.clear();
            rasterUiInstances.clear();
        }

        RT64_LOG_PRINTF("Finished view update");
    }

    void View::createTopLevelAS(const std::vector<RenderInstance>& renderInstances) {
        std::vector<VkAccelerationStructureInstanceKHR> tlas;
        tlas.reserve(renderInstances.size());
        rtBuilder.destroyTlas();

        int id = 0;
        for (RenderInstance r : renderInstances) {
            // Build the blas of each of the render instances
            rtBuilder.emplaceBlas(r.instance->getMesh()->getBlas());
            VkAccelerationStructureInstanceKHR rayInst{};
            rayInst.transform = nvvk::toTransformMatrixKHR(r.transform);
            rayInst.instanceCustomIndex = id;
            rayInst.accelerationStructureReference = r.instance->getMesh()->getBlasAddress();
            rayInst.flags = r.flags;
            rayInst.mask = 0xFF;
            rayInst.instanceShaderBindingTableRecordOffset = id * 2;
            tlas.emplace_back(rayInst);
            id++;
        }
        rtBuilder.buildTlas(tlas, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
    }

    // Get all the RT shader handles and write them into an SBT buffer
    //  From nvpro-samples
    void View::createShaderBindingTable() {
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProperties = device->getRTProperties();
        unsigned int missCount = 2;                                                 // How many miss shaders exist in the pipeline
        unsigned int hitCount = device->getHitShaderCount();                        // How many hit shaders exist in the pipeline
        unsigned int raygenCount = SHADER_INDEX(surfaceMiss);                       // How many raygen shaders exist in the pipeline
        unsigned int handleCount = raygenCount + missCount + hitCount;              // How many rt shaders in total exist in the pipeline
        unsigned int handleSize = rtProperties.shaderGroupHandleSize;
        // The SBT (buffer) need to have starting groups to be aligned and handles in the group to be aligned.
        unsigned int handleSizeAligned = ROUND_UP(handleSize, rtProperties.shaderGroupHandleAlignment);

        VkStridedDeviceAddressRegionKHR raygenRegion{};
        raygenRegion.stride = ROUND_UP(handleSizeAligned, rtProperties.shaderGroupBaseAlignment);
        raygenRegion.size = raygenRegion.stride;    // The size member of pRayGenShaderBindingTable must be equal to its stride member
        primaryRayGenRegion = raygenRegion;
        directRayGenRegion = raygenRegion;
        indirectRayGenRegion = raygenRegion;
        reflectionRayGenRegion = raygenRegion;
        refractionRayGenRegion = raygenRegion;
        missRegion.stride = handleSizeAligned;
        missRegion.size = ROUND_UP(missCount * handleSizeAligned, rtProperties.shaderGroupBaseAlignment);

        // Stride is the size of the handle + the addresses to the vertex/index buffer
        // hitRegion.size and hitCount will differ 
        //  - hitRegion.size is how many rtInstances there are
        //  - hitCount is how many hit shaders there are in the pipeline
        // The number of group handles should match how many overall hit shaders there are in the pipeline. Then 
        //  once we get to copying data to the buffer, the sbt will use the already generated handles instead
        //  of trying to find rtInstances.size() worth of handles, which most likely won't exist and then it
        //  will cause Bad Things(TM) to happen
        hitRegion.stride = ROUND_UP(handleSizeAligned + sizeof(VkDeviceAddress) * 2, rtProperties.shaderGroupHandleAlignment);
        hitRegion.size = ROUND_UP(hitCount * hitRegion.stride * rtInstances.size(), rtProperties.shaderGroupBaseAlignment);

        // Get the shader group handles
        unsigned int dataSize = handleCount * handleSize;
        std::vector<uint8_t> handles(dataSize);
        VK_CHECK(vkGetRayTracingShaderGroupHandlesKHR(device->getVkDevice(), device->getRTPipeline(), 0, handleCount, dataSize, handles.data()));

        // Get the new size of the sbt
        VkDeviceSize newSbtSize = (raygenRegion.size * raygenCount) + missRegion.size + hitRegion.size + callRegion.size;

        // If the SBT has a size (implying there's already an SBT), then destroy the SBT
        if (sbtSize > 0) {
            shaderBindingTable.destroyResource();
        }

        // Allocate a buffer for storing the SBT.
        device->allocateBuffer(
            newSbtSize, 
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR,
            VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT,
            &shaderBindingTable
        );
        device->bufferMemoryBarrier(shaderBindingTable, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, nullptr);
        shaderBindingTable.setAllocationName("ShaderBindingTable");
        sbtSize = newSbtSize;

        // Find the SBT addresses of each group
        VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr, shaderBindingTable.getBuffer()};
        VkDeviceAddress sbtAddress = vkGetBufferDeviceAddress(device->getVkDevice(), &info);
        primaryRayGenRegion.deviceAddress = sbtAddress;
        directRayGenRegion.deviceAddress = primaryRayGenRegion.deviceAddress + primaryRayGenRegion.size;
        indirectRayGenRegion.deviceAddress = directRayGenRegion.deviceAddress + directRayGenRegion.size;
        reflectionRayGenRegion.deviceAddress = indirectRayGenRegion.deviceAddress + indirectRayGenRegion.size;
        refractionRayGenRegion.deviceAddress = reflectionRayGenRegion.deviceAddress + reflectionRayGenRegion.size;
        missRegion.deviceAddress = refractionRayGenRegion.deviceAddress + refractionRayGenRegion.size;
        hitRegion.deviceAddress = missRegion.deviceAddress + missRegion.size;

        // Helper to retrieve the handle data
        auto getHandle = [&](int i) { return handles.data() + i * handleSize; };

        // Map the SBT buffer and write in the handles
        uint8_t* pSBTBuffer;
        uint8_t* pData = reinterpret_cast<uint8_t*>(shaderBindingTable.mapMemory((void**)&pSBTBuffer));
        uint32_t handleIdx = 0;

        // Raygen
        for(uint32_t c = 0; c < raygenCount; c++) {
            memcpy(pData, getHandle(handleIdx++), handleSize);
            pData += raygenRegion.stride;
        }

        // Miss
        pData = pSBTBuffer + (raygenRegion.size * raygenCount);
        for(uint32_t c = 0; c < missCount; c++) {
            memcpy(pData, getHandle(handleIdx++), handleSize);
            pData += missRegion.stride;
        }

        // Hit
        VkBufferDeviceAddressInfo vertAddr { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
        VkBufferDeviceAddressInfo indAddr { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
        pData = pSBTBuffer + (raygenRegion.size * raygenCount) + missRegion.size;
        for(uint32_t c = 0; c < rtInstances.size(); c++) {
            vertAddr.buffer = *rtInstances[c].vertexBuffer;
            indAddr.buffer = *rtInstances[c].indexBuffer;
            // The SBT data consists of the addresses to the vertex and index buffers
            VkDeviceAddress sbtData[] = {
                vkGetBufferDeviceAddress(device->getVkDevice(), &vertAddr),
                vkGetBufferDeviceAddress(device->getVkDevice(), &indAddr)
            };

            // Get the surface hit group
            auto surfaceHitGroup = rtInstances[c].shader->getSurfaceHitGroup();
            memcpy(pData, getHandle(handleIdx + surfaceHitGroup.id), handleSize);       // Copy the handle for the current surface hit group
            memcpy(pData + handleSize, sbtData, sizeof(sbtData));                       // After that, copy the SBT data
            pData += hitRegion.stride;

            // Get the shadow hit group
            auto shadowHitGroup = rtInstances[c].shader->getShadowHitGroup();           
            memcpy(pData, getHandle(handleIdx + shadowHitGroup.id), handleSize);        // Copy the handle for the current shadow hit group
            memcpy(pData + handleSize, sbtData, sizeof(sbtData));                       // You know what it is
            pData += hitRegion.stride;
        }
        shaderBindingTable.unmapMemory();
    }

    void View::render(float deltaTimeMs) { 
        VkCommandBuffer commandBuffer = scene->getDevice()->getCurrentCommandBuffer();
        VkViewport viewport = scene->getDevice()->getViewport();
        VkRect2D scissors = scene->getDevice()->getScissors();
        VkRenderPass presentRenderPass = scene->getDevice()->getPresentRenderPass();
        VkRenderPass offscreenRenderPass = scene->getDevice()->getOffscreenRenderPass();
        VkFramebuffer framebuffer = scene->getDevice()->getCurrentSwapchainFramebuffer();
        VkExtent2D swapChainExtent = scene->getDevice()->getSwapchainExtent();

        // Configure the current viewport
        auto resetViewport = [this, commandBuffer, &viewport]() {
            vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
            viewportApplied = false;
        };

        auto resetScissor = [this, commandBuffer, &scissors]() {
            vkCmdSetScissor(commandBuffer, 0, 1, &scissors);
		    scissorApplied = false;
        };

        auto applyViewport = [this, commandBuffer, resetViewport](const VkViewport& viewport) {
            if ((viewport.width > 0) && (viewport.height > 0)) {
                vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
                viewportApplied = true;
            }
            else if (viewportApplied) {
                resetViewport();
            }
        };

        auto applyScissor = [this, commandBuffer, resetScissor](const VkRect2D& rect) {
            if (rect.offset.x + rect.extent.width > rect.offset.x) {
                vkCmdSetScissor(commandBuffer, 0, 1, &rect);
                scissorApplied = true;
            }
            else if (scissorApplied) {
                resetScissor();
            }
        };

        auto drawInstances = [commandBuffer, &scissors, applyScissor, applyViewport, this](const std::vector<RT64::View::RenderInstance>& rasterInstances, uint32_t baseInstanceIndex, bool applyScissorsAndViewports) {
            uint32_t rasterSize = rasterInstances.size();
            Shader* previousShader = nullptr;
            
            for (uint32_t j = 0; j < rasterSize; j++) {
			    const RenderInstance& renderInstance = rasterInstances[j];
                if (applyScissorsAndViewports) {
                    applyScissor(renderInstance.scissorRect);
                    applyViewport(renderInstance.viewport);
                }
                if (previousShader != renderInstance.shader) {
                    const auto &rasterGroup = renderInstance.shader->getRasterGroup();
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rasterGroup.pipeline);
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rasterGroup.pipelineLayout, 0, 1, &renderInstance.shader->getRasterGroup().descriptorSet, 0, nullptr);
                    previousShader = renderInstance.shader;
                }

                VkDeviceSize offsets[] = {0};
                int pushConst = baseInstanceIndex + j;
                vkCmdPushConstants(commandBuffer, renderInstance.shader->getRasterGroup().pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uint32_t), &pushConst);
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, renderInstance.vertexBuffer, offsets);
                vkCmdBindIndexBuffer(commandBuffer, *renderInstance.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(renderInstance.instance->getMesh()->getIndexCount()), 1, 0, 0, 0);
            }
        };

        RT64_LOG_PRINTF("Updating global parameters");

        // Determine whether to use the viewport and scissor from the first RT Instance or not.
        // TODO: Some less hackish way to determine what viewport to use for the raytraced content perhaps.
        VkRect2D rtScissorRect = scissors;
        VkViewport rtViewport = viewport;
        if (!rtInstances.empty() || !rasterFgInstances.empty()) {
            rtScissorRect = rtInstances[0].scissorRect;
            rtViewport = rtInstances[0].viewport;
            if ((rtScissorRect.offset.x + rtScissorRect.extent.width <= rtScissorRect.offset.x)) {
                rtScissorRect = scissors;
            }

            if ((rtViewport.width == 0) || (rtViewport.height == 0)) {
                rtViewport = viewport;
            }

            // Only use jitter when an upscaler is active.
            // bool jitterActive = rtUpscaleActive && (upscaler != nullptr);
            bool jitterActive = false;
            if (jitterActive) {
                // const int phaseCount = upscaler->getJitterPhaseCount(rtWidth, lround(globalParamsData.resolution.z));
                // globalParamsData.pixelJitter = HaltonJitter(globalParamsData.frameCount, phaseCount);
            }
            else {
                globalParamsData.pixelJitter = { 0.0f, 0.0f };
            }

            globalParamsData.viewport.x = rtViewport.x;
            globalParamsData.viewport.y = rtViewport.y;
            globalParamsData.viewport.z = rtViewport.width;
            globalParamsData.viewport.w = rtViewport.height;

            updateGlobalParamsBuffer();
            updateFilterParamsBuffer();
        }

        // Begin the command buffer
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = 0; // Optional
        beginInfo.pInheritanceInfo = nullptr; // Optional
        VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

        // Create a render pass begin info structure (it's a surprise tool that will help us later)
        VkRenderPassBeginInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent.width = rtWidth;
        renderPassInfo.renderArea.extent.height = rtHeight;
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        // Draw the background instances to a buffer that can be used by the tracer as an environment map.
        RT64_LOG_PRINTF("Drawing background instances");
        {
            resetScissor();
            resetViewport();
            // drawInstances(rasterBgInstances, (UINT)(rtInstances.size()), true);
        }

        // Now raytrace!
        if (rtEnabled)
        {
            // Make sure the images are usable
            AllocatedImage* primaryTranslation[] = {
                &rtDiffuse,
                &rtInstanceId,
                &rtReflection,
                &rtRefraction,
                &rtTransparent,
                &rtFlow,
                &rtReactiveMask,
                &rtLockMask,
                &rtDepth[rtSwap ? 1 : 0]
            };
            device->transitionImageLayout(primaryTranslation, sizeof(primaryTranslation) / sizeof(AllocatedImage*), 
                VK_IMAGE_LAYOUT_GENERAL, 
                VK_ACCESS_SHADER_WRITE_BIT, 
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 
                &commandBuffer);

            // Bind pipeline and dispatch primary rays.
		    RT64_LOG_PRINTF("Dispatching primary rays");
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, device->getRTPipeline());
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, device->getRTPipelineLayout(), 0, device->getRTDescriptorSets().size(), device->getRTDescriptorSets().data(), 0, nullptr);
            vkCmdTraceRaysKHR(commandBuffer, &primaryRayGenRegion, &missRegion, &hitRegion, &callRegion, rtWidth, rtHeight, 1);

	    	// Barriers for shading buffers before dispatching secondary rays.
            AllocatedImage* shadingBarriers[] = {
                &rtViewDirection,
                &rtDirectLightAccum[rtSwap ? 1 : 0],
                &rtIndirectLightAccum[rtSwap ? 1 : 0],
                &rtShadingPosition,
                &rtShadingNormal,
                &rtShadingSpecular,
                &rtNormal[rtSwap ? 1 : 0]
            };
            device->transitionImageLayout(shadingBarriers, sizeof(shadingBarriers) / sizeof(AllocatedImage*), 
                VK_IMAGE_LAYOUT_GENERAL, 
                VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT, 
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 
                &commandBuffer);

    		// Store the first bounce instance Id.
            device->transitionImageLayout(rtInstanceId, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                &commandBuffer);
            device->transitionImageLayout(rtFirstInstanceId, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                &commandBuffer);
            device->copyImage(rtInstanceId, rtFirstInstanceId, rtInstanceId.getDimensions(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &commandBuffer);

		    RT64_LOG_PRINTF("Dispatching direct light rays");
            vkCmdTraceRaysKHR(commandBuffer, &directRayGenRegion, &missRegion, &hitRegion, &callRegion, rtWidth, rtHeight, 1);

            // Indirect lighting
            if (globalParamsData.giBounces > 1 && globalParamsData.giSamples > 0) {
                // Copy the shading position/normal/direction into the secondary shading images
                AllocatedImage* primaryShadingBarriers[] = {
                    &rtShadingPosition,
                    &rtShadingNormal,
                };
                AllocatedImage* secondaryShadingBarriers[] = {
                    &rtShadingPositionSecondary,
                    &rtShadingNormalSecondary,
                };

                device->transitionImageLayout(primaryShadingBarriers, sizeof(primaryShadingBarriers) / sizeof(AllocatedImage*), 
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    &commandBuffer);
                device->transitionImageLayout(secondaryShadingBarriers, sizeof(secondaryShadingBarriers) / sizeof(AllocatedImage*), 
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    &commandBuffer);
                device->copyImage(rtShadingPosition, rtShadingPositionSecondary, rtShadingPosition.getDimensions(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &commandBuffer);
                device->copyImage(rtShadingNormal,   rtShadingNormalSecondary, rtShadingNormal.getDimensions(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &commandBuffer);

                // Now make the primary shading images readable/writeable
                device->transitionImageLayout(primaryShadingBarriers, sizeof(primaryShadingBarriers) / sizeof(AllocatedImage*), 
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                    &commandBuffer);

                // Transition the instance ID buffer into an optimal attachment
                device->transitionImageLayout(rtInstanceId, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                    &commandBuffer);
            
                VkDeviceSize offsets[] = {0};
                RaygenPushConstant pushConst = { 1.0f, 1.0f };
                for (int i = 0; i < globalParamsData.giBounces; i++) {
                    RT64_LOG_PRINTF("Dispatching indirect light rays batch #" + (i+1));
                    vkCmdPushConstants(commandBuffer, device->getRTPipelineLayout(), VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(RaygenPushConstant), &pushConst);
                    vkCmdTraceRaysKHR(commandBuffer, &indirectRayGenRegion, &missRegion, &hitRegion, &callRegion, rtWidth, rtHeight, 1);
                    pushConst.bounceDivisor *= 16.0f;
                    pushConst.currentBounce += 1.0f;

                    if (i < globalParamsData.giBounces - 1) {
                        // This is meant to just make the gpu wait until it's done with the prior indirect lighting
                        device->transitionImageLayout(primaryShadingBarriers, sizeof(primaryShadingBarriers) / sizeof(AllocatedImage*), 
                            VK_IMAGE_LAYOUT_GENERAL,
                            VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                            &commandBuffer);
                    }
                }

                // Copy the secondary shading position image into the primary shading position image now that we're done with secondary bounces
                device->transitionImageLayout(secondaryShadingBarriers, sizeof(secondaryShadingBarriers) / sizeof(AllocatedImage*), 
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    &commandBuffer);
                device->transitionImageLayout(primaryShadingBarriers, sizeof(primaryShadingBarriers) / sizeof(AllocatedImage*), 
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    &commandBuffer);
                device->copyImage(rtShadingPositionSecondary, rtShadingPosition, rtShadingPosition.getDimensions(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &commandBuffer);
                device->copyImage(rtShadingNormalSecondary,   rtShadingNormal, rtShadingNormal.getDimensions(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &commandBuffer);

                // Now make the shading position image readable
                device->transitionImageLayout(primaryShadingBarriers, sizeof(primaryShadingBarriers) / sizeof(AllocatedImage*), 
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                    &commandBuffer);

                // Copy the first instance id back into the instance id image.
                device->transitionImageLayout(rtFirstInstanceId, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    &commandBuffer);
                device->transitionImageLayout(rtInstanceId, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    &commandBuffer);
                device->copyImage(rtFirstInstanceId, rtInstanceId, rtInstanceId.getDimensions(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &commandBuffer);

                // Transition back to the top of the pipeline
                device->transitionImageLayout(secondaryShadingBarriers, sizeof(secondaryShadingBarriers) / sizeof(AllocatedImage*), 
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_ACCESS_NONE,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    &commandBuffer);

            } else if (globalParamsData.giBounces == 1 && globalParamsData.giSamples > 0) {
                RT64_LOG_PRINTF("Dispatching only first indirect rays");
                VkDeviceSize offsets[] = {0};
                RaygenPushConstant pushConst = { 1.0f };
                vkCmdPushConstants(commandBuffer, device->getRTPipelineLayout(), VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(RaygenPushConstant), &pushConst);
                vkCmdTraceRaysKHR(commandBuffer, &indirectRayGenRegion, &missRegion, &hitRegion, &callRegion, rtWidth, rtHeight, 1);

                // Wait until indirect light is done before dispatching reflection or refraction rays.
                // TODO: This is only required to prevent simultaneous usage of the anyhit buffers.
                //  This barrier can be removed if this no longer happens, resulting in less serialization of the commands.
                device->transitionImageLayout(rtIndirectLightAccum[rtSwap ? 1 : 0], 
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                    VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                    &commandBuffer);
            }

		    // Transition the instance ID buffer into an optimal attachment
            device->transitionImageLayout(rtInstanceId, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                &commandBuffer);
            
		    RT64_LOG_PRINTF("Dispatching refraction rays");
            vkCmdTraceRaysKHR(commandBuffer, &refractionRayGenRegion, &missRegion, &hitRegion, &callRegion, rtWidth, rtHeight, 1);

		    // Wait until refraction is done before dispatching reflection rays.
            // TODO: This is only required to prevent simultaneous usage of the anyhit buffers.
            //  This barrier can be removed if this no longer happens, resulting in less serialization of the commands.
            device->transitionImageLayout(rtReflection, 
                VK_IMAGE_LAYOUT_GENERAL,
                VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                &commandBuffer);

            int reflections = maxReflections;
            while (reflections > 0) {
			    RT64_LOG_PRINTF("Dispatching reflection rays");
                vkCmdTraceRaysKHR(commandBuffer, &reflectionRayGenRegion, &missRegion, &hitRegion, &callRegion, rtWidth, rtHeight, 1);
                reflections--;

			    // Add a barrier to wait for the input bindings to be finished if there's more passes left to be done.
                if (reflections > 0) {
                    AllocatedImage* newInputBarriers[] = {
                        &rtViewDirection,
                        &rtShadingNormal,
                        &rtInstanceId,
                        &rtReflection
                    };
                    device->transitionImageLayout(newInputBarriers, sizeof(newInputBarriers) / sizeof(AllocatedImage*), 
                        VK_IMAGE_LAYOUT_GENERAL, 
                        VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT, 
                        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 
                        &commandBuffer);
                }
            }

    	    // Barriers for shading buffers after rays are finished.
            AllocatedImage* postDispatchBarriers[] = {
                &rtOutput[rtSwap ? 1 : 0],
                &rtDiffuse,
                &rtDirectLightAccum[rtSwap ? 1 : 0],
                &rtIndirectLightAccum[rtSwap ? 1 : 0],
                &rtReflection,
                &rtRefraction,
                &rtTransparent
            };
            device->transitionImageLayout(postDispatchBarriers, sizeof(postDispatchBarriers) / sizeof(AllocatedImage*), 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
                VK_ACCESS_SHADER_READ_BIT, 
                VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                &commandBuffer);
            device->transitionImageLayout(rtOutputTonemapped, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
                VK_ACCESS_SHADER_READ_BIT, 
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                &commandBuffer);

		    // Copy direct light raw buffer to the first direct filtered buffer.
#	ifdef DI_DENOISING_SUPPORT
		    bool denoiseDI = denoiserEnabled && (globalParamsData.diSamples > 0);
#	else
		    bool denoiseDI = false;
#	endif
            {
                AllocatedImage& src = rtDirectLightAccum[rtSwap ? 1 : 0];
                AllocatedImage& dest = rtFilteredDirectLight[denoiseDI ? 0 : 1];
                device->transitionImageLayout(src, 
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
                    VK_ACCESS_TRANSFER_READ_BIT, 
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 
                    &commandBuffer);
                device->transitionImageLayout(dest, 
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
                    VK_ACCESS_TRANSFER_WRITE_BIT, 
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 
                    &commandBuffer);

                device->copyImage(src, dest, src.getDimensions(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &commandBuffer);

                device->transitionImageLayout(src, 
                    VK_IMAGE_LAYOUT_GENERAL, 
                    VK_ACCESS_SHADER_READ_BIT, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    &commandBuffer);
                device->transitionImageLayout(dest, 
                    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, 
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    &commandBuffer);
            }

            /* 
            *
            *  Filter the direct light, but that seems to be WIP in upstream
            * 
            */

            // Copy indirect light raw buffer to the first indirect filtered buffer.
            bool denoiseGI = denoiserEnabled && (globalParamsData.giSamples > 0);
            {
                AllocatedImage& src = rtIndirectLightAccum[rtSwap ? 1 : 0];
                AllocatedImage& dest = rtFilteredIndirectLight[denoiseGI ? 0 : 1];
                device->transitionImageLayout(src, 
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
                    VK_ACCESS_TRANSFER_READ_BIT, 
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 
                    &commandBuffer);
                device->transitionImageLayout(dest, 
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
                    VK_ACCESS_TRANSFER_WRITE_BIT, 
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 
                    &commandBuffer);

                device->copyImage(src, dest, src.getDimensions(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &commandBuffer);

                device->transitionImageLayout(src, 
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
                    VK_ACCESS_SHADER_READ_BIT, 
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                    &commandBuffer);
                device->transitionImageLayout(dest, 
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
                    VK_ACCESS_SHADER_READ_BIT, 
                    denoiseGI ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                    &commandBuffer);
            }
            
            if (denoiseGI)
            {
                for (int i = 0; i < 5; i++) {
                    const int ThreadGroupWorkCount = 8;
                    int dispatchX = rtWidth / ThreadGroupWorkCount + ((rtWidth % ThreadGroupWorkCount) ? 1 : 0);
                    int dispatchY = rtHeight / ThreadGroupWorkCount + ((rtHeight % ThreadGroupWorkCount) ? 1 : 0);

                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, device->getGaussianFilterRGB3x3Pipeline());
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, device->getGaussianFilterRGB3x3PipelineLayout(), 0, 1, &indirectFilterDescriptorSets[i % 2], 0, nullptr);
                    vkCmdDispatch(commandBuffer, dispatchX, dispatchY, 1);

                    // The read image
                    device->transitionImageLayout(rtFilteredIndirectLight[(i % 2) ? 1 : 0], 
                        VK_IMAGE_LAYOUT_GENERAL, 
                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, 
                        i == 4 ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                        &commandBuffer);
                    // The write image
                    device->transitionImageLayout(rtFilteredIndirectLight[(i % 2) ? 0 : 1], 
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
                        VK_ACCESS_SHADER_READ_BIT, 
                        i == 4 ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                        &commandBuffer);
                }
            }
        }
        else        // Or don't raytrace.
        {
            // Make sure the images are usable
            AllocatedImage* primaryTranslation[] = {
                &rtDiffuse,
            };
            device->transitionImageLayout(primaryTranslation, sizeof(primaryTranslation) / sizeof(AllocatedImage*), 
                VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, 
                VK_ACCESS_SHADER_WRITE_BIT, 
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                &commandBuffer);

            // Begin the render pass for the diffuse image
            renderPassInfo.renderPass = rasterPass;
            renderPassInfo.framebuffer = diffuseFB;
            vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

            // Apply the scissor and viewport to the size of the output texture.
            applyScissor({rtWidth, rtHeight});
            VkViewport v = {0.f, 0.f, (float)rtWidth, (float)rtHeight, 1.f, 1.f};
            applyViewport(v);
	        drawInstances(rasterFgInstances, 0, true);

            vkCmdEndRenderPass(commandBuffer);
        }

        // Begin the offscreen render pass (you can't raytrace with it, so just raytrace before you render pass it up)
        renderPassInfo.renderPass = offscreenRenderPass;
        renderPassInfo.framebuffer = rtOutputFB[rtSwap ? 1 : 0];
        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		// Apply the scissor and viewport to the size of the output texture.
        applyScissor({rtWidth, rtHeight});
        VkViewport v = {0.f, 0.f, (float)rtWidth, (float)rtHeight, 1.f, 1.f};
        applyViewport(v);

        // Now compose!
		RT64_LOG_PRINTF("Composing the raytracing output");
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, device->getComposePipeline());
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, device->getComposePipelineLayout(), 0, 1, &device->getComposeDescriptorSet(), 0, nullptr);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);

        vkCmdEndRenderPass(commandBuffer);

        // Begin tonemapping 
        renderPassInfo.framebuffer = rtOutputTonemappedFB;
        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        
        // Now tonemap!
		RT64_LOG_PRINTF("Tonemapping the raytracing output");
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, device->getTonemappingPipeline());
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, device->getTonemappingPipelineLayout(), 0, 1, &device->getTonemappingDescriptorSet(), 0, nullptr);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(commandBuffer);

        // Begine the on-screen render pass
        renderPassInfo.framebuffer = framebuffer;
        renderPassInfo.renderArea.extent = swapChainExtent;
        renderPassInfo.renderPass = presentRenderPass;
        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Apply the same scissor and viewport that was determined for the raytracing step.
		applyScissor(rtScissorRect);
		applyViewport(rtViewport);

        // The final output
        if (globalParamsData.visualizationMode == VisualizationModeFinal) {
            RT64_LOG_PRINTF("Drawing the final output!");
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, device->getPostProcessPipeline());
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, device->getPostProcessPipelineLayout(), 0, 1, &device->getPostProcessDescriptorSet(), 0, nullptr);
            vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        } else {
            RT64_LOG_PRINTF("Drawing the debug image");
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, device->getDebugPipeline());
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, device->getDebugPipelineLayout(), 0, 1, &device->getDebugDescriptorSet(), 0, nullptr);
            vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        }

        // Draw the UI images
        RT64_LOG_PRINTF("Drawing UI instances");
        resetScissor();
        resetViewport();
	        // drawInstances(rasterFgInstances, rtInstances.size() + rasterBgInstances.size(), true);
	    drawInstances(rasterUiInstances, rtInstances.size() + rasterBgInstances.size() + rasterFgInstances.size(), true);

        vkCmdEndRenderPass(commandBuffer);
        
	    // Barriers for shading buffers after the whole render process is finished
        AllocatedImage* postRTBarriers[] = {
            &rasterBg,
            &rtViewDirection,
            &rtShadingPosition,
            &rtShadingNormal,
            &rtShadingSpecular,
            &rtInstanceId,
            &rtFlow,
            &rtReactiveMask,
            &rtLockMask,
            &rtNormal[rtSwap ? 1 : 0],
            &rtDepth[rtSwap ? 1 : 0]
        };
        device->transitionImageLayout(postRTBarriers, sizeof(postRTBarriers) / sizeof(AllocatedImage*), 
            VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_NONE, 
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
            &commandBuffer);

        AllocatedImage* postFragBarriers[] = {
            &rtOutput[rtSwap ? 1 : 0],
            &rtDiffuse,
            &rtDirectLightAccum[rtSwap ? 1 : 0],
            &rtIndirectLightAccum[rtSwap ? 1 : 0],
            &rtReflection,
            &rtRefraction,
            &rtTransparent
        };
        device->transitionImageLayout(postFragBarriers, sizeof(postFragBarriers) / sizeof(AllocatedImage*), 
            VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_NONE, 
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
            &commandBuffer);

        rtSwap = !rtSwap;
        rtSkipReprojection = false;
        rtFirstInstanceIdReadbackUpdated = false;
        globalParamsData.frameCount++;
    }

    // Public

    void View::setSkyPlaneTexture(Texture *texture) {
        skyPlaneTexture = texture;
    }

    void View::allocateDescriptorSets() {
        device->allocateDescriptorSet(device->getGaussianFilterRGB3x3DescriptorSetLayout(), indirectFilterDescriptorSets[0]);
        device->allocateDescriptorSet(device->getGaussianFilterRGB3x3DescriptorSetLayout(), indirectFilterDescriptorSets[1]);
    }

    RT64_VECTOR3 View::getRayDirectionAt(int px, int py) {
        float x = ((px + 0.5f) / getWidth()) * 2.0f - 1.0f;
        float y = ((py + 0.5f) / getHeight()) * 2.0f - 1.0f;
        glm::vec4 target = globalParamsData.projectionI * glm::vec4{x, -y, 1.0f, 1.0f};
        glm::vec4 rayDirection = globalParamsData.viewI * glm::vec4{target.x, target.y, target.z, 0.0f};
        rayDirection = glm::normalize(rayDirection);
        return { rayDirection.x, rayDirection.y, rayDirection.z };
    }

    void View::resize() {
        createOutputBuffers();
    }

    int View::getWidth() const {
        return scene->getDevice()->getWidth();
    }

    int View::getHeight() const {
        return scene->getDevice()->getHeight();
    }

    RT64_VECTOR3 View::getViewPosition() {
        glm::vec4 pos = globalParamsData.viewI * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
        return { pos.x, pos.y, pos.z };
    }

    RT64_VECTOR3 View::getViewDirection() {
        glm::vec4 xdir = globalParamsData.viewI * glm::vec4{0.0f, 0.0f, 1.0f, 0.0f};
        RT64_VECTOR3 dir = { xdir.x, xdir.y, xdir.z };
        float length = Length(dir);
        return dir / length;
    }

    void View::setPerspective(RT64_MATRIX4 viewMatrix, float fovRadians, float nearDist, float farDist) {
        // Ignore all external calls to set the perspective when control override is active.
        if (perspectiveControlActive) {
            return;
        }

        this->fovRadians = fovRadians;
        this->nearDist = nearDist;
        this->farDist = farDist;

        globalParamsData.view = {
            viewMatrix.m[0][0], 
            viewMatrix.m[0][1],
            viewMatrix.m[0][2], 
            viewMatrix.m[0][3],
            viewMatrix.m[1][0], 
            viewMatrix.m[1][1], 
            viewMatrix.m[1][2], 
            viewMatrix.m[1][3],
            viewMatrix.m[2][0], 
            viewMatrix.m[2][1], 
            viewMatrix.m[2][2], 
            viewMatrix.m[2][3],
            viewMatrix.m[3][0], 
            viewMatrix.m[3][1], 
            viewMatrix.m[3][2], 
            viewMatrix.m[3][3],
        };

        globalParamsData.projection = glm::perspective(fovRadians, (float)this->scene->getDevice()->getAspectRatio(), nearDist, farDist);
    }

    void View::movePerspective(RT64_VECTOR3 localMovement) {
        glm::vec4 offset = globalParamsData.viewI * glm::vec4{localMovement.x, localMovement.y, localMovement.z, 0.0f};
        globalParamsData.view = (globalParamsData.view * glm::inverse(glm::translate(glm::mat4(1.0f), glm::vec3{offset.x, offset.y, offset.z})));
    }

    void View::rotatePerspective(float localYaw, float localPitch, float localRoll) {
        glm::vec4 viewPos = (globalParamsData.viewI * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f});
        glm::vec4 viewFocus = {0.0f, 0.0f, -farDist, 1.0f};
        glm::mat4 euler = glm::eulerAngleXYZ(localPitch, localYaw, localRoll);
        viewFocus = (euler * viewFocus);
        viewFocus = (globalParamsData.viewI * viewFocus);
        globalParamsData.view = glm::lookAt(
            glm::vec3{viewPos.x, viewPos.y, viewPos.z}, 
            glm::vec3{viewFocus.x, viewFocus.y, viewFocus.z}, 
            glm::vec3{0.0f, 1.0f, 0.0f}
        );
    }

    // Doesn't work correctly
	void View::renderInspector(Inspector* inspector) {
        if (Im3d::GetDrawListCount() > 0) {
            VkCommandBuffer commandBuffer = device->getCurrentCommandBuffer();
            auto viewport = device->getViewport();
            auto scissors = device->getScissors();

            unsigned int totalVertexCount = 0;
            for (Im3d::U32 i = 0, n = Im3d::GetDrawListCount(); i < n; ++i) {
                auto &drawList = Im3d::GetDrawLists()[i];
                totalVertexCount += drawList.m_vertexCount;
            }

            if (totalVertexCount > 0) {
			    // Destroy the previous vertex buffer if it should be bigger.
                if (!im3dVertexBuffer.isNull() && (totalVertexCount > im3dVertexCount)) {
                    im3dVertexBuffer.destroyResource();
                }

                // Create the vertex buffer if it's empty.
                const unsigned int vertexBufferSize = totalVertexCount * sizeof(Im3d::VertexData);
                if (im3dVertexBuffer.isNull()) {
                    VmaAllocationCreateFlags allocFlags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT;
                    device->allocateBuffer(vertexBufferSize, 
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                        VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                        allocFlags, &im3dVertexBuffer);
                    im3dVertexCount = totalVertexCount;
                }

                // Copy im3d draw lists to vertex buffer.
                void* pData;
                uint8_t* pIm3Ddata = (uint8_t*)im3dVertexBuffer.mapMemory(&pData);
                for (int i = 0; i < Im3d::GetDrawListCount(); i++) {
                    auto& drawList = Im3d::GetDrawLists()[i];
                    size_t copySize = sizeof(Im3d::VertexData) * drawList.m_vertexCount;
                    memcpy(pIm3Ddata, drawList.m_vertexData, copySize);
                    pIm3Ddata += copySize;
                }
                im3dVertexBuffer.unmapMemory();

                // Begin the render pass
                VkRenderPassBeginInfo renderPassInfo{};
                renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                renderPassInfo.renderPass = device->getPresentRenderPass();
                renderPassInfo.framebuffer = device->getCurrentSwapchainFramebuffer();
                renderPassInfo.renderArea.offset = {0, 0};
                renderPassInfo.renderArea.extent.width = device->getWidth();
                renderPassInfo.renderArea.extent.height = device->getHeight();
                std::array<VkClearValue, 2> clearValues{};
                clearValues[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
                clearValues[1].depthStencil = {1.0f, 0};
                renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
                renderPassInfo.pClearValues = clearValues.data();
                vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
                vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
                vkCmdSetScissor(commandBuffer, 0, 1, &scissors);
                
                // Draw the im3d stuff
                unsigned int vertexOffset = 0;
                for (int i = 0; i < Im3d::GetDrawListCount(); i++) {
                    auto &drawList = Im3d::GetDrawLists()[i];
                    VkDeviceSize offsets[] = {0};

                    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &im3dVertexBuffer.getBuffer(), offsets);
                    switch (drawList.m_primType) {
                        case Im3d::DrawPrimitive_Points:
                            // d3dCommandList->SetPipelineState(scene->getDevice()->getIm3dPipelineStatePoint());
                            // d3dCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
                            vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
                            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, device->getIm3dPointsPipeline());
                            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, device->getIm3dPointsPipelineLayout(), 0, 1, &device->getIm3dDescriptorSet(), 0, nullptr);
                            break;
                        case Im3d::DrawPrimitive_Lines:
                            // d3dCommandList->SetPipelineState(scene->getDevice()->getIm3dPipelineStateLine());
                            // d3dCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
                            vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
                            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, device->getIm3dLinesPipeline());
                            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, device->getIm3dLinesPipelineLayout(), 0, 1, &device->getIm3dDescriptorSet(), 0, nullptr);
                            break;
                        case Im3d::DrawPrimitive_Triangles:
                            // d3dCommandList->SetPipelineState(scene->getDevice()->getIm3dPipelineStateTriangle());
                            // d3dCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                            vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, device->getIm3dPipeline());
                            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, device->getIm3dPipelineLayout(), 0, 1, &device->getIm3dDescriptorSet(), 0, nullptr);
                            break;
                        default:
                            break;
                    }
                    vkCmdDraw(commandBuffer, drawList.m_vertexCount, 1, vertexOffset, 0);
				    vertexOffset += drawList.m_vertexCount;
                }

                vkCmdEndRenderPass(commandBuffer);
            }
        }
    }

    void  View::setPerspectiveControlActive(bool v) { perspectiveControlActive = v; } 
    void  View::setPerspectiveCanReproject(bool v) { perspectiveCanReproject = v; } 
    float View::getFOVRadians() const { return fovRadians; } 
    float View::getNearDistance() const { return nearDist; } 
    float View::getFarDistance() const { return farDist; } 
    void  View::setDISamples(int v) { globalParamsData.diSamples = v; }
    int   View::getDISamples() const { return globalParamsData.diSamples; }
    void  View::setGISamples(int v) { globalParamsData.giSamples = v; }
    int   View::getGISamples() const { return globalParamsData.giSamples; }
    void  View::setGIBounces(int v) { globalParamsData.giBounces = v; }
    int   View::getGIBounces() const { return globalParamsData.giBounces; } 
    void  View::setMaxLights(int v) { globalParamsData.maxLights = v; } 
    int   View::getMaxLights() const { return globalParamsData.maxLights; } 
    void  View::setTonemappingMode(int v) { globalParamsData.tonemapMode = v; }
    int   View::getTonemappingMode() { return globalParamsData.tonemapMode; }
    void  View::setTonemappingExposure(float v)  { globalParamsData.tonemapExposure = v; }
    float View::getTonemappingExposure() { return globalParamsData.tonemapExposure; }
    void  View::setTonemappingBlack(float v) { globalParamsData.tonemapBlack = v; }
    float View::getTonemappingBlack() { return globalParamsData.tonemapBlack; }
    void  View::setTonemappingWhite(float v) { globalParamsData.tonemapWhite = v; }
    float View::getTonemappingWhite() { return globalParamsData.tonemapWhite; }
    void  View::setTonemappingGamma(float v) { globalParamsData.tonemapGamma = v; }
    float View::getTonemappingGamma() { return globalParamsData.tonemapGamma; }
    void  View::setMotionBlurStrength(float v) { globalParamsData.motionBlurStrength = v; } 
    float View::getMotionBlurStrength() const { return globalParamsData.motionBlurStrength; } 
    void  View::setMotionBlurSamples(int v) { globalParamsData.motionBlurSamples = v; } 
    int   View::getMotionBlurSamples() const { return globalParamsData.motionBlurSamples; }
    void  View::setVisualizationMode(int v) { globalParamsData.visualizationMode = v; } 
    int   View::getVisualizationMode() const { return globalParamsData.visualizationMode; } 
    float View::getResolutionScale() const { return resolutionScale; } 
    void  View::setMaxReflections(int v) { maxReflections = v; } 
    int   View::getMaxReflections() const { return maxReflections; }
    void  View::setDenoiserEnabled(bool v) { denoiserEnabled = v; }
    bool  View::getDenoiserEnabled() const { return denoiserEnabled; }

    void View::setResolutionScale(float v) {
        if (resolutionScale != v) {
            resolutionScale = v;
            recreateRTBuffers = true;
        }
    }
};

// Library exports

DLEXPORT void RT64_SetViewSkyPlane(RT64_VIEW *viewPtr, RT64_TEXTURE *texturePtr) {
	assert(viewPtr != nullptr);
	RT64::View *view = (RT64::View *)(viewPtr);
	RT64::Texture *texture = (RT64::Texture *)(texturePtr);
	view->setSkyPlaneTexture(texture);
}

DLEXPORT RT64_VIEW* RT64_CreateView(RT64_SCENE* scenePtr) {
	assert(scenePtr != nullptr);
	RT64::Scene* scene = (RT64::Scene*)(scenePtr);
	return (RT64_VIEW*)(new RT64::View(scene));
}

DLEXPORT void RT64_SetViewPerspective(RT64_VIEW* viewPtr, RT64_MATRIX4 viewMatrix, float fovRadians, float nearDist, float farDist, bool canReproject) {
	assert(viewPtr != nullptr);
	RT64::View *view = (RT64::View *)(viewPtr);
	view->setPerspective(viewMatrix, fovRadians, nearDist, farDist);
	view->setPerspectiveCanReproject(canReproject);
}

DLEXPORT void RT64_SetViewDescription(RT64_VIEW *viewPtr, RT64_VIEW_DESC viewDesc) {
	assert(viewPtr != nullptr);
	RT64::View *view = (RT64::View *)(viewPtr);
	view->setResolutionScale(viewDesc.resolutionScale);
	view->setMotionBlurStrength(viewDesc.motionBlurStrength);
	view->setMaxLights(viewDesc.maxLights);
	view->setDISamples(viewDesc.diSamples);
	view->setGISamples(viewDesc.giSamples);
	view->setGIBounces(viewDesc.giBounces);
    view->setTonemappingMode(viewDesc.tonemapMode);
    view->setTonemappingExposure(viewDesc.tonemapExposure);
    view->setTonemappingBlack(viewDesc.tonemapBlack);
    view->setTonemappingWhite(viewDesc.tonemapWhite);
    view->setTonemappingGamma(viewDesc.tonemapGamma);
	view->setDenoiserEnabled(viewDesc.denoiserEnabled);
	
	// switch (viewDesc.upscaler) {
	// case RT64_UPSCALER_AUTO:
	// 	// Prefer using DLSS if it's supported on NVIDIA hardware.
	// 	if (view->getUpscalerInitialized(RT64::UpscaleMode::DLSS)) {
	// 		view->setUpscaleMode(RT64::UpscaleMode::DLSS);
	// 	}
	// 	// Prefer using XeSS if it's reported to be on Intel hardware. Initialization is not enough to check for
	// 	// this because XeSS can run on non-native platforms.
	// 	else if (view->getUpscalerInitialized(RT64::UpscaleMode::XeSS) && view->getUpscalerAccelerated(RT64::UpscaleMode::XeSS)) {
	// 		view->setUpscaleMode(RT64::UpscaleMode::XeSS);
	// 	}
	// 	else if (view->getUpscalerInitialized(RT64::UpscaleMode::FSR)) {
	// 		view->setUpscaleMode(RT64::UpscaleMode::FSR);
	// 	}
	// 	else {
	// 		view->setUpscaleMode(RT64::UpscaleMode::Bilinear);
	// 	}

	// 	break;
	// case RT64_UPSCALER_DLSS:
	// 	view->setUpscaleMode(RT64::UpscaleMode::DLSS);
	// 	break;
	// case RT64_UPSCALER_FSR:
	// 	view->setUpscaleMode(RT64::UpscaleMode::FSR);
	// 	break;
	// case RT64_UPSCALER_XESS:
	// 	view->setUpscaleMode(RT64::UpscaleMode::XeSS);
	// 	break;
	// case RT64_UPSCALER_OFF:
	// default:
	// 	view->setUpscaleMode(RT64::UpscaleMode::Bilinear);
	// 	break;
	// }

	// switch (viewDesc.upscalerMode) {
	// case RT64_UPSCALER_MODE_AUTO:
	// 	view->setUpscalerQualityMode(RT64::Upscaler::QualityMode::Auto);
	// 	break;
	// case RT64_UPSCALER_MODE_ULTRA_PERFORMANCE:
	// 	view->setUpscalerQualityMode(RT64::Upscaler::QualityMode::UltraPerformance);
	// 	break;
	// case RT64_UPSCALER_MODE_PERFORMANCE:
	// 	view->setUpscalerQualityMode(RT64::Upscaler::QualityMode::Performance);
	// 	break;
	// case RT64_UPSCALER_MODE_BALANCED:
	// 	view->setUpscalerQualityMode(RT64::Upscaler::QualityMode::Balanced);
	// 	break;
	// case RT64_UPSCALER_MODE_QUALITY:
	// 	view->setUpscalerQualityMode(RT64::Upscaler::QualityMode::Quality);
	// 	break;
	// case RT64_UPSCALER_MODE_ULTRA_QUALITY:
	// 	view->setUpscalerQualityMode(RT64::Upscaler::QualityMode::UltraQuality);
	// 	break;
	// case RT64_UPSCALER_MODE_NATIVE:
	// 	view->setUpscalerQualityMode(RT64::Upscaler::QualityMode::Native);
	// 	break;
	// }

	// view->setUpscalerSharpness(viewDesc.upscalerSharpness);
}

DLEXPORT void RT64_DestroyView(RT64_VIEW* viewPtr) {
	delete (RT64::View*)(viewPtr);
}

#endif