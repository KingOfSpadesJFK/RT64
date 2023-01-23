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

#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>
#include <vector>
#include <optional>
#include <vulkan/vulkan.h>
#include <array>
#include <nvvk/context_vk.hpp>
#include <nvvk/raytraceKHR_vk.hpp>
#include <nvvk/resourceallocator_vk.hpp>

#ifdef _WIN32
    #define VK_USE_PLATFORM_WIN32_KHR
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3native.h>
#endif

#define MAX_FRAMES_IN_FLIGHT    2

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
            VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
            VkSwapchainKHR swapChain;
            std::vector<VkImage> swapChainImages;
            VkFormat swapChainImageFormat;
            VkExtent2D swapChainExtent;
            std::vector<VkImageView> swapChainImageViews;

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

            static void framebufferResizeCallback(GLFWwindow* glfwWindow, int width, int height);

#ifndef RT64_MINIMAL
            void createDxcCompiler();
            void createMemoryAllocator();
            void createSurface();
            void cleanupSwapChain();
            void createSwapChain();
            void createImageViews();
		    void createRenderPass();
            void createCommandPool();
            void createCommandBuffers();
            void createSyncObjects();
            void createRayTracingPipeline();

            void initRayTracing();
            void recordCommandBuffer(VkCommandBuffer& commandBuffer, uint32_t imageIndex);
            void recreateSwapChain();
            bool updateSize(VkResult result, const char* error);
            void updateViewport();
            void updateScenes();
            void resizeScenes();

            GLFWwindow* window;
            VkSurfaceKHR vkSurface;
            int width;
            int height;
            float aspectRatio;
            bool framebufferCreated = false;
            bool framebufferResized = false;

            std::vector<Scene*> scenes;
            std::vector<Shader*> shaders;
            std::vector<Mesh*> meshes;
            std::vector<Texture*> textures;
            std::vector<Inspector*> inspectors;
            
		    Mipmaps *mipmaps;
            VmaAllocator allocator;

            VkRenderPass renderPass;
            std::vector<VkFramebuffer> swapChainFramebuffers;

            VkCommandPool commandPool;
            std::vector<VkCommandBuffer> commandBuffers;

            std::vector<VkSemaphore> imageAvailableSemaphores;
            std::vector<VkSemaphore> renderFinishedSemaphores;
            std::vector<VkFence> inFlightFences;
            std::vector<VkImageView*> depthViews;

            bool rtStateDirty = false;
            nvvk::RaytracingBuilderKHR rtBlasBuilder;
            nvvk::ResourceAllocatorDma rtAllocator;
            std::vector<VkRayTracingShaderGroupCreateInfoKHR> rtShaderGroups;
            VkPipelineLayout rtPipelineLayout;
            VkPipeline rtPipeline;

            uint32_t currentFrame = 0;
            uint32_t framebufferIndex = 0;

            VkViewport vkViewport;
            VkRect2D vkScissorRect;

            IDxcCompiler* d3dDxcCompiler;   // Who invited my man blud XDXDXD
            IDxcLibrary* d3dDxcLibrary;     // Bro thinks he's on the team  XDXDXDXDXDXD

            //***********************************************************
            // The Ray Tracing Shader Modules
            VkShaderModule primaryRayGenModule;
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
#ifdef NDEBUG
            const bool enableValidationLayers = false;
#else
            const bool enableValidationLayers = true;
#endif

            Device(GLFWwindow* window);
		    virtual ~Device();

#ifndef RT64_MINIMAL

            /********************** Getters **********************/
		    VkDevice& getVkDevice();
		    VkPhysicalDevice& getPhysicalDevice();
		    nvvk::ResourceAllocator& getRTAllocator();
		    VmaAllocator& getMemAllocator();
		    VkExtent2D& getSwapchainExtent();
            VkRenderPass& getRenderPass();
		    VkViewport& getViewport();
		    VkRect2D& getScissors();
		    int getWidth();
		    int getHeight();
		    double getAspectRatio();
            int getCurrentFrameIndex();
		    VkCommandBuffer& getCurrentCommandBuffer();
		    VkFramebuffer& getCurrentSwapchainFramebuffer();
            IDxcCompiler* getDxcCompiler();
            IDxcLibrary* getDxcLibrary();

            VkCommandBuffer* beginSingleTimeCommands();
            VkCommandBuffer* beginSingleTimeCommands(VkCommandBuffer* commandBuffer);
            void endSingleTimeCommands(VkCommandBuffer* commandBuffer);

            void createFramebuffers();

            VkResult allocateBuffer(VkDeviceSize bufferSize, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memUsage, VmaAllocationCreateFlags allocProperties, AllocatedBuffer* alre);
            VkResult allocateImage(uint32_t width, uint32_t height, VkImageType imageType, VkFormat imageFormat, VkImageTiling imageTiling, VkImageLayout initLayout, VkImageUsageFlags imageUsage, VmaMemoryUsage memUsage, VmaAllocationCreateFlags allocProperties, AllocatedImage* alre);
            void copyBuffer(VkBuffer src, VkBuffer dest, VkDeviceSize size, VkCommandBuffer* commandBuffer);
            void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, VkCommandBuffer* commandBuffer);
            void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, VkCommandBuffer* commandBuffer);
            VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
            VkBufferView createBufferView(VkBuffer& buffer, VkFormat format, VkBufferViewCreateFlags flags, VkDeviceSize size);
            void draw(int vsyncInterval, double delta);
		    void addScene(Scene* scene);
		    void removeScene(Scene* scene);
		    void addMesh(Mesh* mesh);
		    void removeMesh(Mesh* mesh);
		    void addTexture(Texture* texture);
		    void removeTexture(Texture* texture);
            void addShader(Shader* shader);
            void removeShader(Shader* shader);
            void addDepthImageView(VkImageView* depthImageView);
            void removeDepthImageView(VkImageView* depthImageView);
            void createShaderModule(const void* code, size_t size, const char* entryName, VkShaderStageFlagBits stage, VkPipelineShaderStageCreateInfo& shaderStageInfo, VkShaderModule& shader, std::vector<VkPipelineShaderStageCreateInfo>* shaderStages);
            void initRTBuilder(nvvk::RaytracingBuilderKHR& rtBuilder);

            // More stuff for window resizing
            bool wasWindowResized() { return framebufferResized; }
            void resetWindowResized() { framebufferResized = false; }
#endif
    };
}