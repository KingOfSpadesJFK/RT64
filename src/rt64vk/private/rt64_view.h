/*
*   RT64VK
*/

#pragma once

#include "rt64_common.h"

// Very much incomplete. Might as well just do
#include "rt64_upscaler.h"
#include "rt64_device.h"
// #include "rt64_dlss.h"
// #include "rt64_fsr.h"
// #include "rt64_xess.h"

namespace RT64 
{
	class Scene;
	class Shader;
	class Inspector;
	class Instance;
	class Texture;
    struct DescriptorSetBinding;

	class View {
        private:
            struct RenderInstance {
                Instance *instance;
                const VkBufferView* vertexBufferView;
                const VkBufferView* indexBufferView;
                int indexCount;
                VkBuffer* bottomLevelAS;
                glm::mat4 transform;
                glm::mat4 transformPrevious;
                RT64_MATERIAL material;
                Shader* shader;
                VkRect2D scissorRect;
                VkViewport viewport;
                UINT flags;
            };

            struct GlobalParams {                
                glm::mat4 view;
                glm::mat4 viewI;
                glm::mat4 prevViewI;
                glm::mat4 projection;
                glm::mat4 projectionI;
                glm::mat4 viewProj;
                glm::mat4 prevViewProj;
                RT64_VECTOR4 cameraU;
                RT64_VECTOR4 cameraV;
                RT64_VECTOR4 cameraW;
                RT64_VECTOR4 viewport;
                RT64_VECTOR4 resolution;
                RT64_VECTOR4 ambientBaseColor;
                RT64_VECTOR4 ambientNoGIColor;
                RT64_VECTOR4 eyeLightDiffuseColor;
                RT64_VECTOR4 eyeLightSpecularColor;
                RT64_VECTOR4 skyDiffuseMultiplier;
                RT64_VECTOR4 skyHSLModifier;
                RT64_VECTOR2 pixelJitter;
                float skyYawOffset;
                float giDiffuseStrength;
                float giSkyStrength;
                float motionBlurStrength;
                int skyPlaneTexIndex;
                unsigned int randomSeed;
                unsigned int diSamples;
                unsigned int giSamples;
                unsigned int diReproject;
                unsigned int giReproject;
                unsigned int binaryLockMask;
                unsigned int maxLights;
                unsigned int motionBlurSamples;
                unsigned int visualizationMode;
                unsigned int frameCount;
            };

            Device *device;
            Scene *scene;
            VkDescriptorPool descriptorPool;
            std::vector<VkDescriptorSet> descriptorSets;

            bool imageBuffersInit = false;
            bool recreateImageBuffers = false;

            AllocatedBuffer globalParamsBuffer;
            GlobalParams globalParamsData;
            VkDeviceSize globalParamsSize;
            AllocatedImage depthImage;
            VkImageView depthImageView;

            void createImageBuffers();
            void destroyImageBuffers();

            void createDescriptorPool(DescriptorSetBinding* bindings, uint32_t count);
            void createDescriptorSets(DescriptorSetBinding* bindings, uint32_t count);

            void createGlobalParamsBuffer();
            void updateGlobalParamsBuffer();
            
        public:
            View(Scene *scene);
            virtual ~View();
            void update();
            void render(float deltaTimeMs);
            void resize();

            VkImageView& getDepthImageView();
	};
};