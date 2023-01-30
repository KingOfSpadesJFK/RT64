/*
*   RT64VK
*/

#pragma once

#include "rt64_common.h"

// Very much incomplete. Might as well just do
#include "rt64_upscaler.h"
#include "rt64_device.h"
#include <nvh/alignment.hpp>
#include <nvvk/raytraceKHR_vk.hpp>
#include <glm/gtx/euler_angles.hpp>
// #include "rt64_dlss.h"
// #include "rt64_fsr.h"
// #include "rt64_xess.h"

#define MAX_QUERIES (16 + 1)
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
                VkBuffer* vertexBuffer;
                VkBuffer* indexBuffer;
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
		    int maxReflections = 4;
            float fovRadians = 40.0f;
            float nearDist;
            float farDist;
            bool perspectiveControlActive;
            bool perspectiveCanReproject = true;

            bool imageBuffersInit = false;
            bool recreateImageBuffers = false;

            std::vector<VkWriteDescriptorSet> rasterDescriptorSetWrite;
            AllocatedBuffer globalParamsBuffer;
            GlobalParams globalParamsData;
            VkDeviceSize globalParamsSize = 0;
            AllocatedBuffer activeInstancesBufferTransforms;
            VkDeviceSize activeInstancesBufferTransformsSize = 0;
            AllocatedBuffer activeInstancesBufferMaterials;
            VkDeviceSize activeInstancesBufferMaterialsSize = 0;
            AllocatedImage depthImage;
            VkImageView depthImageView;
            Texture* skyPlaneTexture;
            std::vector<RenderInstance> rasterBgInstances;
            std::vector<RenderInstance> rasterFgInstances;
            std::vector<RenderInstance> rasterUiInstances;
            std::vector<RenderInstance> rtInstances;
		    std::vector<Texture*> usedTextures;
            bool scissorApplied = false;
            bool viewportApplied = false;
            int rtWidth;
            int rtHeight;
            float resolutionScale = 1.0f;
            unsigned int rtFirstInstanceIdRowWidth;
            bool rtFirstInstanceIdReadbackUpdated;
            bool rtEnabled = true;
            bool rtSwap = false;
            bool rtSkipReprojection = false;

            nvvk::RaytracingBuilderKHR rtBuilder;
            AllocatedBuffer shaderBindingTable;
            VkStridedDeviceAddressRegionKHR primaryRayGenRegion{};
            VkStridedDeviceAddressRegionKHR directRayGenRegion{};
            VkStridedDeviceAddressRegionKHR indirectRayGenRegion{};
            VkStridedDeviceAddressRegionKHR reflectionRayGenRegion{};
            VkStridedDeviceAddressRegionKHR refractionRayGenRegion{};
            VkStridedDeviceAddressRegionKHR missRegion{};
            VkStridedDeviceAddressRegionKHR hitRegion{};
            VkStridedDeviceAddressRegionKHR callRegion{};
            VkDeviceSize sbtSize = 0;

            // The images
            AllocatedImage rasterBg;
            AllocatedImage rtOutput[2];
            AllocatedImage rtViewDirection;
            AllocatedImage rtShadingPosition;
            AllocatedImage rtShadingNormal;
            AllocatedImage rtShadingSpecular;
            AllocatedImage rtDiffuse;
            AllocatedImage rtInstanceId;
            AllocatedImage rtFirstInstanceId;
            AllocatedBuffer rtFirstInstanceIdReadback;
            AllocatedImage rtDirectLightAccum[2];
            AllocatedImage rtFilteredDirectLight[2];
            AllocatedImage rtIndirectLightAccum[2];
            AllocatedImage rtFilteredIndirectLight[2];
            AllocatedImage rtReflection;
            AllocatedImage rtRefraction;
            AllocatedImage rtTransparent;
            AllocatedImage rtFlow;
            AllocatedImage rtReactiveMask;
            AllocatedImage rtLockMask;
            AllocatedImage rtNormal[2];
            AllocatedImage rtDepth[2];
            AllocatedBuffer rtHitDistAndFlow;
            AllocatedBuffer rtHitColor;
            AllocatedBuffer rtHitNormal;
            AllocatedBuffer rtHitSpecular;
            AllocatedBuffer rtHitInstanceId;
            AllocatedImage rtOutputUpscaled;


            void createOutputBuffers();
            void destroyOutputBuffers();

            void updateShaderDescriptorSets(bool updateDescriptors);
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
            RT64_VECTOR3 getViewPosition();
            RT64_VECTOR3 getViewDirection();
            void setPerspective(RT64_MATRIX4 viewMatrix, float fovRadians, float nearDist, float farDist);
            void movePerspective(RT64_VECTOR3 localMovement);
            void rotatePerspective(float localYaw, float localPitch, float localRoll);
            void setPerspectiveControlActive(bool v);
            void setPerspectiveCanReproject(bool v);
            VkImageView& getDepthImageView();
            AllocatedBuffer& getGlobalParamsBuffer();
            void setSkyPlaneTexture(Texture *texture);
            int getWidth() const;
            int getHeight() const;
	};
};