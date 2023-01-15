/*
*   RT64VK
*/

#pragma once

#include "rt64_common.h"

#include "rt64_scene.h"

#include "../contrib/nvpro_core/nvvk/raytraceKHR_vk.hpp"
#include "../contrib/nvpro_core/nvvk/memallocator_dma_vk.hpp"
#include "../contrib/nvpro_core/nvvk/resourceallocator_vk.hpp"

#include <GLFW/glfw3.h>
#include <vector>
#include <optional>

#ifdef _WIN32
    #define VK_USE_PLATFORM_WIN32_KHR
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3native.h>
#endif

namespace nvvk
{
    class ResourceAllocator;
    class RaytracingBuilderKHR;
    class ResourceAllocatorDma;
    class CommandPool;
}

namespace RT64
{
	class Scene;
	class Shader;
	class Inspector;
	class Texture;
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

    enum ShaderStage {
        VertexStage,
        FragmentStage,
        ComputeStage,
        GeometryStage,
        RaytraceStage
    };
    
    // For the test shader
    struct TestVertex {
        glm::vec4 pos;
        glm::vec3 color;
        static VkVertexInputBindingDescription getBindingDescription() {
            VkVertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(TestVertex);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return bindingDescription;
        }
        static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions() {
            std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
            attributeDescriptions[0].binding = 0;   // You know that (location = 0) thing in the glsl shaders? Yeah that's what
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[0].offset = offsetof(TestVertex, pos);
            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(TestVertex, color);
            return attributeDescriptions;
        }
    };
    
    class Device
    {
        private:
            const int MAX_FRAMES_IN_FLIGHT = 2;
            VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
            VkInstance vkInstance;
            VkDevice vkDevice;
            VkDebugUtilsMessengerEXT debugMessenger;
            VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
            VkQueue graphicsQueue;
            VkQueue presentQueue;
            VkSwapchainKHR swapChain;
            std::vector<VkImage> swapChainImages;
            VkFormat swapChainImageFormat;
            VkExtent2D swapChainExtent;
            std::vector<VkImageView> swapChainImageViews;

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
            void createMemoryAllocator();
            void createSurface();
            void cleanupSwapChain();
            void createSwapChain();
            void createImageViews();
		    void createRenderPass();
		    void createGraphicsPipeline();
            void createFramebuffers();
            void createCommandPool();
            void createCommandBuffers();
            void createSyncObjects();

            void initRayTracing();
            void recordCommandBuffer(VkCommandBuffer& commandBuffer, uint32_t imageIndex);
            void recreateSwapChain();
            void updateSize(VkResult result, const char* error);
            void updateViewport();

            std::vector<TestVertex> vertices = {
                {{-0.5f, -0.5f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},
                {{0.5f, -0.5f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
                {{0.5f, 0.5f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
                {{-0.5f, 0.5f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}}
            };
            std::vector<uint32_t> indices = {
                0, 1, 2, 2, 3, 0
            };

            GLFWwindow* window;
            VkSurfaceKHR vkSurface;
            int width;
            int height;
            float aspectRatio;
            bool framebufferResized = false;
            std::vector<Scene*> scenes;
            std::vector<Shader*> shaders;
            std::vector<Inspector*> inspectors;
		    Mipmaps *mipmaps;
            nvvk::RaytracingBuilderKHR rtBuilder;
            nvvk::ResourceAllocatorDma memAlloc;
            VmaAllocator allocator;
            VkPipelineLayout pipelineLayout;
            VkRenderPass renderPass;
            VkPipeline graphicsPipeline;
            std::vector<VkFramebuffer> swapChainFramebuffers;
            VkCommandPool commandPool;
            std::vector<VkCommandBuffer> commandBuffers;
            std::vector<VkSemaphore> imageAvailableSemaphores;
            std::vector<VkSemaphore> renderFinishedSemaphores;
            std::vector<VkFence> inFlightFences;
            uint32_t currentFrame = 0;
            uint32_t framebufferIndex = 0;
            AllocatedResource vertexBuffer;
            AllocatedResource indexBuffer;
            // VkDeviceMemory vertexBufferMemory;

            VkViewport vkViewport;
            VkRect2D vkScissorRect;
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
		    VkDevice& getVkDevice();
		    VkPhysicalDevice& getPhysicalDevice();
		    nvvk::RaytracingBuilderKHR& getRTBuilder();
		    VmaAllocator& getMemAllocator();
		    VkCommandBuffer& getCurrentCommandBuffer();
		    VkFramebuffer& getCurrentSwapchainFramebuffer();
		    VkExtent2D& getSwapchainExtent();
            VkPipeline& getGraphicsPipeline();
            VkRenderPass& getRenderPass();
		    VkViewport& getViewport();
		    VkRect2D& getScissors();

            VkResult allocateBuffer(VkDeviceSize bufferSize, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memUsage, VmaAllocationCreateFlags allocProperties, AllocatedResource* alre);
            void copyBuffer(VkBuffer src, VkBuffer dest, VkDeviceSize size);
            void draw(int vsyncInterval, double delta);
		    void addScene(Scene* scene);
		    void removeScene(Scene* scene);
            VkShaderModule createShaderModule(const void* code, size_t size, ShaderStage stage, VkPipelineShaderStageCreateInfo& shaderStageInfo, std::vector<VkPipelineShaderStageCreateInfo>* shaderStages);

            // More stuff for window resizing
            bool wasWindowResized() { return framebufferResized; }
            void resetWindowResized() { framebufferResized = false; }
#endif
    };
}