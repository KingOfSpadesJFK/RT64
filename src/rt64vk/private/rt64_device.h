/*
*   RT64VK
*/

#pragma once

#include "rt64_common.h"

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <optional>

#ifdef _WIN32
    #define VK_USE_PLATFORM_WIN32_KHR
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3native.h>
#endif

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
            void initRayTracing();
            void createSwapChain();
            void createImageViews();

		    void loadAssets();

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
            VkShaderModule createShaderModule(const unsigned char* arr, size_t size);

            // For window resizing
            bool framebufferResized;
            static void framebufferResizeCallback(GLFWwindow* glfwWindow, int width, int height);

#ifndef RT64_MINIMAL
            void createSurface();
		    static const uint32_t FrameCount = 2;

            GLFWwindow* window;
            VkSurfaceKHR vkSurface;
            int width;
            int height;
            float aspectRatio;
            std::vector<Scene *> scenes;
            std::vector<Shader *> shaders;
            std::vector<Inspector *> inspectors;
		    Mipmaps *mipmaps;
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

            // More stuff for window resizing
            bool wasWindowResized() { return framebufferResized; }
            void resetWindowResized() { framebufferResized = false; }
    };
}