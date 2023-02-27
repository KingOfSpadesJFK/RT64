/*
*   RT64VK
*/

#pragma once

#include "rt64_common.h"

#include "rt64_view.h"
#include "rt64_scene.h"
#include "rt64_mesh.h"
#include "rt64_texture.h"
#include "rt64_shader.h"
#include "rt64_inspector.h"
#include "rt64_mipmaps.h"

#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <optional>
#include <vulkan/vulkan.h>
#include <array>
#include <nvvk/context_vk.hpp>
#include <nvvk/raytraceKHR_vk.hpp>
#include <nvvk/resourceallocator_vk.hpp>
#include <unordered_map>
#include <GLFW/glfw3.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_vulkan.h>

#ifdef _WIN32
#endif

#define MAX_FRAMES_IN_FLIGHT    2

#define PS_ENTRY    "PSMain"
#define VS_ENTRY    "VSMain"
#define GS_ENTRY    "GSMain"
#define CS_ENTRY    "mainCS"

// The windows
#ifdef _WIN32
//  #define RT64_WINDOW HWND
#define RT64_WINDOW  GLFWwindow
#else
#define RT64_WINDOW GLFWwindow
#endif

namespace RT64
{
	class Scene;
	class Shader;
	class Mesh;
	class Texture;
	class Inspector;
	class Mipmaps;

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;

        bool isComplete() {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    };

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    struct RaygenPushConstant {
        float bounceDivisor;
        float currentBounce;
    };
    
    class Device
    {
        private:
            nvvk::Context vkctx {};
            VkPhysicalDevice& physicalDevice = vkctx.m_physicalDevice;
            VkInstance& vkInstance = vkctx.m_instance;
            VkDevice& vkDevice = vkctx.m_device;
            VkQueue& graphicsQueue = vkctx.m_queueGCT.queue;
            VkQueue& presentQueue = vkctx.m_queueGCT.queue;
            VkDebugUtilsMessengerEXT debugMessenger;
            VkSwapchainKHR swapChain;
            std::vector<VkImage> swapChainImages;
            VkFormat swapChainImageFormat;
            VkExtent2D swapChainExtent;
            std::vector<VkImageView> swapChainImageViews;
            Mipmaps* mipmaps = nullptr;
            bool disableMipmaps = false;

            inline void createVkInstanceNV();
            void createVKInstance();
            void setupDebugMessenger();
            void pickPhysicalDevice();
            void createLogicalDevice();

            bool isDeviceSuitable(VkPhysicalDevice device);
            std::vector<const char*> getInstanceExtensions();
            bool checkDeviceExtensionSupport(VkPhysicalDevice device);
            void hasGflwRequiredInstanceExtensions();
            void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo);
            QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
            SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
            VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
            VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
            VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

            static void framebufferResizeCallback(RT64_WINDOW* window, int width, int height);

#ifndef RT64_MINIMAL
            void createDxcCompiler();
            void createMemoryAllocator();
            void createSurface();
            void cleanupSwapChain();
            void createSwapChain();
            void createImageViews();
            void createCommandPool();
            void createCommandBuffers();
            void createFramebuffers();
            void createSyncObjects();
            void createRayTracingPipeline();
            void createDescriptorPool();
            void loadAssets();

            void initRayTracing();
            void recreateSwapChain();
            bool updateSize(VkResult result, const char* error);
            void updateViewport();
            void updateScenes();
            void resizeScenes();
            void generateRTDescriptorSetLayout();
            void loadBlueNoise();
            void generateSamplers();

            RT64_WINDOW* window;
            VkSurfaceKHR vkSurface;
            int width;
            int height;
            float anisotropy = 1.0f;
            float aspectRatio;
            bool framebufferCreated = false;
            bool framebufferResized = false;

            std::vector<Scene*> scenes;
            std::vector<Shader*> shaders;
            std::vector<Mesh*> meshes;
            std::vector<Texture*> textures;
            std::unordered_map<unsigned int, VkSampler> samplers;

            Inspector inspector;
            bool showInspector = false;
            
		    Texture* blueNoise;
            VkSampler gaussianSampler;
            VkSampler composeSampler;
            VkSampler tonemappingSampler;
            VkSampler postProcessSampler;

            VkPhysicalDeviceProperties physDeviceProperties {};   // Properties of the physical device
            VmaAllocator allocator;
            VkRenderPass presentRenderPass;         // The render pass for presenting to the screen
            VkRenderPass offscreenRenderPass;       // The render pass for rendering to an r32g32b32a32 image
            std::vector<VkFramebuffer> swapChainFramebuffers;

            VkCommandPool commandPool;
            std::vector<VkCommandBuffer> commandBuffers;

            std::vector<VkSemaphore> imageAvailableSemaphores;
            std::vector<VkSemaphore> renderFinishedSemaphores;
            std::vector<VkFence> inFlightFences;
            std::vector<VkImageView*> depthViews;

            bool rtStateDirty = false;
            bool descPoolDirty = false;
            bool recreateSamplers = false;
            VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProperties {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
            nvvk::RaytracingBuilderKHR rtBlasBuilder;
            nvvk::ResourceAllocatorDma rtAllocator;
            std::vector<VkDescriptorSetLayout> rtDescriptorSetLayouts;
            std::vector<VkDescriptorSet> rtDescriptorSets;

            std::vector<VkDescriptorPoolSize> descriptorPoolBindings;
            VkDescriptorPool descriptorPool;

            uint32_t currentFrame = 0;
            uint32_t framebufferIndex = 0;
            uint32_t overallShaderCount = 0;
            uint32_t rasterShaderCount = 0;
            uint32_t hitShaderCount = 0;
            uint32_t firstShaderNullSpot = 0;

            VkViewport vkViewport;
            VkRect2D vkScissorRect;

            IDxcCompiler* d3dDxcCompiler;   // Who invited my man blud XDXDXD
            IDxcLibrary* d3dDxcLibrary;     // Bro thinks he's on the team  XDXDXDXDXDXD

            //***********************************************************
            // The Shaders
            VkShaderModule primaryRayGenModule;
            VkShaderModule directRayGenModule;
            VkShaderModule indirectRayGenModule;
            VkShaderModule reflectionRayGenModule;
            VkShaderModule refractionRayGenModule;
            VkShaderModule surfaceMissModule;
            VkShaderModule shadowMissModule;
            VkShaderModule fullscreenVSModule;
            VkShaderModule composePSModule;
            VkShaderModule postProcessPSModule;
            VkShaderModule tonemappingPSModule;
            VkShaderModule debugPSModule;
            VkShaderModule im3dVSModule;
            VkShaderModule im3dPSModule;
            VkShaderModule im3dGSPointsModule;
            VkShaderModule im3dGSLinesModule;
            VkShaderModule gaussianFilterRGB3x3CSModule;
            // And their shader stage infos
            VkPipelineShaderStageCreateInfo primaryRayGenStage          {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            VkPipelineShaderStageCreateInfo directRayGenStage           {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            VkPipelineShaderStageCreateInfo indirectRayGenStage         {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            VkPipelineShaderStageCreateInfo reflectionRayGenStage       {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            VkPipelineShaderStageCreateInfo refractionRayGenStage       {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            VkPipelineShaderStageCreateInfo surfaceMissStage            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            VkPipelineShaderStageCreateInfo shadowMissStage             {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            VkPipelineShaderStageCreateInfo fullscreenVSStage           {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            VkPipelineShaderStageCreateInfo composePSStage              {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            VkPipelineShaderStageCreateInfo postProcessPSStage          {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            VkPipelineShaderStageCreateInfo tonemappingPSStage          {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            VkPipelineShaderStageCreateInfo debugPSStage                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            VkPipelineShaderStageCreateInfo im3dVSStage                 {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            VkPipelineShaderStageCreateInfo im3dPSStage                 {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            VkPipelineShaderStageCreateInfo im3dGSPointsStage           {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            VkPipelineShaderStageCreateInfo im3dGSLinesStage            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            VkPipelineShaderStageCreateInfo gaussianFilterRGB3x3CSStage {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            // And pipelines
            VkPipelineLayout        rtPipelineLayout;
            VkPipeline              rtPipeline;
            VkPipelineLayout        composePipelineLayout;
            VkPipeline              composePipeline;
            VkPipelineLayout        tonemappingPipelineLayout;
            VkPipeline              tonemappingPipeline;
            VkPipelineLayout        postProcessPipelineLayout;
            VkPipeline              postProcessPipeline;
            VkPipelineLayout        debugPipelineLayout;
            VkPipeline              debugPipeline;
            VkPipelineLayout        im3dPipelineLayout;
            VkPipeline              im3dPipeline;
            VkPipelineLayout        im3dPointsPipelineLayout;
            VkPipeline              im3dPointsPipeline;
            VkPipelineLayout        im3dLinesPipelineLayout;
            VkPipeline              im3dLinesPipeline;
            VkPipelineLayout        gaussianFilterRGB3x3PipelineLayout;
            VkPipeline              gaussianFilterRGB3x3Pipeline;
            // Did I mention the descriptors?
            VkDescriptorSetLayout   rtDescriptorSetLayout;
            VkDescriptorSet         rtDescriptorSet;
            VkDescriptorSetLayout   composeDescriptorSetLayout;
            VkDescriptorSet         composeDescriptorSet;
            VkDescriptorSetLayout   tonemappingDescriptorSetLayout;
            VkDescriptorSet         tonemappingDescriptorSet;
            VkDescriptorSetLayout   postProcessDescriptorSetLayout;
            VkDescriptorSet         postProcessDescriptorSet;
            VkDescriptorSetLayout   debugDescriptorSetLayout;
            VkDescriptorSet         debugDescriptorSet;
            VkDescriptorSetLayout   im3dDescriptorSetLayout;
            VkDescriptorSet         im3dDescriptorSet;
            VkDescriptorSetLayout   gaussianFilterRGB3x3DescriptorSetLayout;
#endif

            const std::vector<const char *> validationLayers = {"VK_LAYER_KHRONOS_validation"};
            const std::vector<const char *> deviceExtensions = {
                VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                // The things that allow for raytracing
                VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
                VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
                VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
                VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
                VK_KHR_RAY_QUERY_EXTENSION_NAME,
                VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME
            };

        public:
#ifdef RT64_DEBUG
            const bool enableValidationLayers = false;
#else
            const bool enableValidationLayers = true;
#endif

            Device(RT64_WINDOW* window);
		    virtual ~Device();

#ifndef RT64_MINIMAL

            /********************** Getters **********************/
            RT64_WINDOW* getWindow() const;
		    VkInstance& getVkInstance();
		    VkDevice& getVkDevice();
		    VkPhysicalDevice& getPhysicalDevice();
		    nvvk::ResourceAllocator& getRTAllocator();
		    VmaAllocator& getMemAllocator();
		    VkExtent2D& getSwapchainExtent();
            VkRenderPass& getPresentRenderPass();
            VkRenderPass& getOffscreenRenderPass();
		    VkViewport& getViewport();
		    VkRect2D& getScissors();
		    int getWidth();
		    int getHeight();
		    double getAspectRatio();
            int getCurrentFrameIndex();
		    VkCommandBuffer& getCurrentCommandBuffer();
            VkDescriptorPool& getDescriptorPool();
		    VkFramebuffer& getCurrentSwapchainFramebuffer();
            IDxcCompiler* getDxcCompiler();
            IDxcLibrary* getDxcLibrary();
            VkPhysicalDeviceRayTracingPipelinePropertiesKHR getRTProperties() const;
            VkPipeline& getRTPipeline();
            VkPipelineLayout& getRTPipelineLayout();
            VkDescriptorSet& getRTDescriptorSet();
            VkDescriptorSetLayout& getRTDescriptorSetLayout();
            std::vector<VkDescriptorSet>& getRTDescriptorSets();
            Texture* getBlueNoise() const;
            uint32_t getHitShaderCount() const;
            uint32_t getRasterShaderCount() const;
            VkFence& getCurrentFence();
            Inspector& getInspector();
            Mipmaps* getMipmaps();
            float getAnisotropyLevel();
            void setAnisotropyLevel(float level);
            VkPhysicalDeviceProperties getPhysicalDeviceProperties();
            // Samplers
            VkSampler& getGaussianSampler();
            VkSampler& getComposeSampler();
            VkSampler& getTonemappingSampler();
            VkSampler& getPostProcessSampler();
            // Shader getters
            VkPipelineShaderStageCreateInfo getPrimaryShaderStage() const;
            VkPipelineShaderStageCreateInfo getDirectShaderStage() const;
            VkPipelineShaderStageCreateInfo getIndirectShaderStage() const;
            VkPipelineShaderStageCreateInfo getReflectionShaderStage() const;
            VkPipelineShaderStageCreateInfo getRefractionShaderStage() const;
            // Rasterization pipelines and descriptor sets
            VkPipeline&                 getComposePipeline();
            VkPipelineLayout&           getComposePipelineLayout();
            VkDescriptorSet&            getComposeDescriptorSet();
            VkPipeline&                 getTonemappingPipeline();
            VkPipelineLayout&           getTonemappingPipelineLayout();
            VkDescriptorSet&            getTonemappingDescriptorSet();
            VkPipeline&                 getPostProcessPipeline();
            VkPipelineLayout&           getPostProcessPipelineLayout();
            VkDescriptorSet&            getPostProcessDescriptorSet();
            VkPipeline&                 getDebugPipeline();
            VkPipelineLayout&           getDebugPipelineLayout();
            VkDescriptorSet&            getDebugDescriptorSet();
            VkPipeline&                 getIm3dPipeline();
            VkPipelineLayout&           getIm3dPipelineLayout();
            VkPipeline&                 getIm3dPointsPipeline();
            VkPipelineLayout&           getIm3dPointsPipelineLayout();
            VkPipeline&                 getIm3dLinesPipeline();
            VkPipelineLayout&           getIm3dLinesPipelineLayout();
            VkDescriptorSet&            getIm3dDescriptorSet();
            VkPipeline&                 getGaussianFilterRGB3x3Pipeline();
            VkPipelineLayout&           getGaussianFilterRGB3x3PipelineLayout();
            VkDescriptorSetLayout&      getGaussianFilterRGB3x3DescriptorSetLayout();

            VkCommandBuffer* beginSingleTimeCommands();
            VkCommandBuffer* beginSingleTimeCommands(VkCommandBuffer* commandBuffer);
            void endSingleTimeCommands(VkCommandBuffer* commandBuffer);

		    void createRenderPass(VkRenderPass& renderPass, bool useDepth, VkFormat imageFormat, VkImageLayout finalLayout);
            VkResult allocateBuffer(VkDeviceSize bufferSize, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memUsage, VmaAllocationCreateFlags allocProperties, AllocatedBuffer* alre);
            VkResult allocateImage(AllocatedImage* alre, VkImageCreateInfo createInfo, VmaMemoryUsage memUsage, VmaAllocationCreateFlags allocProperties);
            VkResult allocateImage(uint32_t width, uint32_t height, VkImageType imageType, VkFormat imageFormat, VkImageTiling imageTiling, VkImageLayout initLayout, VkImageUsageFlags imageUsage, VmaMemoryUsage memUsage, VmaAllocationCreateFlags allocProperties, AllocatedImage* alre);
            void copyBuffer(VkBuffer src, VkBuffer dest, VkDeviceSize size, VkCommandBuffer* commandBuffer);
            void copyImage(AllocatedImage& src, AllocatedImage& dest, VkExtent3D dimensions, VkImageAspectFlags srcFlags, VkImageAspectFlags destFlags, VkCommandBuffer* commandBuffer);
            void matchLayoutToAccessMask(VkImageLayout inLayout, VkAccessFlags& outMask);
            void memoryBarrier(VkAccessFlags oldMask, VkAccessFlags newMask, VkPipelineStageFlags oldStage, VkPipelineStageFlags newStage, VkCommandBuffer* commandBuffer);
            void bufferMemoryBarrier(AllocatedBuffer& buffer, VkAccessFlags newMask, VkPipelineStageFlags newStage, VkCommandBuffer* commandBuffer);
            void transitionImageLayout(AllocatedImage& image, VkImageLayout newLayout, VkAccessFlags newMask, VkPipelineStageFlags newStage, VkCommandBuffer* commandBuffer);
            void transitionImageLayout(AllocatedImage** images, uint32_t imageCount, VkImageLayout newLayout, VkAccessFlags newMask, VkPipelineStageFlags oldStage, VkPipelineStageFlags newStage, VkCommandBuffer* commandBuffer);
            void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, VkCommandBuffer* commandBuffer);
            VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
            VkBufferView createBufferView(VkBuffer& buffer, VkFormat format, VkBufferViewCreateFlags flags, VkDeviceSize size);
            void draw(int vsyncInterval, double delta);
            void setInspectorVisibility(bool v);
		    void addScene(Scene* scene);
		    void removeScene(Scene* scene);
		    void addMesh(Mesh* mesh);
		    void removeMesh(Mesh* mesh);
		    void addTexture(Texture* texture);
		    void removeTexture(Texture* texture);
            void addShader(Shader* shader);
            void removeShader(Shader* shader);
            std::unordered_map<unsigned int, VkSampler>& getSamplerMap();
            VkSampler& getSampler(unsigned int index);
            ImGui_ImplVulkan_InitInfo generateImguiInitInfo();
            void addDepthImageView(VkImageView* depthImageView);
            void dirtyDescriptorPool();
            void removeDepthImageView(VkImageView* depthImageView);
            void createShaderModule(const void* code, size_t size, const char* entryName, VkShaderStageFlagBits stage, VkPipelineShaderStageCreateInfo& shaderStageInfo, VkShaderModule& shader, std::vector<VkPipelineShaderStageCreateInfo>* shaderStages);
            void initRTBuilder(nvvk::RaytracingBuilderKHR& rtBuilder);
            void generateDescriptorSetLayout(std::vector<VkDescriptorSetLayoutBinding>& bindings, VkDescriptorBindingFlags& flags, VkDescriptorSetLayout& descriptorSetLayout);
            void addToDescriptorPool(std::vector<VkDescriptorSetLayoutBinding>& bindings);
            void allocateDescriptorSet(VkDescriptorSetLayout& descriptorSetLayout, VkDescriptorSet& descriptorSet);
            uint32_t getFirstAvailableHitDescriptorSetIndex() const;
            uint32_t getFirstAvailableHitShaderID() const;
            uint32_t getFirstAvailableRasterShaderID() const;
            void createFramebuffer(VkFramebuffer& framebuffer, VkRenderPass& renderPass, VkImageView& imageView, VkImageView* depthView, VkExtent2D extent);
            void waitForGPU();

            // More stuff for window resizing
            bool wasWindowResized() { return framebufferResized; }
            void resetWindowResized() { framebufferResized = false; }
#endif
    };
}