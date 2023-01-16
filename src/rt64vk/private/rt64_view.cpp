/*
*   RT64VK
*/

#ifndef RT64_MINIMAL

#include "../public/rt64.h"

#include <map>
#include <set>

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

	    scene->addView(this);
    }

    View::~View() {
        scene->removeView(this);
    }

    void View::update() { }

    void View::render(float deltaTimeMs) { 
        VkCommandBuffer& commandBuffer = scene->getDevice()->getCurrentCommandBuffer();
        VkViewport viewport = scene->getDevice()->getViewport();
        VkRect2D scissors = scene->getDevice()->getScissors();
        VkRenderPass renderPass = scene->getDevice()->getRenderPass();
        VkFramebuffer framebuffer = scene->getDevice()->getCurrentSwapchainFramebuffer();
        VkExtent2D swapChainExtent = scene->getDevice()->getSwapchainExtent();
        VkPipeline graphicsPipeline = scene->getDevice()->getGraphicsPipeline();
        VkPipelineLayout pipelineLayout = scene->getDevice()->getPipelineLayout();
        VkDescriptorSet descriptorSet = scene->getDevice()->getCurrentDescriptorSet();

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
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

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