/*
*   RT64VK
*/

#pragma once

#include "rt64_common.h"
#include "rt64_device.h"
#include <vulkan/vulkan.h>

namespace RT64 
{
	class Device;

	class Mesh 
    {
        private:
            Device *device;
            AllocatedBuffer vertexBuffer;             // The actual one; the one optimized for the GPU
            AllocatedBuffer stagingVertexBuffer;      // The temporary one used for like doing stuff on the CPU
            AllocatedBuffer indexBuffer;
            AllocatedBuffer stagingIndexBuffer;
            int vertexCount;
            int vertexStride;
            int indexCount;
            AccelerationStructureBuffers blasBuffers;
            int flags;

            void createBottomLevelAS(std::vector<std::pair<VkBuffer*, uint32_t>> vVertexBuffers, std::vector<std::pair<VkBuffer *, uint32_t>> vIndexBuffers);
            // nvvk::RaytracingBuilderKHR::BlasInput modelIntoVkGeo(VkBuffer* vertexBuffer, uint32_t vertexCount, VkBuffer* indexBuffer, uint32_t indexCount);
        public:
            Mesh(Device* device, int flags);
            virtual ~Mesh();
            void updateVertexBuffer(void* vertexArray, int vertexCount, int vertexStride);
            VkBuffer* getVertexBuffer() const;
            const VkBufferView* getVertexBufferView() const;
            int getVertexCount() const;
            void updateIndexBuffer(unsigned int* indexArray, int indexCount);
            VkBuffer* getIndexBuffer() const;
            const VkBufferView* getIndexBufferView() const;
            int getIndexCount() const;
            void updateBottomLevelAS();
            VkBuffer* getBottomLevelASResult() const;
	};
};