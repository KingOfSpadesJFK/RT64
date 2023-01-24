/*
*   RT64VK
*/

#pragma once

#include "rt64_common.h"
#include "rt64_device.h"
#include <vulkan/vulkan.h>
#include <nvpro_core/nvvk/buffers_vk.hpp>
#include <nvvk/raytraceKHR_vk.hpp>

namespace RT64 
{
	class Device;

	class Mesh 
    {
        private:
            Device* device;
            AllocatedBuffer vertexBuffer;             // The actual one; the one optimized for the GPU
            AllocatedBuffer stagingVertexBuffer;      // The temporary one used for like doing stuff on the CPU
            AllocatedBuffer indexBuffer;
            AllocatedBuffer stagingIndexBuffer;
            int vertexCount;
            int vertexStride;
            int indexCount;
            bool builderActive = false;
            nvvk::RaytracingBuilderKHR builder;
            VkDeviceAddress blasAddress;
            int flags;

            void createBottomLevelAS(std::pair<VkBuffer&, uint32_t> vVertexBuffers, std::pair<VkBuffer&, uint32_t> vIndexBuffers);
            void modelIntoVkGeo(VkBuffer& vertexBuffer, uint32_t vertexCount, VkBuffer& indexBuffer, uint32_t indexCount, nvvk::RaytracingBuilderKHR::BlasInput& input);
        public:
            Mesh(Device* device, int flags);
            virtual ~Mesh();
            void updateVertexBuffer(void* vertexArray, int vertexCount, int vertexStride);
            VkBuffer& getVertexBuffer();
            int getVertexCount() const;
            void updateIndexBuffer(unsigned int* indexArray, int indexCount);
            VkBuffer& getIndexBuffer();
            int getIndexCount() const;
            nvvk::AccelKHR& getBlas();
            VkDeviceAddress getBlasAddress() const;
            void updateBottomLevelAS();
	};
};