/*
*   RT64VK
*/

#pragma once

#include "rt64_common.h"

namespace RT64 {
	class Device;
	class Inspector;
	class Instance;
	class View;

	// A light struct that's compatible with Vulkan's memory stride requirements.
	typedef struct {
		alignas(16) RT64_VECTOR3 position;
		alignas(16) RT64_VECTOR3 diffuseColor;
		alignas(16) RT64_VECTOR3 specularColor;
		float attenuationRadius;
		float pointRadius;
		float shadowOffset;
		float attenuationExponent;
		float flickerIntensity;
		unsigned int groupBits;
	} Light;

	class Scene {
	private:
		Device* device;
		std::vector<Instance*> instances;
		std::vector<View*> views;
		AllocatedBuffer lightsBuffer;
		size_t lightsBufferSize;
		int lightsCount;
		RT64_SCENE_DESC description;
	public:
		Scene(Device* device);
		virtual ~Scene();
		void update();
		void render(float deltaTimeMs);
		void resize();
		void setDescription(RT64_SCENE_DESC v);
		RT64_SCENE_DESC getDescription() const;
		void setLights(RT64_LIGHT* lightArray, int lightCount);
		int getLightsCount() const;
		AllocatedBuffer& getLightsBuffer();
		void addInstance(Instance* instance);
		void removeInstance(Instance* instance);
		void addView(View* view);
		void removeView(View* view);
		const std::vector<View*>& getViews() const;
		const std::vector<Instance*>& getInstances() const;
		Device* getDevice() const;
	};
};