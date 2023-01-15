/*
*   RT64VK
*/

#ifndef RT64_MINIMAL

#include "../public/rt64.h"
#include "rt64_instance.h"
#include "rt64_scene.h"

const RT64_MATERIAL DefaultMaterial {};

namespace RT64{
	Instance::Instance(Scene* scene) {
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

	Instance::~Instance() {
		scene->removeInstance(this);
		delete mesh;
	}

	void Instance::setMesh(Mesh* mesh) {
		this->mesh = mesh;
	}

	Mesh* Instance::getMesh() const {
		return mesh;
	}

	void Instance::setMaterial(const RT64_MATERIAL &material) {
		this->material = material;
	}

	const RT64_MATERIAL &Instance::getMaterial() const {
		return material;
	}

	void Instance::setShader(Shader* shader) {
		this->shader = shader;
	}

	Shader *Instance::getShader() const {
		return shader;
	}

	void Instance::setDiffuseTexture(Texture *texture) {
		this->diffuseTexture = texture;
	}

	Texture *Instance::getDiffuseTexture() const {
		return diffuseTexture;
	}

	void Instance::setNormalTexture(Texture* texture) {
		this->normalTexture = texture;
	}

	Texture* Instance::getNormalTexture() const {
		return normalTexture;
	}

	void Instance::setSpecularTexture(Texture* texture) {
		this->specularTexture = texture;
	}

	Texture* Instance::getSpecularTexture() const {
		return specularTexture;
	}

	inline glm::mat4 matrixFromFloats(float m[4][4]) {		
		return glm::mat4(
			m[0][0], m[0][1], m[0][2], m[0][3],
			m[1][0], m[1][1], m[1][2], m[1][3],
			m[2][0], m[2][1], m[2][2], m[2][3],
			m[3][0], m[3][1], m[3][2], m[3][3]
		);
	}

	void Instance::setTransform(float m[4][4]) {
		transform = matrixFromFloats(m);
	}

	glm::mat4 Instance::getTransform() const {
		return transform;
	}

	void Instance::setPreviousTransform(float m[4][4]) {
		previousTransform = matrixFromFloats(m);
	}

	glm::mat4 Instance::getPreviousTransform() const {
		return previousTransform;
	}

	void Instance::setScissorRect(const RT64_RECT &rect) {
		scissorRect = rect;
	}

	RT64_RECT Instance::getScissorRect() const {
		return scissorRect;
	}

	bool Instance::hasScissorRect() const {
		return (scissorRect.w > 0) && (scissorRect.h > 0);
	}

	void Instance::setViewportRect(const RT64_RECT &rect) {
		viewportRect = rect;
	}

	RT64_RECT Instance::getViewportRect() const {
		return viewportRect;
	}

	bool Instance::hasViewportRect() const {
		return (viewportRect.w > 0) && (viewportRect.h > 0);
	}

	void Instance::setFlags(int v) {
		flags = v;
	}

	unsigned int Instance::getFlags() const {
		return flags;
	}
};

// Library functions

DLEXPORT RT64_INSTANCE *RT64_CreateInstance(RT64_SCENE *scenePtr) {
	RT64::Scene *scene = (RT64::Scene *)(scenePtr);
	RT64::Instance *instance = new RT64::Instance(scene);
	return (RT64_INSTANCE *)(instance);
}

DLEXPORT void RT64_SetInstanceDescription(RT64_INSTANCE *instancePtr, RT64_INSTANCE_DESC instanceDesc) {
	assert(instancePtr != nullptr);
	assert(instanceDesc.mesh != nullptr);
	// assert(instanceDesc.diffuseTexture != nullptr);		lol still need to work on that
	// assert(instanceDesc.shader != nullptr);

	RT64::Instance *instance = (RT64::Instance *)(instancePtr);
	instance->setMesh((RT64::Mesh *)(instanceDesc.mesh));
	instance->setTransform(instanceDesc.transform.m);
	instance->setPreviousTransform(instanceDesc.previousTransform.m);
	instance->setMaterial(instanceDesc.material);
	instance->setShader((RT64::Shader *)(instanceDesc.shader));
	instance->setDiffuseTexture((RT64::Texture *)(instanceDesc.diffuseTexture));
	instance->setNormalTexture((RT64::Texture *)(instanceDesc.normalTexture));
	instance->setSpecularTexture((RT64::Texture *)(instanceDesc.specularTexture));
	instance->setFlags(instanceDesc.flags);
	instance->setScissorRect(instanceDesc.scissorRect);
	instance->setViewportRect(instanceDesc.viewportRect);
}

DLEXPORT void RT64_DestroyInstance(RT64_INSTANCE *instancePtr) {
	delete (RT64::Instance *)(instancePtr);
}

#endif