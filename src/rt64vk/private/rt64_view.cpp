/*
*   RT64VK
*/

#ifndef RT64_MINIMAL

#include "../public/rt64.h"

#include <map>
#include <set>
#include <chrono>

#include "rt64_device.h"
// #include "rt64_dlss.h"
#include "rt64_instance.h"
#include "rt64_mesh.h"
#include "rt64_scene.h"
#include "rt64_shader.h"
// #include "rt64_texture.h"
#include "rt64_view.h"

// #include "im3d/im3d.h"
// #include "xxhash/xxhash32.h"

namespace RT64
{
    View::View(Scene* scene) {
        this->scene = scene;
        this->device = scene->getDevice();

        createGlobalParamsBuffer();
        createDescriptorPool();
        createDescriptorSets();

	    scene->addView(this);
    }

    View::~View() {
        scene->removeView(this);

        vkDestroyDescriptorPool(device->getVkDevice(), descriptorPool, nullptr);
        globalParamsBuffer.destroyResource();
    }

    void View::createDescriptorPool() {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
        VK_CHECK(vkCreateDescriptorPool(device->getVkDevice(), &poolInfo, nullptr, &descriptorPool));
    }

    // The function used to bind the global params buffer
    void View::createDescriptorSets() {
        std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, device->getDescriptorSetLayout());
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
        allocInfo.pSetLayouts = layouts.data();

        descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
        VK_CHECK(vkAllocateDescriptorSets(device->getVkDevice(), &allocInfo, descriptorSets.data()));

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = *globalParamsBuffer.getResource();
            bufferInfo.offset = 0;
            bufferInfo.range = globalParamsSize;

            VkWriteDescriptorSet descriptorWrite{};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = descriptorSets[i];
            descriptorWrite.dstBinding = 0;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pBufferInfo = &bufferInfo;
            descriptorWrite.pImageInfo = nullptr; // Optional
            descriptorWrite.pTexelBufferView = nullptr; // Optional
            vkUpdateDescriptorSets(device->getVkDevice(), 1, &descriptorWrite, 0, nullptr);
        }
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

    void View::update() {
        updateGlobalParamsBuffer();
    }

    void View::render(float deltaTimeMs) { 
        VkCommandBuffer& commandBuffer = scene->getDevice()->getCurrentCommandBuffer();
        VkViewport viewport = scene->getDevice()->getViewport();
        VkRect2D scissors = scene->getDevice()->getScissors();
        VkRenderPass renderPass = scene->getDevice()->getRenderPass();
        VkFramebuffer framebuffer = scene->getDevice()->getCurrentSwapchainFramebuffer();
        VkExtent2D swapChainExtent = scene->getDevice()->getSwapchainExtent();
        VkPipeline rasterPipeline = scene->getDevice()->getRasterPipeline();
        VkPipelineLayout rasterPipelinLayout = scene->getDevice()->getRasterPipelineLayout();

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
        VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;
        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rasterPipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rasterPipelinLayout, 0, 1, &descriptorSets[device->getCurrentFrameIndex()], 0, nullptr);

        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissors);

        // Draw the meshes
        std::vector<VkBuffer*> vertexBuffers;
        std::vector<VkBuffer*> indexBuffers;
        for (Instance* i : scene->getInstances()) {
            Mesh* mesh = i->getMesh();
            VkBuffer vBuff[] = {*mesh->getVertexBuffer()};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vBuff, offsets);
            vkCmdBindIndexBuffer(commandBuffer, *mesh->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(mesh->getIndexCount()), 1, 0, 0, 0);
        }

        vkCmdEndRenderPass(commandBuffer);
        VK_CHECK(vkEndCommandBuffer(commandBuffer));
    }
};

// Library exports

DLEXPORT RT64_VIEW *RT64_CreateView(RT64_SCENE *scenePtr) {
	assert(scenePtr != nullptr);
	RT64::Scene *scene = (RT64::Scene *)(scenePtr);
	return (RT64_VIEW *)(new RT64::View(scene));
}

DLEXPORT void RT64_DestroyView(RT64_VIEW *viewPtr) {
	delete (RT64::View *)(viewPtr);
}

#endif