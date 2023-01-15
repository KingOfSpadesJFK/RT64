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