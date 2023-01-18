/*
*   RT64VK
*/

#pragma once

#include "rt64_common.h"

#include "rt64_scene.h"
#include "rt64_shader.h"

#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>
#include <vector>
#include <optional>
#include <vulkan/vulkan.h>
#include <array>

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

    struct DescriptorSetBinding {
        AllocatedResource* resource;
        VkImageView* imageView;
        VkSampler* sampler;
        VkDeviceSize size;
        VkDescriptorType type;
        VkShaderStageFlagBits stage;
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
        glm::vec3 normal;
        glm::vec2 uv;
        static VkVertexInputBindingDescription getBindingDescription() {
            VkVertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(TestVertex);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return bindingDescription;
        }
        static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() {
            std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
            attributeDescriptions[0].binding = 0;   // You know that (location = 0) thing in the glsl shaders? Yeah that's what
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attributeDescriptions[0].offset = offsetof(TestVertex, pos);
            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(TestVertex, normal);
            attributeDescriptions[2].binding = 0;
            attributeDescriptions[2].location = 2;
            attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[2].offset = offsetof(TestVertex, uv);
            return attributeDescriptions;
        }
    };
    
    class Device
    {
        private:
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
            void createDxcCompiler();
            void createMemoryAllocator();
            void createSurface();
            void cleanupSwapChain();
            void createSwapChain();
            void createImageViews();
		    void createRenderPass();
            void createFramebuffers();
            void createCommandPool();
            void createCommandBuffers();
            void createSyncObjects();

            void createDescriptorSetLayout();
		    void createGraphicsPipeline();

            void initRayTracing();
            void recordCommandBuffer(VkCommandBuffer& commandBuffer, uint32_t imageIndex);
            void recreateSwapChain();
            bool updateSize(VkResult result, const char* error);
            void updateViewport();

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
            VmaAllocator allocator;
            VkDescriptorSetLayout descriptorSetLayout;
            VkPipelineLayout rasterPipelineLayout;
            VkRenderPass renderPass;
            VkPipeline rasterPipeline;
            std::vector<VkFramebuffer> swapChainFramebuffers;
            VkCommandPool commandPool;
            std::vector<VkCommandBuffer> commandBuffers;
            std::vector<VkSemaphore> imageAvailableSemaphores;
            std::vector<VkSemaphore> renderFinishedSemaphores;
            std::vector<VkFence> inFlightFences;
            uint32_t currentFrame = 0;
            uint32_t framebufferIndex = 0;

            VkViewport vkViewport;
            VkRect2D vkScissorRect;

            IDxcCompiler* d3dDxcCompiler;   // Who invited my man blud XDXDXD
            IDxcLibrary* d3dDxcLibrary;     // Bro thinks he's on the team  XDXDXDXDXDXD
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
		    // nvvk::RaytracingBuilderKHR& getRTBuilder();
		    VmaAllocator& getMemAllocator();
		    VkExtent2D& getSwapchainExtent();
            VkDescriptorSetLayout& getDescriptorSetLayout();
            VkPipelineLayout& getRasterPipelineLayout();
            VkPipeline& getRasterPipeline();
            VkRenderPass& getRenderPass();
		    VkViewport& getViewport();
		    VkRect2D& getScissors();
		    double getAspectRatio();
            int getCurrentFrameIndex();
		    VkCommandBuffer& getCurrentCommandBuffer();
		    VkFramebuffer& getCurrentSwapchainFramebuffer();
            IDxcCompiler* getDxcCompiler();
            IDxcLibrary* getDxcLibrary();

            VkCommandBuffer* beginSingleTimeCommands();
            VkCommandBuffer* beginSingleTimeCommands(VkCommandBuffer* commandBuffer);
            void endSingleTimeCommands(VkCommandBuffer* commandBuffer);

            VkResult allocateBuffer(VkDeviceSize bufferSize, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memUsage, VmaAllocationCreateFlags allocProperties, AllocatedBuffer* alre);
            VkResult allocateImage(uint32_t width, uint32_t height, VkImageType imageType, VkFormat imageFormat, VkImageTiling imageTiling, VkImageLayout initLayout, VkImageUsageFlags imageUsage, VmaMemoryUsage memUsage, VmaAllocationCreateFlags allocProperties, AllocatedImage* alre);
            void copyBuffer(VkBuffer src, VkBuffer dest, VkDeviceSize size, VkCommandBuffer* commandBuffer);
            void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, VkCommandBuffer* commandBuffer);
            void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, VkCommandBuffer* commandBuffer);
            VkImageView createImageView(VkImage image, VkFormat format);
            void draw(int vsyncInterval, double delta);
		    void addScene(Scene* scene);
		    void removeScene(Scene* scene);
            void addShader(Shader* shader);
            void removeShader(Shader* shader);
            VkShaderModule createShaderModule(const void* code, size_t size, ShaderStage stage, VkPipelineShaderStageCreateInfo& shaderStageInfo, std::vector<VkPipelineShaderStageCreateInfo>* shaderStages);
            void createRasterPipeline(DescriptorSetBinding* bindings, uint32_t count);

            // More stuff for window resizing
            bool wasWindowResized() { return framebufferResized; }
            void resetWindowResized() { framebufferResized = false; }
#endif
    };
}