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

        createImageBuffers();

        createGlobalParamsBuffer();

        // Create binding info for the descriptor sets
        // std::vector<DescriptorSetBinding> bindings;
        // DescriptorSetBinding globalParamsBind;
        // globalParamsBind.resource = &globalParamsBuffer;
        // globalParamsBind.size = globalParamsSize;
        // globalParamsBind.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        // globalParamsBind.stage = VK_SHADER_STAGE_VERTEX_BIT;
        // bindings.push_back(globalParamsBind);

        // for (Instance* i : scene->getInstances()) {
        //     DescriptorSetBinding instanceBind;
        //     instanceBind.resource = i->getDiffuseTexture()->getTexture();
        //     instanceBind.size = i->getDiffuseTexture()->getWidth() * i->getDiffuseTexture()->getHeight();
        //     instanceBind.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        //     instanceBind.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        //     instanceBind.imageView = i->getDiffuseTexture()->getTextureImageView();
        //     instanceBind.sampler = i->getDiffuseTexture()->getTextureSampler();
        //     bindings.push_back(instanceBind);
        // }
        
        // device->createRasterPipeline(bindings.data(), bindings.size());

	    scene->addView(this);
    }

    void View::createImageBuffers() {
        
        // If the image buffers have already been made, then destroy them
        if (imageBuffersInit) {
            destroyImageBuffers();
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

    void View::destroyImageBuffers() {
        // Destroy the depth buffer
        depthImage.destroyResource();   
        device->removeDepthImageView(&depthImageView);
        vkDestroyImageView(device->getVkDevice(), depthImageView, nullptr);
    }

    View::~View() {
        scene->removeView(this);

        destroyImageBuffers();
        globalParamsBuffer.destroyResource();
    }

    void View::createShaderDescriptorSets() { 
	    assert(usedTextures.size() <= SRV_TEXTURES_MAX);

        // Create descriptor sets for the instances
        {
            
        }
    }

    // Create a descriptor set for the passed-in instance
    void View::createRasterInstanceDescriptorSet(RenderInstance renderInstance) {
        if (renderInstance.shader->isDescriptorBound()) {
            return;
        }

        std::vector<VkWriteDescriptorSet> descriptorWrites{};
        VkDescriptorBufferInfo gParams_Info {};
        gParams_Info.buffer = *globalParamsBuffer.getBuffer();
        gParams_Info.range = globalParamsSize;
        VkDescriptorBufferInfo instId {};
        instId.buffer = *globalParamsBuffer.getBuffer();
        instId.range = sizeof(uint32_t);
        VkDescriptorBufferInfo transform_Info {};
        transform_Info.buffer = *activeInstancesBufferTransforms.getBuffer();
        transform_Info.range = activeInstancesBufferTransformsSize;
        VkDescriptorBufferInfo materials_Info {};
        materials_Info.buffer = *activeInstancesBufferMaterials.getBuffer();
        materials_Info.range = activeInstancesBufferMaterialsSize;
        VkDescriptorImageInfo textures_Info {};
        textures_Info.imageView = *renderInstance.instance->getDiffuseTexture()->getTextureImageView();
        VkDescriptorImageInfo sampler_Info {};
        sampler_Info.sampler = *renderInstance.instance->getDiffuseTexture()->getTextureSampler();

        VkWriteDescriptorSet write {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = renderInstance.shader->getRasterGroup().descriptorSet;
        write.dstBinding = CBV_INDEX(gParams) + CBV_SHIFT;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &gParams_Info;
        descriptorWrites.push_back(write);
        
        write.dstBinding = 1 + CBV_SHIFT;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &instId;
        descriptorWrites.push_back(write);
        
        write.dstBinding = SRV_INDEX(instanceTransforms) + SRV_SHIFT;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &transform_Info;
        descriptorWrites.push_back(write);
        
        write.dstBinding = SRV_INDEX(instanceMaterials) + SRV_SHIFT;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &materials_Info;
        descriptorWrites.push_back(write);
        
        write.dstBinding = SRV_INDEX(gTextures) + SRV_SHIFT;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.descriptorCount = 1;
        write.pBufferInfo = nullptr;
        write.pImageInfo = &textures_Info;
        descriptorWrites.push_back(write);
        
        write.dstBinding = 10 + SAMPLER_SHIFT; // Very much not good code
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &sampler_Info;
        descriptorWrites.push_back(write);

        vkUpdateDescriptorSets(device->getVkDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }

    void View::createGlobalParamsBuffer() {
        globalParamsSize = sizeof(GlobalParams);
        device->allocateBuffer(
            globalParamsSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
            VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            &globalParamsBuffer
        );
    }

    void View::updateGlobalParamsBuffer() {
        static auto startTime = std::chrono::high_resolution_clock::now();

        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

        glm::vec3 currentPoint = glm::vec3(sinf32(time) * glm::radians(90.0f), cosf32(time) * glm::radians(90.0f), 0.f);
        globalParamsData.view = glm::lookAt(currentPoint, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        globalParamsData.projection = glm::perspective(glm::radians(45.0f), (float)this->scene->getDevice()->getAspectRatio(), 0.1f, 10.0f);
        globalParamsData.projection[1][1] *= -1;
        globalParamsBuffer.setData(&globalParamsData, sizeof(globalParamsData));
    }

    void View::createInstanceTransformsBuffer() {
        uint32_t totalInstances = static_cast<uint32_t>(rtInstances.size() + rasterBgInstances.size() + rasterFgInstances.size());
        uint32_t newBufferSize = totalInstances * sizeof(InstanceTransforms);
        if (activeInstancesBufferTransformsSize != newBufferSize) {
            activeInstancesBufferTransforms.destroyResource();
            scene->getDevice()->allocateBuffer(
                newBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                &activeInstancesBufferTransforms
            );
            activeInstancesBufferTransformsSize = newBufferSize;
        }
    }

    void View::updateInstanceTransformsBuffer() {
        InstanceTransforms* current = nullptr;
        activeInstancesBufferTransforms.mapMemory(reinterpret_cast<void **>(&current));
        // D3D12_CHECK(activeInstancesBufferTransforms.Get()->Map(0, &readRange, reinterpret_cast<void **>(&current)));

        for (const RenderInstance &inst : rtInstances) {
            // Store world transform.
            current->objectToWorld = inst.transform;
            current->objectToWorldPrevious = inst.transformPrevious;

            // Store matrix to transform normal.
            glm::mat4 upper3x3 = current->objectToWorld;
            upper3x3[0][3] = 0.f;
            upper3x3[1][3] = 0.f;
            upper3x3[2][3] = 0.f;
            upper3x3[3][0] = 0.f;
            upper3x3[3][1] = 0.f;
            upper3x3[3][2] = 0.f;
            upper3x3[3][3] = 1.f;

            // current->objectToWorldNormal = XMMatrixTranspose(XMMatrixInverse(&det, upper3x3));
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
                newBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                &activeInstancesBufferMaterials
            );

            activeInstancesBufferMaterialsSize = newBufferSize;
        }
    }

    void View::updateInstanceMaterialsBuffer() {
        RT64_MATERIAL* current = nullptr;
        activeInstancesBufferMaterials.mapMemory(reinterpret_cast<void **>(&current));

        for (const RenderInstance &inst : rtInstances) {
            *current = inst.material;
            current++;
        }

        for (const RenderInstance &inst : rasterBgInstances) {
            *current = inst.material;
            current++;
        }

        for (const RenderInstance& inst : rasterFgInstances) {
            *current = inst.material;
            current++;
        }

        activeInstancesBufferMaterials.unmapMemory();
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
                // renderInstance.bottomLevelAS = usedMesh->getBottomLevelASResult();
                renderInstance.transform = instance->getTransform();
                renderInstance.transformPrevious = instance->getPreviousTransform();
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

                if (renderInstance.bottomLevelAS != nullptr) {
                    rtInstances.push_back(renderInstance);
                }
                else if (instFlags & RT64_INSTANCE_RASTER_BACKGROUND) {
                    rasterBgInstances.push_back(renderInstance);
                }
                else {
                    rasterFgInstances.push_back(renderInstance);
                }
            }

            // Create the acceleration structures used by the raytracer.
            if (!rtInstances.empty()) {
                // createTopLevelAS(rtInstances);
            }

            // Create the instance buffers for the active instances (if necessary).
            createInstanceTransformsBuffer();
            createInstanceMaterialsBuffer();
            
            // Create the buffer containing the raytracing result (always output in a
            // UAV), and create the heap referencing the resources used by the raytracing,
            // such as the acceleration structure
            // createShaderResourceHeap();
            
            // Create the shader binding table and indicating which shaders
            // are invoked for each instance in the AS.
            // createShaderBindingTable();

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

    void View::render(float deltaTimeMs) { 
        VkCommandBuffer& commandBuffer = scene->getDevice()->getCurrentCommandBuffer();
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
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rasterGroup.pipelineLayout, 0, 1, &rasterGroup.descriptorSet, 0, nullptr);
                    previousShader = renderInstance.shader;
                }

                VkDeviceSize offsets[] = {0};
                createRasterInstanceDescriptorSet(renderInstance);
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

        // Draw the raster images
        // Draw the background instances to the screen.
        RT64_LOG_PRINTF("Drawing background instances");
        resetScissor();
        resetViewport();
	    drawInstances(rasterFgInstances, (uint32_t)(rasterBgInstances.size() + rtInstances.size()), true);

        vkCmdEndRenderPass(commandBuffer);
        VK_CHECK(vkEndCommandBuffer(commandBuffer));
    }

    // Public

    VkImageView& View::getDepthImageView() { return depthImageView; }

    void View::resize() {
        createImageBuffers();
    }

    int View::getWidth() const {
        return scene->getDevice()->getWidth();
    }

    int View::getHeight() const {
        return scene->getDevice()->getHeight();
    }
};

// Library exports

DLEXPORT RT64_VIEW* RT64_CreateView(RT64_SCENE* scenePtr) {
	assert(scenePtr != nullptr);
	RT64::Scene* scene = (RT64::Scene*)(scenePtr);
	return (RT64_VIEW*)(new RT64::View(scene));
}

DLEXPORT void RT64_DestroyView(RT64_VIEW* viewPtr) {
	delete (RT64::View*)(viewPtr);
}

#endif