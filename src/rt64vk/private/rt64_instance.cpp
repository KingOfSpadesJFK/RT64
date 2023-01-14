/*
*   RT64VK
*/

#ifndef RT64_MINIMAL

#include "../public/rt64.h"
#include "rt64_instance.h"
#include "rt64_scene.h"

const RT64_MATERIAL DefaultMaterial;

namespace RT64{
	Instance::Instance(Scene *scene) {
		assert(scene != nullptr);

		this->scene = scene;
		mesh = nullptr;
		diffuseTexture = nullptr;
		normalTexture = nullptr;
		specularTexture = nullptr;
		transform = glm::mat4(1);
		previousTransform = glm::mat4(1);
		material = DefaultMaterial;
		shader = nullptr;
		scissorRect = { 0, 0, 0, 0 };
		viewportRect = { 0, 0, 0, 0 };
		flags = 0;

		scene->addInstance(this);
	}
};
#endif