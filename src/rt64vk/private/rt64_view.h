/*
*   RT64VK
*/

#pragma once

#include "rt64_common.h"

// Very much incomplete. Might as well just do
#include "rt64_upscaler.h"
#include "rt64_device.h"
#include <nvvk/raytraceKHR_vk.hpp>
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
                Instance* instance;
                // Don't think Vulkan buffer views work the same way as DX12 buffer views.
                //  If i'm reading this correctly, VkBufferViews are more for texels than stuff like verticies...
                // const VkBufferView* vertexBufferView;
                // const VkBufferView* indexBufferView;
                int indexCount;
                nvvk::AccelKHR* blas;
                nvmath::mat4f transform;
                nvmath::mat4f transformPrevious;
                RT64_MATERIAL material;
                Shader* shader;
                VkRect2D scissorRect;
                VkViewport viewport;
                unsigned int flags;
                unsigned int id;
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

            Device* device;
            Scene* scene;

            bool imageBuffersInit = false;
            bool recreateImageBuffers = false;

            std::vector<VkWriteDescriptorSet> rasterDescriptorSetWrite;
            AllocatedBuffer globalParamsBuffer;
            GlobalParams globalParamsData;
            VkDeviceSize globalParamsSize;
            AllocatedBuffer activeInstancesBufferTransforms;
            VkDeviceSize activeInstancesBufferTransformsSize;
            AllocatedBuffer activeInstancesBufferMaterials;
            VkDeviceSize activeInstancesBufferMaterialsSize;
            AllocatedImage depthImage;
            VkImageView depthImageView;
            VkSampler texSampler;
            nvvk::RaytracingBuilderKHR rtBuilder;
            std::vector<RenderInstance> rasterBgInstances;
            std::vector<RenderInstance> rasterFgInstances;
            std::vector<RenderInstance> rtInstances;
		    std::vector<Texture*> usedTextures;
            bool scissorApplied;
            bool viewportApplied;

            void createOutputBuffers();
            void destroyOutputBuffers();

            void createShaderDescriptorSets(bool updateDescriptors);
            void createShaderBindingTable();
		    void createTopLevelAS(const std::vector<RenderInstance>& rtInstances);

            void createGlobalParamsBuffer();
            void updateGlobalParamsBuffer();
            void createInstanceTransformsBuffer();
            void updateInstanceTransformsBuffer();
            void createInstanceMaterialsBuffer();
            void updateInstanceMaterialsBuffer();
            
        public:
            View(Scene *scene);
            virtual ~View();
            void update();
            void render(float deltaTimeMs);
            void resize();

            VkImageView& getDepthImageView();
            AllocatedBuffer& getGlobalParamsBuffer();
            int getWidth() const;
            int getHeight() const;
	};
};