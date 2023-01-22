/*
*   RT64VK
*/

#include <cassert>

#include "rt64_device.h"
#include <cstring>
#include <iostream>
#include <set>
#include <unordered_set>
#include <map>
#include <cstdint>
#include <limits>
#include <algorithm>

#include <stb_image.h>

// ##############################################
// SPIR-V blobs

// Test shaders
//static unsigned int helloTriangleVS[] = 
#include "shaders/HelloTriangleVS.hlsl.h"
#include "shaders/HelloTrianglePS.hlsl.h"

// Pixel shaders
#include "shaders/ComposePS.hlsl.h"
// Vertex shaders
#include "shaders/FullScreenVS.hlsl.h"

namespace RT64
{

    Device::Device(GLFWwindow* glfwWindow) {
	    RT64_LOG_OPEN("rt64.log");

#ifndef RT64_MINIMAL
        window = glfwWindow;
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
#endif

        createVkInstanceNV();
        // createVKInstance();
        // setupDebugMessenger();
#ifndef RT64_MINIMAL
        createSurface();
#endif
        // pickPhysicalDevice();
        // createLogicalDevice();

#ifndef RT64_MINIMAL
        createMemoryAllocator();
        createSwapChain();
        updateViewport();
        createImageViews();
        createRenderPass();
        createCommandPool();

        createCommandBuffers();
        createSyncObjects();

        createDxcCompiler();
        
        initRayTracing();
#endif
    }

    void Device::createVkInstanceNV() {
        // Vulkan required extensions
        assert(glfwVulkanSupported() == 1);
        uint32_t count{0};
        auto     reqExtensions = glfwGetRequiredInstanceExtensions(&count);

        // Requesting Vulkan extensions and layers
        nvvk::ContextCreateInfo contextInfo;
        contextInfo.setVersion(1, 2);                       // Using Vulkan 1.2
        for(uint32_t ext_id = 0; ext_id < count; ext_id++)  // Adding required extensions (surface, win32, linux, ..)
            contextInfo.addInstanceExtension(reqExtensions[ext_id]);
        contextInfo.addInstanceLayer("VK_LAYER_LUNARG_monitor", true);              // FPS in titlebar
        contextInfo.addInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, true);  // Allow debug names
        contextInfo.addDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);            // Enabling ability to present rendering

        // #VKRay: Activate the ray tracing extension
        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
        contextInfo.addDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, false, &accelFeature);  // To build acceleration structures
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
        contextInfo.addDeviceExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, false, &rtPipelineFeature);  // To use vkCmdTraceRaysKHR
        contextInfo.addDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);  // Required by ray tracing pipeline

        // Creating Vulkan base application
        vkctx.initInstance(contextInfo);
        // Find all compatible devices
        auto compatibleDevices = vkctx.getCompatibleDevices(contextInfo);
        assert(!compatibleDevices.empty());
        // Use a compatible device
        vkctx.initDevice(compatibleDevices[0], contextInfo);
    }

    /*
        More notes about how vulkan works just for me...
            Semaphores act like signals. They signal commands to execute
                - Command A and B are commands and there's a semaphore.
                - Command A starts.
                - While command A is being worked on, command B is waiting.
                - Once command A finishes, command A tells the semaphore that
                   its done
                - After the semaphore signals its signal, command B does its
                   thing
            Fences are like that but for the CPU
                - While a command is working, when vkWaitForFence() is called,
                   the CPU can't really do anything until the command is finished
                - When the command is finished, it signals the fence.
                - The CPU can go back to lolygagging after all that
    */

    // Just pasting this here as a reminder...
    // From the official vulkan tutorial:
    /*
        At a high level, rendering a frame in Vulkan consists of a common set of steps:
            - Wait for the previous frame to finish
            - Acquire an image from the swap chain
            - Record a command buffer which draws the scene onto that image
            - Submit the recorded command buffer
            - Present the swap chain image
    */
#ifndef RT64_MINIMAL
    void Device::draw(int vsyncInterval, double delta) {
        if (!framebufferCreated) {
            createFramebuffers();
            framebufferCreated = true;
        }
        vkWaitForFences(vkDevice, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

        VkResult result = vkAcquireNextImageKHR(vkDevice, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &framebufferIndex);

        // Handle resizing
        if (updateSize(result, "failed to acquire swap chain image!")) {
            // don't draw the image if resized
            return;
        }

        // Only reset the fence if we are submitting work
        vkResetFences(vkDevice, 1, &inFlightFences[currentFrame]);

        vkResetCommandBuffer(commandBuffers[currentFrame], 0);

        // Update the scenes....
        updateScenes();

        // Draw the scenes!
        for (Scene* s : scenes) {
            s->render(delta);
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

        VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;
        VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]));

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        VkSwapchainKHR swapChains[] = {swapChain};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &framebufferIndex;
        presentInfo.pResults = nullptr; // Optional
        // Now pop it on the screen!
        result = vkQueuePresentKHR(presentQueue, &presentInfo); 

        // Handle resizing again
        updateSize(result, "failed to present swap chain image!");
        
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void Device::updateViewport() {
        vkViewport.x = 0.0f;
        vkViewport.y = 0.0f;
        vkViewport.width = static_cast<float>(swapChainExtent.width);
        vkViewport.height = static_cast<float>(swapChainExtent.height);
        vkViewport.minDepth = 0.0f;
        vkViewport.maxDepth = 1.0f;
        vkScissorRect.offset = {0, 0};
        vkScissorRect.extent = swapChainExtent;
    }

    // Recreates the swapchain and what not if the resolution gets resized
    // Returns true if the size got updated, false if not
    //  Also returns a secret third thing if something goes wrong (a runtime error)
    bool Device::updateSize(VkResult result, const char* error) {
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
            recreateSwapChain();
            updateViewport();
            framebufferResized = false;
            return true;
        } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error(error);
        }
        return false;
    }

    // The things needed for syncing the CPU and GPU lmao
    void Device::createSyncObjects() {
        imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            VK_CHECK(vkCreateSemaphore(vkDevice, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]));
            VK_CHECK(vkCreateSemaphore(vkDevice, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]));
            VK_CHECK(vkCreateFence(vkDevice, &fenceInfo, nullptr, &inFlightFences[i]));
        }
    }
    
    // I used the DX12 to destroy the DX12
    void Device::createDxcCompiler() {
        RT64_LOG_PRINTF("Compiler creation started");
        D3D12_CHECK(DxcCreateInstance(CLSID_DxcCompiler, __uuidof(IDxcCompiler), (void **)&d3dDxcCompiler));
        D3D12_CHECK(DxcCreateInstance(CLSID_DxcLibrary, __uuidof(IDxcLibrary), (void **)&d3dDxcLibrary));
        RT64_LOG_PRINTF("Compiler creation finished");
    }

    void Device::createCommandBuffers() {
        commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        // Primary command buffers are called in a queue, but secondary command
        //  buffers cannot call primary command buffers
        // Secondary command buffers can't be submitted directly into a queue, but 
        //  primary commmand buffers can call them
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();
        VK_CHECK(vkAllocateCommandBuffers(vkDevice, &allocInfo, commandBuffers.data()));
    }

    void Device::createCommandPool() {
	    RT64_LOG_PRINTF("Command pool creation started");
        QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
        
        VK_CHECK(vkCreateCommandPool(vkDevice, &poolInfo, nullptr, &commandPool));
    }

    void Device::createFramebuffers() {
	    RT64_LOG_PRINTF("Framebuffer creation started");
        swapChainFramebuffers.resize(swapChainImageViews.size());

        for (size_t i = 0; i < swapChainImageViews.size(); i++) {
            std::vector<VkImageView> attachments = {
                swapChainImageViews[i]
            };
            for (int j = 0; j < depthViews.size(); j++) {  
                attachments.push_back(*depthViews[j]);
            }

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = attachments.size();
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = swapChainExtent.width;
            framebufferInfo.height = swapChainExtent.height;
            framebufferInfo.layers = 1;

            VK_CHECK(vkCreateFramebuffer(vkDevice, &framebufferInfo, nullptr, &swapChainFramebuffers[i]));
        }

    }

    void Device::createRenderPass() {
	    RT64_LOG_PRINTF("Render pass creation started");
        // Describe the render pass
        // TODO: Add the rest of the color/depth buffers later on
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapChainImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;        // Not gonna worry about stencil buffers
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = VK_FORMAT_D32_SFLOAT;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // TODO: Do some subpass trickery later on
        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkAttachmentDescription attachments[] = {colorAttachment, depthAttachment};
        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 2;
        renderPassInfo.pAttachments = attachments;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        VK_CHECK(vkCreateRenderPass(vkDevice, &renderPassInfo, nullptr, &renderPass));
    }

    #define PS_ENTRY "PSMain"
    #define VS_ENTRY "VSMain"
    void Device::createShaderModule(const void* code, size_t size, const char* entryName, VkShaderStageFlagBits stage, VkPipelineShaderStageCreateInfo& shaderStageInfo, VkShaderModule& shader, std::vector<VkPipelineShaderStageCreateInfo>* shaderStages) {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = size;
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code);

        VK_CHECK(vkCreateShaderModule(vkDevice, &createInfo, nullptr, &shader));
        // Create the shader stage info
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.module = shader;
        shaderStageInfo.pName = entryName;
        shaderStageInfo.stage = stage;

        if (shaderStages) {
            shaderStages->push_back(shaderStageInfo);
        }
    }

    void Device::createSwapChain() {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);

        VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
        VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
        VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);
        
        uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
        if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
            imageCount = swapChainSupport.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = vkSurface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
        uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

        if (indices.graphicsFamily != indices.presentFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0; // Optional
            createInfo.pQueueFamilyIndices = nullptr; // Optional
        }
        createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        VK_CHECK(vkCreateSwapchainKHR(vkDevice, &createInfo, nullptr, &swapChain));

        vkGetSwapchainImagesKHR(vkDevice, swapChain, &imageCount, nullptr);
        swapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(vkDevice, swapChain, &imageCount, swapChainImages.data());
        swapChainImageFormat = surfaceFormat.format;
        swapChainExtent = extent;
    }

    void Device::cleanupSwapChain() {
        for (auto imageView : swapChainImageViews) {
            vkDestroyImageView(vkDevice, imageView, nullptr);
        }
        for (auto framebuffer : swapChainFramebuffers) {
            vkDestroyFramebuffer(vkDevice, framebuffer, nullptr);
        }
        vkDestroySwapchainKHR(vkDevice, swapChain, nullptr);
    }

    // Y'know, if u ever wanna like resize ur window
    void Device::recreateSwapChain() {
        int w, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        while (w == 0 || h == 0) {
            glfwGetFramebufferSize(window, &w, &h);
            glfwWaitEvents();
        }

        vkDeviceWaitIdle(vkDevice);

        cleanupSwapChain();

        createSwapChain();
        createImageViews();
        resizeScenes();
        createFramebuffers();
    }

    void Device::updateScenes() {
        for (Scene* s : scenes) {
            s->update();
        }
    }

    void Device::resizeScenes() {
        for (Scene* s : scenes) {
            s->resize();
        }
    }
#endif

    void Device::createMemoryAllocator() {
        RT64_LOG_PRINTF("Creating memory allocator");
        VmaVulkanFunctions vulkanFunctions = {};
        vulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
        vulkanFunctions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;
        
        VmaAllocatorCreateInfo allocatorCreateInfo = {};
        allocatorCreateInfo.vulkanApiVersion = RT64_VULKAN_VERSION;
        allocatorCreateInfo.physicalDevice = physicalDevice;
        allocatorCreateInfo.device = vkDevice;
        allocatorCreateInfo.instance = vkInstance;
        allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;
        allocatorCreateInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        VK_CHECK(vmaCreateAllocator(&allocatorCreateInfo, &allocator));

        rtAllocator.init(vkInstance, vkDevice, physicalDevice);
    }

	void Device::createVKInstance() 
    {
	    RT64_LOG_PRINTF("Creating Vulkan instance");
        
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "RT64VK Application";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = RT64_VULKAN_VERSION;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        auto extensions = getInstanceExtensions();
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo;
        if (enableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();

            populateDebugMessengerCreateInfo(debugCreateInfo);
            createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debugCreateInfo;
        } else {
            createInfo.enabledLayerCount = 0;
            createInfo.pNext = nullptr;
        }

        VK_CHECK(vkCreateInstance(&createInfo, nullptr, &vkInstance));

        hasGflwRequiredInstanceExtensions();
    }

#ifndef RT64_MINIMAL
    void Device::createSurface() 
    { 

#ifdef _WIN32
        VkWin32SurfaceCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        createInfo.hwnd = glfwGetWin32Window(window);
        createInfo.hinstance = GetModuleHandle(nullptr);
#endif

        VK_CHECK(glfwCreateWindowSurface(vkInstance, window, nullptr, &vkSurface));
    }
#endif

    void Device::pickPhysicalDevice() 
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(vkInstance, &deviceCount, nullptr);
        if (deviceCount == 0) {
            throw std::runtime_error("failed to find GPUs with Vulkan support!");
        }
        
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(vkInstance, &deviceCount, devices.data());
        
        for (const auto& device : devices) {
            if (isDeviceSuitable(device)) {
                physicalDevice = device;
                break;
            }
        }

        if (physicalDevice == VK_NULL_HANDLE) {
            throw std::runtime_error("failed to find a suitable GPU!");
        }
    }

    void Device::createLogicalDevice() {
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};

        float queuePriority = 1.0f;
        for (uint32_t queueFamily : uniqueQueueFamilies) {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }
        
        VkPhysicalDeviceFeatures deviceFeatures{};
        VkPhysicalDeviceFeatures2 deviceFeatures2{};
        VkPhysicalDeviceVulkan11Features deviceFeatures11{};
        VkPhysicalDeviceVulkan12Features deviceFeatures12{};
        deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        deviceFeatures11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        deviceFeatures12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        deviceFeatures2.features = deviceFeatures;
        deviceFeatures2.pNext = &deviceFeatures12;
        deviceFeatures12.pNext = &deviceFeatures11;
        vkGetPhysicalDeviceFeatures2(physicalDevice, &deviceFeatures2);

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        // createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.pNext = &deviceFeatures2;

        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();
        if (enableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();
        } else {
            createInfo.enabledLayerCount = 0;
        }

        VK_CHECK(vkCreateDevice(physicalDevice, &createInfo, nullptr, &vkDevice));

        vkGetDeviceQueue(vkDevice, indices.graphicsFamily.value(), 0, &graphicsQueue);
        vkGetDeviceQueue(vkDevice, indices.presentFamily.value(), 0, &presentQueue);
    }

    SwapChainSupportDetails Device::querySwapChainSupport(VkPhysicalDevice device) {
        SwapChainSupportDetails details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, vkSurface, &details.capabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, vkSurface, &formatCount, nullptr);
        if (formatCount != 0) {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, vkSurface, &formatCount, details.formats.data());
        }

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, vkSurface, &presentModeCount, nullptr);
        if (presentModeCount != 0) {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, vkSurface, &presentModeCount, details.presentModes.data());
        }

        return details;
    }

    VkSurfaceFormatKHR Device::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
    {
        for (const auto& availableFormat : availableFormats) {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return availableFormat;
            }
        }
        return availableFormats[0];
    }

    VkPresentModeKHR Device::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) 
    {
        for (const auto& availablePresentMode : availablePresentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return availablePresentMode;
            }
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }
    
    VkExtent2D Device::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) 
    {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        } else {
            int width, height;
            glfwGetFramebufferSize(window, &width, &height);

            VkExtent2D actualExtent = {
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)
            };

            actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

            return actualExtent;
        }
    }

    void Device::initRayTracing()
    {
        // Requesting ray tracing properties
        VkPhysicalDeviceProperties2 prop2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        prop2.pNext = &rtProperties;
        vkGetPhysicalDeviceProperties2(physicalDevice, &prop2);
    }

    void Device::createImageViews() {
        swapChainImageViews.resize(swapChainImages.size());
        for (size_t i = 0; i < swapChainImages.size(); i++) {
            swapChainImageViews[i] = createImageView(swapChainImages[i], swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
        }
    }

    bool Device::isDeviceSuitable(VkPhysicalDevice device) {
        QueueFamilyIndices indices = findQueueFamilies(device);
        bool extensionsSupported = checkDeviceExtensionSupport(device);
        bool swapChainAdequate = false;
        if (extensionsSupported) {
            SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
            swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
        }
        return indices.isComplete() && extensionsSupported && swapChainAdequate;

    }

    bool Device::checkDeviceExtensionSupport(VkPhysicalDevice device) {
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

        std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

        for (const auto& extension : availableExtensions) {
            requiredExtensions.erase(extension.extensionName);
        }

        return requiredExtensions.empty();
    }

    // local callback functions
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
        void *pUserData) 
    {
        std::cerr << "********************************************************" << std::endl;
        std::cerr << "[VALIDATION LAYER] " << pCallbackData->pMessage << std::endl;
        std::cerr << std::endl;
        return VK_FALSE;
    }

    VkResult CreateDebugUtilsMessengerEXT(
        VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
        const VkAllocationCallbacks *pAllocator,
        VkDebugUtilsMessengerEXT *pDebugMessenger) 
    {
        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance,
            "vkCreateDebugUtilsMessengerEXT");
        if (func != nullptr) {
            return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
        } else {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }

    void Device::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo) 
    {
        createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = debugCallback;
        createInfo.pUserData = nullptr;  // Optional
    }

    void Device::setupDebugMessenger() {
        if (!enableValidationLayers) return;
        VkDebugUtilsMessengerCreateInfoEXT createInfo;
        populateDebugMessengerCreateInfo(createInfo);
        VK_CHECK(CreateDebugUtilsMessengerEXT(vkInstance, &createInfo, nullptr, &debugMessenger));
    }

    QueueFamilyIndices Device::findQueueFamilies(VkPhysicalDevice device)
    {
        QueueFamilyIndices indices;
        // Assign index to queue families that could be found
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        int i = 0;
        for (const auto& queueFamily : queueFamilies) {
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphicsFamily = i;
            }
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, vkSurface, &presentSupport);
            if (presentSupport) {
                indices.presentFamily = i;
            }
            if (indices.isComplete()) {
                break;
            }
            i++;
        }

        return indices;
    }

    std::vector<const char*> Device::getInstanceExtensions() 
    {
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions;
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

        if (enableValidationLayers) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        return extensions;
    }

    void Device::hasGflwRequiredInstanceExtensions() 
    {
        uint32_t extensionCount = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

        std::cout << "available extensions:" << std::endl;
        std::unordered_set<std::string> available;
        for (const auto &extension : extensions) {
            std::cout << "\t" << extension.extensionName << std::endl;
            available.insert(extension.extensionName);
        }

        std::cout << "required extensions:" << std::endl;
        auto requiredExtensions = getInstanceExtensions();
        for (const auto &required : requiredExtensions) {
            std::cout << "\t" << required << std::endl;
            if (available.find(required) == available.end()) {
                throw std::runtime_error("Missing required glfw extension");
            }
        }
    }

#ifndef RT64_MINIMAL
    void Device::framebufferResizeCallback(GLFWwindow* glfwWindow, int width, int height) {
        auto rt64Device = reinterpret_cast<Device *>(glfwGetWindowUserPointer(glfwWindow));
        rt64Device->framebufferResized = true;
        rt64Device->width = width;
        rt64Device->height = height;
    }
#endif

    void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) {
            func(instance, debugMessenger, pAllocator);
        }
    }

    Device::~Device() {
#ifndef RT64_MINIMAL
        vkDeviceWaitIdle(vkDevice);

        cleanupSwapChain();
        vkDestroySurfaceKHR(vkInstance, vkSurface, nullptr);
        vkDestroyRenderPass(vkDevice, renderPass, nullptr);
        vkDestroyCommandPool(vkDevice, commandPool, nullptr);
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(vkDevice, imageAvailableSemaphores[i], nullptr);
            vkDestroySemaphore(vkDevice, renderFinishedSemaphores[i], nullptr);
            vkDestroyFence(vkDevice, inFlightFences[i], nullptr);
        }

        // Destroy the scenes
        auto scenesCopy = scenes;
        for (Scene* scene : scenesCopy) {
            delete scene;
        }
        
        rtAllocator.deinit();
		vmaDestroyAllocator(allocator);
        // TODO: Actually delete stuff instead of just leaking everything.
#endif
        if (enableValidationLayers) {
            DestroyDebugUtilsMessengerEXT(vkInstance, debugMessenger, nullptr);
        }
        vkDestroyDevice(vkDevice, nullptr);
        vkDestroyInstance(vkInstance, nullptr);
        // Destroy the window
        glfwDestroyWindow(window);
        glfwTerminate();
        RT64_LOG_CLOSE();
    }

    // Public

#ifndef RT64_MINIMAL

    /********************** Getters **********************/

    // Returns Vulkan device
    VkDevice& Device::getVkDevice() { return vkDevice; }
    // Returns physical device
    VkPhysicalDevice& Device::getPhysicalDevice() { return physicalDevice; }
    // Returns VMA allocator
    VmaAllocator& Device::getMemAllocator() { return allocator; }
    // Returns RT allocator
	nvvk::ResourceAllocator& Device::getRTAllocator() { return rtAllocator; }

    VkRenderPass& Device::getRenderPass() { return renderPass; };
    VkExtent2D& Device::getSwapchainExtent() { return swapChainExtent; }
	VkViewport& Device::getViewport() { return vkViewport; }
	VkRect2D& Device::getScissors() { return vkScissorRect; }
	int Device::getWidth() { return swapChainExtent.width; }
	int Device::getHeight() { return swapChainExtent.height; }
	double Device::getAspectRatio() { return (double)swapChainExtent.width / (double)swapChainExtent.height; }
	int Device::getCurrentFrameIndex() { return currentFrame; }

	VkCommandBuffer& Device::getCurrentCommandBuffer() { return commandBuffers[currentFrame]; }
    VkFramebuffer& Device::getCurrentSwapchainFramebuffer() { return swapChainFramebuffers[framebufferIndex]; };
    IDxcCompiler* Device::getDxcCompiler() { return d3dDxcCompiler; }
    IDxcLibrary* Device::getDxcLibrary() { return d3dDxcLibrary; }

    void Device::initRTBuilder(nvvk::RaytracingBuilderKHR& rtBuilder) {
        rtBuilder.setup(vkDevice, &rtAllocator, vkctx.m_queueGCT.queueIndex);
    }

    // Adds a scene to the device
    void Device::addScene(Scene* scene) {
        assert(scene != nullptr);
        scenes.push_back(scene);
    }
    
    // Removes a scene from the device
    void Device::removeScene(Scene *scene) {
        assert(scene != nullptr);
        scenes.erase(std::remove(scenes.begin(), scenes.end(), scene), scenes.end());
    }

    // Adds a scene to the device

    void Device::addShader(Shader *shader) {
        assert(shader != nullptr);
        if (shader->hasHitGroups()) {
            shaders.push_back(shader);
            // d3dRtStateObjectDirty = true;
        }
    }

    // Removes a shader from the device
    void Device::removeShader(Shader *shader) {
        assert(shader != nullptr);
        if (shader->hasHitGroups()) {
            shaders.erase(std::remove(shaders.begin(), shaders.end(), shader), shaders.end());
            // d3dRtStateObjectDirty = true;
        }
    }

    void Device::addDepthImageView(VkImageView* depthImageView) {
        assert(depthImageView != nullptr);
        depthViews.push_back(depthImageView);
    }

    void Device::removeDepthImageView(VkImageView* depthImageView) {
        assert(depthImageView != nullptr);
        depthViews.erase(std::remove(depthViews.begin(), depthViews.end(), depthImageView), depthViews.end());
    }

    // Creates an allocated buffer. You must pass in a pointer to an AllocatedResource. Once the function does its thing, the pointer will point to the newly created AllocatedBuffer with the buffer
    VkResult Device::allocateBuffer(
            VkDeviceSize bufferSize, 
            VkBufferUsageFlags bufferUsage, 
            VmaMemoryUsage memUsage, 
            VmaAllocationCreateFlags allocProperties, 
            AllocatedBuffer* alre
        ) 
    {
        VkResult res;
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = bufferUsage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.usage = memUsage;
        allocCreateInfo.flags = allocProperties;
        VmaAllocationInfo allocInfo = {};

        VmaAllocation* allocation = new VmaAllocation;
        VkBuffer* buffer = new VkBuffer;
        res = vmaCreateBuffer(allocator, &bufferInfo, &allocCreateInfo, buffer, allocation, &allocInfo);
        alre->init(&allocator, allocation, buffer);
        return res;
    }

    // Creates an allocated image. You must pass in a pointer to an AllocatedResource. Once the function does its thing, the pointer will point to the newly created AllocatedImage with the image
    VkResult Device::allocateImage(
            uint32_t width, uint32_t height, 
            VkImageType imageType, 
            VkFormat imageFormat, 
            VkImageTiling imageTiling, 
            VkImageLayout initLayout, 
            VkImageUsageFlags imageUsage, 
            VmaMemoryUsage memUsage, 
            VmaAllocationCreateFlags allocProperties, 
            AllocatedImage* alre
        ) 
    {
        VkResult res;
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = imageType;
        imageInfo.format = imageFormat;
        imageInfo.tiling = imageTiling;
        imageInfo.initialLayout = initLayout;
        imageInfo.usage = imageUsage;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
            imageInfo.extent.depth = 1;             // Maybe if I were to like
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.flags = 0; // Optional

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.usage = memUsage;
        allocCreateInfo.flags = allocProperties;
        VmaAllocationInfo allocInfo = {};

        VmaAllocation* allocation = new VmaAllocation;
        VkImage* image = new VkImage;
        res = vmaCreateImage(allocator, &imageInfo, &allocCreateInfo, image, allocation, &allocInfo);
        alre->init(&allocator, allocation, image);
        return res;
    }

    // Copies a buffer into another
    //  If a pointer to a command buffer is passed into the function, this function will use the passed-in command buffer
    //  Otherwise, it would create a new command buffer just for this
    void Device::copyBuffer(VkBuffer src, VkBuffer dest, VkDeviceSize size, VkCommandBuffer* commandBuffer) {
        bool oneTime = !commandBuffer;
        if (!commandBuffer) {
            commandBuffer = beginSingleTimeCommands();
        }

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0; // Optional
        copyRegion.dstOffset = 0; // Optional
        copyRegion.size = size;
        vkCmdCopyBuffer(*commandBuffer, src, dest, 1, &copyRegion);

        if (oneTime) {
            endSingleTimeCommands(commandBuffer);
        }
    }

    // Turns the layout of one image into another
    //  If a pointer to a command buffer is passed into the function, this function will use the passed-in command buffer
    //  Otherwise, it would create a new command buffer just for this
    void Device::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, VkCommandBuffer* commandBuffer) {
        bool oneTime = !commandBuffer;
        if (!commandBuffer) {
            commandBuffer = beginSingleTimeCommands();
        }

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0; // TODO
        barrier.dstAccessMask = 0; // TODO

        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        } else {
            throw std::invalid_argument("unsupported layout transition!");
        }

        vkCmdPipelineBarrier(
            *commandBuffer,
            sourceStage, destinationStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );

        if (oneTime) {
            // The fact that you passed on passing in a pointer to a
            //  command buffer kinda implies that's all you wanna do
            //  with the command buffer :/
            endSingleTimeCommands(commandBuffer);
        }
    }

    //  If a pointer to a command buffer is passed into the function, this function will use the passed-in command buffer
    //  Otherwise, it would create a new command buffer just for this
    void Device::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, VkCommandBuffer* commandBuffer) {
        bool oneTime = !commandBuffer;
        if (!commandBuffer) {
            commandBuffer = beginSingleTimeCommands();
        }

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {
            width,
            height,
            1
        };

        vkCmdCopyBufferToImage(
            *commandBuffer,
            buffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &region
        );

        if (oneTime) {
            endSingleTimeCommands(commandBuffer);
        }
    }

    VkImageView Device::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspectFlags;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView imageView;
        VK_CHECK(vkCreateImageView(vkDevice, &viewInfo, nullptr, &imageView));
        return imageView;
    }

    VkBufferView Device::createBufferView(VkBuffer& buffer, VkFormat format, VkBufferViewCreateFlags flags, VkDeviceSize size) {
        VkBufferViewCreateInfo viewInfo {};
        viewInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
        viewInfo.buffer = buffer;
        viewInfo.format = format;
        viewInfo.flags = flags;
        viewInfo.offset = 0;
        viewInfo.range = size;

        VkBufferView bufferView;
        VK_CHECK(vkCreateBufferView(vkDevice, &viewInfo, nullptr, &bufferView));
        return bufferView;
    }

    // Begin the use of a command buffer that runs temporarily
    VkCommandBuffer* Device::beginSingleTimeCommands() { return beginSingleTimeCommands(nullptr); }
    // Begin the use of the passed in command buffer that runs temporarily
    VkCommandBuffer* Device::beginSingleTimeCommands(VkCommandBuffer* commandBuffer) {
        // If no command buffer is passed in, create one
        if (!commandBuffer) {
            commandBuffer = new VkCommandBuffer;
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandPool = commandPool;
            allocInfo.commandBufferCount = 1;
            vkAllocateCommandBuffers(vkDevice, &allocInfo, commandBuffer);
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(*commandBuffer, &beginInfo);

        return commandBuffer;
    }
    // Ends the passed in command buffer and destroys the command buffer
    void Device::endSingleTimeCommands(VkCommandBuffer* commandBuffer) {
        vkEndCommandBuffer(*commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = commandBuffer;

        vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);

        vkFreeCommandBuffers(vkDevice, commandPool, 1, commandBuffer);
    }
    #endif

}

// Library exports

DLEXPORT RT64_DEVICE* RT64_CreateDevice(void* glfwWindow) {
	try {
		return (RT64_DEVICE*)(new RT64::Device(static_cast<GLFWwindow*>(glfwWindow)));
	}
	RT64_CATCH_EXCEPTION();
	return nullptr;
}

DLEXPORT void RT64_DestroyDevice(RT64_DEVICE* devicePtr) {
	assert(devicePtr != nullptr);
	try {
		delete (RT64::Device*)(devicePtr);
	}
	RT64_CATCH_EXCEPTION();
}

#ifndef RT64_MINIMAL

DLEXPORT void RT64_DrawDevice(RT64_DEVICE* devicePtr, int vsyncInterval, double delta) {
	assert(devicePtr != nullptr);
	try {
		RT64::Device* device = (RT64::Device*)(devicePtr);
		device->draw(vsyncInterval, delta);
	}
	RT64_CATCH_EXCEPTION();
}

#endif