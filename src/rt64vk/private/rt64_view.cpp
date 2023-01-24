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

// #include "im3d/im3d.h"
// #include "xxhash/xxhash32.h"

namespace RT64
{
    View::View(Scene* scene) {
        this->scene = scene;
        this->device = scene->getDevice();
        device->initRTBuilder(rtBuilder);

        createOutputBuffers();

        createGlobalParamsBuffer();

        // Create a generic texture sampler
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
        vkCreateSampler(device->getVkDevice(), &samplerInfo, nullptr, &texSampler);

	    scene->addView(this);
    }

    void View::createOutputBuffers() {
        
        // If the image buffers have already been made, then destroy them
        if (imageBuffersInit) {
            destroyOutputBuffers();
        }

        // Create the depth buffer
        VK_CHECK(device->allocateImage(
            device->getSwapchainExtent().width, 
            device->getSwapchainExtent().height, 
            VK_IMAGE_TYPE_2D, 
            VK_FORMAT_D32_SFLOAT, 
            VK_IMAGE_TILING_OPTIMAL, 
            VK_IMAGE_LAYOUT_UNDEFINED, 
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 
            VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT, 
            &depthImage));
        depthImageView = device->createImageView(*depthImage.getImage(), VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);
        this->device->addDepthImageView(&depthImageView);

        imageBuffersInit = true;
    }

    void View::destroyOutputBuffers() {
        // Destroy the depth buffer
        depthImage.destroyResource();   
        device->removeDepthImageView(&depthImageView);
        vkDestroyImageView(device->getVkDevice(), depthImageView, nullptr);
    }

    View::~View() {
        scene->removeView(this);

        vkDestroySampler(device->getVkDevice(), texSampler, nullptr);
        destroyOutputBuffers();
        globalParamsBuffer.destroyResource();
        activeInstancesBufferMaterials.destroyResource();
        activeInstancesBufferTransforms.destroyResource();
        shaderBindingTable.destroyResource();

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
        globalParamsBuffer.setDescriptorInfo(globalParamsSize, 0);
    }

    void View::updateGlobalParamsBuffer() {
        static auto startTime = std::chrono::high_resolution_clock::now();

        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

        #define RADIUS 10.0f
        #define YOFF 2.0f
        glm::vec3 eye = glm::vec3(sinf32(time) * glm::radians(90.0f) * RADIUS, YOFF, cosf32(time) * glm::radians(90.0f) * RADIUS);
        globalParamsData.view = glm::lookAt(eye, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        globalParamsData.projection = glm::perspective(glm::radians(45.0f), (float)this->scene->getDevice()->getAspectRatio(), 0.1f, 1000.0f);
        globalParamsData.projection[1][1] *= -1;
        globalParamsBuffer.setData(&globalParamsData, sizeof(globalParamsData));
    }

    void View::createInstanceTransformsBuffer() {
        uint32_t totalInstances = static_cast<uint32_t>(rtInstances.size() + rasterBgInstances.size() + rasterFgInstances.size());
        uint32_t newBufferSize = ROUND_UP(totalInstances * sizeof(InstanceTransforms), CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        if (activeInstancesBufferTransformsSize != newBufferSize) {
            activeInstancesBufferTransforms.destroyResource();
            scene->getDevice()->allocateBuffer(
                newBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                &activeInstancesBufferTransforms
            );
            activeInstancesBufferTransforms.setDescriptorInfo(newBufferSize, 0);
            activeInstancesBufferTransformsSize = newBufferSize;
        }
    }

    void View::updateInstanceTransformsBuffer() {
        InstanceTransforms* current = nullptr;
        activeInstancesBufferTransforms.mapMemory(reinterpret_cast<void **>(&current));
        // D3D12_CHECK(activeInstancesBufferTransforms.Get()->Map(0, &readRange, reinterpret_cast<void **>(&current)));

        for (const RenderInstance &inst : rtInstances) {
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

            current++;
        } 

        activeInstancesBufferTransforms.unmapMemory();
    }

    void View::createInstanceMaterialsBuffer() {
        uint32_t totalInstances = static_cast<uint32_t>(rtInstances.size() + rasterBgInstances.size() + rasterFgInstances.size());
        uint32_t newBufferSize = ROUND_UP(totalInstances * sizeof(RT64_MATERIAL), CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        if (activeInstancesBufferMaterialsSize != newBufferSize) {
            activeInstancesBufferMaterials.destroyResource();
            device->allocateBuffer(
                newBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                &activeInstancesBufferMaterials
            );

            activeInstancesBufferMaterials.setDescriptorInfo(newBufferSize, 0);
            activeInstancesBufferMaterialsSize = newBufferSize;
        }
    }

    void View::updateInstanceMaterialsBuffer() {
        RT64_MATERIAL* current = nullptr;
        activeInstancesBufferMaterials.mapMemory(reinterpret_cast<void**>(&current));

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

        activeInstancesBufferMaterials.unmapMemory();
    }

    void View::createShaderDescriptorSets(bool updateDescriptors) { 
	    assert(usedTextures.size() <= SRV_TEXTURES_MAX);

        VkWriteDescriptorSet write {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

        // Create descriptor sets for the instances
        if (updateDescriptors)
        {
            // First step is create the infos
            VkDescriptorBufferInfo gParams_Info {};
            gParams_Info.buffer = *globalParamsBuffer.getBuffer();
            gParams_Info.range = globalParamsSize;

            VkDescriptorBufferInfo transform_Info {};
            transform_Info.buffer = *activeInstancesBufferTransforms.getBuffer();
            transform_Info.range = activeInstancesBufferTransformsSize;

            VkDescriptorBufferInfo materials_Info {};
            materials_Info.buffer = *activeInstancesBufferMaterials.getBuffer();
            materials_Info.range = activeInstancesBufferMaterialsSize;

            std::vector<VkDescriptorImageInfo> texture_infos;
            texture_infos.resize(usedTextures.size());
            for (int i = 0; i < usedTextures.size(); i++) {
                VkDescriptorImageInfo currTexInfo {};
                currTexInfo.imageView = *usedTextures[i]->getTextureImageView();
                currTexInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                texture_infos[i] = currTexInfo;
            }

            VkDescriptorImageInfo sampler_Info {};
            sampler_Info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            sampler_Info.sampler = texSampler;

            // Then create the descriptor writes
            std::vector<VkWriteDescriptorSet> descriptorWrites{};
            write.descriptorCount = 1;
            write.dstBinding = CBV_INDEX(gParams) + CBV_SHIFT;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.pBufferInfo = &gParams_Info;
            descriptorWrites.push_back(write);
            
            write.dstBinding = SRV_INDEX(instanceTransforms) + SRV_SHIFT;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.pBufferInfo = &transform_Info;
            descriptorWrites.push_back(write);
            
            write.dstBinding = SRV_INDEX(instanceMaterials) + SRV_SHIFT;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.pBufferInfo = &materials_Info;
            descriptorWrites.push_back(write);
            
            write.dstBinding = SRV_INDEX(gTextures) + SRV_SHIFT;
            write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            write.descriptorCount = texture_infos.size();
            write.pBufferInfo = nullptr;
            write.pImageInfo = texture_infos.data();
            descriptorWrites.push_back(write);
            
            write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &sampler_Info;
            descriptorWrites.push_back(write);

            // Write to the instances' descriptor sets
            for (RenderInstance& r : rtInstances) {
                for (VkWriteDescriptorSet& w : descriptorWrites) {
                    w.dstSet = r.shader->getRasterGroup().descriptorSet;
                }
                descriptorWrites[4].dstBinding = r.shader->getSamplerRegisterIndex() + SAMPLER_SHIFT;
                vkUpdateDescriptorSets(device->getVkDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
            }
        }
    }

    void View::update() 
    {
	    RT64_LOG_PRINTF("Started view update");
        updateGlobalParamsBuffer();

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

        if (!scene->getInstances().empty()) {
            // Create the active instance vectors.
            RenderInstance renderInstance;
            Mesh* usedMesh = nullptr;
            Texture *usedDiffuse = nullptr;
            size_t totalInstances = scene->getInstances().size();
		    bool updateDescriptors = (totalInstances != (rtInstances.size() + rasterBgInstances.size() + rasterFgInstances.size()));
            unsigned int instFlags = 0;
            unsigned int screenHeight = getHeight();
            rtInstances.clear();
            rasterBgInstances.clear();
            rasterFgInstances.clear();

            rtInstances.reserve(totalInstances);
            rasterBgInstances.reserve(totalInstances);
            rasterFgInstances.reserve(totalInstances);

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
                renderInstance.flags = (instFlags & RT64_INSTANCE_DISABLE_BACKFACE_CULLING) ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
                renderInstance.material.diffuseTexIndex = getTextureIndex(instance->getDiffuseTexture());
                renderInstance.material.normalTexIndex = getTextureIndex(instance->getNormalTexture());
                renderInstance.material.specularTexIndex = getTextureIndex(instance->getSpecularTexture());

                if (!instance->hasScissorRect()) {
                    RT64_RECT rect = instance->getScissorRect();
                    renderInstance.scissorRect.offset.x = rect.x;
                    renderInstance.scissorRect.offset.y = screenHeight - rect.y - rect.h;
                    renderInstance.scissorRect.extent.width = rect.w;
                    renderInstance.scissorRect.extent.height = rect.y;
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

                if (usedMesh->getBlasAddress() != (VkDeviceAddress)nullptr) {
                    rtInstances.push_back(renderInstance);
                }
                else if (instFlags & RT64_INSTANCE_RASTER_BACKGROUND) {
                    rasterBgInstances.push_back(renderInstance);
                }
                else {
                    // rtInstances.push_back(renderInstance);
                    rasterFgInstances.push_back(renderInstance);
                }
            }

            // Create the acceleration structures used by the raytracer.
            if (!rtInstances.empty()) {
                createTopLevelAS(rtInstances);
            }

            // Create the instance buffers for the active instances (if necessary).
            createInstanceTransformsBuffer();
            createInstanceMaterialsBuffer();
            
            // Create the buffer containing the raytracing result (or atleast soon), 
            //  and create the descriptor sets referencing the resources used 
            //  by the raytracing, such as the acceleration structure
            createShaderDescriptorSets(updateDescriptors);
            
            // Create the shader binding table and indicating which shaders
            // are invoked for each instance in the AS.
            createShaderBindingTable();

            // Update the instance buffers for the active instances.
            updateInstanceTransformsBuffer();
            updateInstanceMaterialsBuffer();
        } else {
            rtInstances.clear();
            rasterBgInstances.clear();
            rasterFgInstances.clear();
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
            rayInst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            rayInst.mask = 0xFF;
            tlas.emplace_back(rayInst);
            id++;
        }
        rtBuilder.buildTlas(tlas, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
    }

    // Get all the RT shader handles and write them into an SBT buffer
    //  From nvpro-samples
    void View::createShaderBindingTable() {
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProperties = device->getRTProperties();
        unsigned int missCount = 0;                                                 // How many miss shaders we have
        unsigned int hitCount = rtInstances.size();                                 // How many hit shaders we have
        unsigned int handleCount = SHADER_INDEX(MAX) + missCount + hitCount;        // How many rt shaders in total we have
        unsigned int handleSize = rtProperties.shaderGroupHandleSize;
        // The SBT (buffer) need to have starting groups to be aligned and handles in the group to be aligned.
        unsigned int handleSizeAligned = nvh::align_up(handleSize, rtProperties.shaderGroupHandleAlignment);

        raygenRegion.stride = nvh::align_up(handleSizeAligned, rtProperties.shaderGroupBaseAlignment);
        raygenRegion.size = raygenRegion.stride;    // The size member of pRayGenShaderBindingTable must be equal to its stride member
        missRegion.stride = handleSizeAligned;
        missRegion.size = nvh::align_up(missCount * handleSizeAligned, rtProperties.shaderGroupBaseAlignment);
        hitRegion.stride = handleSizeAligned;
        hitRegion.size = nvh::align_up(hitCount * handleSizeAligned, rtProperties.shaderGroupBaseAlignment);

        // Get the shader group handles
        unsigned int dataSize = handleCount * handleSize;
        std::vector<uint8_t> handles(dataSize);
        VkResult res = vkGetRayTracingShaderGroupHandlesKHR(device->getVkDevice(), device->getRTPipeline(), 0, handleCount, dataSize, handles.data());

        // Get the new size of the sbt
        VkDeviceSize newSbtSize = raygenRegion.size + missRegion.size + hitRegion.size + callRegion.size;
        // If it's the same size, then just return since there's no need to do this whole thing again
        if (newSbtSize == sbtSize) {
            return;
        }

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
        shaderBindingTable.setAllocationName("ShaderBindingTable");
        sbtSize = newSbtSize;

        // Find the SBT addresses of each group
        VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr, *shaderBindingTable.getBuffer()};
        VkDeviceAddress sbtAddress = vkGetBufferDeviceAddress(device->getVkDevice(), &info);
        raygenRegion.deviceAddress = sbtAddress;
        missRegion.deviceAddress = sbtAddress + raygenRegion.size;
        hitRegion.deviceAddress = sbtAddress + raygenRegion.size + missRegion.size;

        // Helper to retrieve the handle data
        auto getHandle = [&](int i) { return handles.data() + i * handleSize; };

        // Map the SBT buffer and write in the handles
        uint8_t* pSBTBuffer;
        uint8_t* pData = reinterpret_cast<uint8_t*>(shaderBindingTable.mapMemory((void**)&pSBTBuffer));
        uint32_t handleIdx = 0;

        // Raygen
        memcpy(pData, getHandle(handleIdx++), handleSize);

        // Miss
        pData = pSBTBuffer + raygenRegion.size;
        for(uint32_t c = 0; c < missCount; c++)
        {
            memcpy(pData, getHandle(handleIdx++), handleSize);
            pData += missRegion.stride;
        }

        // Hit
        pData = pSBTBuffer + raygenRegion.size + missRegion.size;
        for(uint32_t c = 0; c < hitCount; c++)
        {
            memcpy(pData, getHandle(handleIdx++), handleSize);
            pData += hitRegion.stride;
        }
        shaderBindingTable.unmapMemory();
    }

    void View::render(float deltaTimeMs) { 
        VkCommandBuffer commandBuffer = scene->getDevice()->getCurrentCommandBuffer();
        VkViewport viewport = scene->getDevice()->getViewport();
        VkRect2D scissors = scene->getDevice()->getScissors();
        VkRenderPass renderPass = scene->getDevice()->getRenderPass();
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
            if (rect.extent.width > 0 & rect.extent.height > 0) {
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
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, renderInstance.instance->getMesh()->getVertexBuffer(), offsets);
                vkCmdBindIndexBuffer(commandBuffer, *renderInstance.instance->getMesh()->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(renderInstance.instance->getMesh()->getIndexCount()), 1, 0, 0, 0);
            }
        };

        // Begin the command buffer
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = 0; // Optional
        beginInfo.pInheritanceInfo = nullptr; // Optional
        VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffer;
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapChainExtent;
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();
        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissors);

        // vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rasterGroup.pipeline);
        // vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rasterGroup.pipelineLayout, 0, 1, &renderInstance.shader->getRasterGroup().descriptorSet, 0, nullptr);

        // Draw the raster images
        // Draw the background instances to the screen.
        RT64_LOG_PRINTF("Drawing background instances");
        resetScissor();
        resetViewport();
	    drawInstances(rtInstances, 0, true);
	    // drawInstances(rasterFgInstances, rtInstances.size() + rasterBgInstances.size(), true);

        vkCmdEndRenderPass(commandBuffer);
        VK_CHECK(vkEndCommandBuffer(commandBuffer));
    }

    // Public

    VkImageView& View::getDepthImageView() { return depthImageView; }

    void View::setSkyPlaneTexture(Texture *texture) {
        skyPlaneTexture = texture;
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

DLEXPORT void RT64_DestroyView(RT64_VIEW* viewPtr) {
	delete (RT64::View*)(viewPtr);
}

#endif