//
// RT64
//

#pragma once

#include "rt64_common.h"

bool ImGui_ImplVulkan_CreateDeviceObjects();

namespace RT64 {
	class Device;
	class Scene;
	class View;

	class Inspector {
        private:
            Device* device;
            VkDescriptorSetLayout descSetLayout;
            VkDescriptorPool descPool;
            VkDescriptorSet descSet;
            RT64_SCENE_DESC* sceneDesc;
            RT64_MATERIAL* material;
            std::string materialName;
            RT64_LIGHT* lights = nullptr;
            int* lightCount = nullptr;
            int maxLightCount;
            bool cameraControl;
            bool invertCameraX;
            bool invertCameraY;
            float cameraPanX;
            float cameraPanY;
            float cameraRoll = 0.0f;
            float cameraPanSpeed = 1.0f;
            int prevCursorX, prevCursorY;
            std::string dumpPath;
            int dumpFrameCount;
            std::vector<std::string> printMessages;
            bool initialized = false;

            void setupWithView(View *view, long cursorX, long cursorY);
            void renderViewParams(View *view);
            void renderPostInspector(View* view);
            void renderSceneInspector();
            void renderMaterialInspector();
            void renderLightInspector();
            void renderPrint();
            void renderCameraControl();
        public:
            Inspector();
            void controlCamera(View *view, long cursorX, long cursorY);
            bool init(Device* device);
            ~Inspector();
            void destroy();
            void render(View *activeView, long cursorX, long cursorY);
            void resize();
            void setSceneDescription(RT64_SCENE_DESC *sceneDesc);
            void setMaterial(RT64_MATERIAL *material, const std::string& materialName);
            void setLights(RT64_LIGHT *lights, int *lightCount, int maxLightCount);
            void printClear();
            void printMessage(const std::string& message);
	};
};