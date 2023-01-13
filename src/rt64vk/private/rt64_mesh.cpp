/*
*   RT64VK
*/

#ifndef RT64_MINIMAL

#include "../public/rt64.h"
#include "rt64_mesh.h"
#include <nvvk/buffers_vk.hpp>

// Private

namespace RT64 
{
    Mesh::Mesh(Device *device, int flags) {
        assert(device != nullptr);
        this->device = device;
        this->flags = flags;
        vertexCount = 0;
        indexCount = 0;
        vertexStride = 0;
    }

    Mesh::~Mesh() {
        vertexBuffer.destroyResource();
        vertexBufferUpload.destroyResource();
        indexBuffer.destroyResource();
        indexBufferUpload.destroyResource();
        blasBuffers.destroyResource();
    }

    // This method copies the passed in vertex array into  
    void Mesh::updateVertexBuffer(void *vertexArray, int vertexCount, int vertexStride) {
        const uint32_t vertexBufferSize = vertexCount * vertexStride;

        if (vertexBuffer.isNull() && ((this->vertexCount != vertexCount) || (this->vertexStride != vertexStride))) {
            vertexBuffer.destroyResource();
            vertexBufferUpload.destroyResource();
            // Discard the BLAS since it won't be compatible anymore even if it's updatable.
            blasBuffers.destroyResource();
        }

        if (vertexBuffer.isNull()) {
            // CD3DX12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
            // vertexBufferUpload = device->allocateResource(D3D12_HEAP_TYPE_UPLOAD, &uploadBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr);
            
            // CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
            // vertexBuffer = device->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr);
            VkBufferCreateInfo bufferInfo {}; bufferInfo.size = vertexBufferSize;
            // vertexBufferUpload = device->allocateBuffer(new VkBuffer, &bufferInfo);
            // vertexBuffer = device->allocateBuffer(new VkBuffer, &bufferInfo);
        }

        // Copy data to upload heap.
        uint8_t* dataBegin;
        VkBufferCopy bufferCopy = {};
        bufferCopy.srcOffset = (VkDeviceSize)vertexBuffer.getBuffer();
        // CD3DX12_RANGE readRange(0, 0);
        // D3D12_CHECK(vertexBufferUpload.Get()->Map(0, &readRange, reinterpret_cast<void**>(&pDataBegin)));
        // memcpy(pDataBegin, vertexArray, vertexBufferSize);
        // vertexBufferUpload.Get()->Unmap(0, nullptr);
        // vertexBufferUpload.getResource()->
        
        // // Copy resource to the real default resource.
        // device->getD3D12CommandList()->CopyResource(vertexBuffer.Get(), vertexBufferUpload.Get());

        // // Wait for the resource to finish copying before switching to generic read.
        // CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
        // device->getD3D12CommandList()->ResourceBarrier(1, &transition);

        // // Configure vertex buffer view.
        // d3dVertexBufferView.BufferLocation = vertexBuffer.Get()->GetGPUVirtualAddress();
        // d3dVertexBufferView.StrideInBytes = vertexStride;
        // d3dVertexBufferView.SizeInBytes = vertexBufferSize;

        // // Store the new vertex count and stride.
        // this->vertexCount = vertexCount;
        // this->vertexStride = vertexStride;
    }

    // vVertexBuffers is a tuple vector, with the 1st thing being a vertex 
    //  buffer and the second thing being the number of verticies
    // vIndexBuffers is another tuple vector, a pairing of an index buffer
    //  (a buffer of pointers into the vertex buffer) and the number of
    //  indicies.
    //      Index buffers are used to point to indexes as a means to not
    //       be redundant and reduce the amount of duplicate verticies
    void Mesh::createBottomLevelAS(std::vector<std::pair<VkBuffer*, uint32_t>> vVertexBuffers, std::vector<std::pair<VkBuffer*, uint32_t>> vIndexBuffers) {
        bool updatable = flags & RT64_MESH_RAYTRACE_UPDATABLE;
        bool fastTrace = flags & RT64_MESH_RAYTRACE_FAST_TRACE;
        bool compact = flags & RT64_MESH_RAYTRACE_COMPACT;
        if (!updatable) {
            // Release the previously stored AS buffers if there's any.
            blasBuffers.destroyResource();
        }

        std::vector<nvvk::RaytracingBuilderKHR::BlasInput> allBlas;
        allBlas.reserve(vVertexBuffers.size());
        for (size_t i = 0; i < vVertexBuffers.size(); i++) {
            nvvk::RaytracingBuilderKHR::BlasInput blas = modelIntoVkGeo(vVertexBuffers[i].first, vVertexBuffers[i].second, vIndexBuffers[i].first, vIndexBuffers[i].second);
            allBlas.emplace_back(blas);
        }

        device->getRTBuilder().buildBlas(allBlas, flags >> 1);
    }

    // Based off a function found in nvpro_core/nvvkhl/gltf_scene_rtx.cpp
    nvvk::RaytracingBuilderKHR::BlasInput Mesh::modelIntoVkGeo(VkBuffer* vertexBuffer, uint32_t vertexCount, VkBuffer* indexBuffer, uint32_t indexCount) {
        VkDeviceAddress vertexAddress = nvvk::getBufferDeviceAddress(device->getVkDevice(), *vertexBuffer);
        VkDeviceAddress indexAddress  = nvvk::getBufferDeviceAddress(device->getVkDevice(), *indexBuffer);

        uint32_t maxPrimitiveCount = indexCount / 3;        // Max possible number of triangles

        // Describe buffer.
        VkAccelerationStructureGeometryTrianglesDataKHR triangles{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
        triangles.vertexFormat             = VK_FORMAT_R32G32B32_SFLOAT;  // vec3 vertex position data.
        triangles.vertexData.deviceAddress = vertexAddress;
        triangles.vertexStride             = this->vertexStride;
        // Describe index data (32-bit unsigned int)
        triangles.indexType               = VK_INDEX_TYPE_UINT32;
        triangles.indexData.deviceAddress = indexAddress;
        // Indicate identity transform by setting transformData to null device pointer.
        //triangles.transformData = {};
        triangles.maxVertex = indexCount;
        
        // Identify the above data as containing opaque triangles.
        VkAccelerationStructureGeometryKHR asGeom{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        asGeom.geometryType       = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        asGeom.flags              = VK_GEOMETRY_OPAQUE_BIT_KHR;
        asGeom.geometry.triangles = triangles;

        // The entire array will be used to build the BLAS.
        VkAccelerationStructureBuildRangeInfoKHR offset;
        offset.firstVertex     = 0;
        offset.primitiveCount  = maxPrimitiveCount;
        offset.primitiveOffset = 0;
        offset.transformOffset = 0;

        // Our blas is made from only one geometry, but could be made of many geometries
        nvvk::RaytracingBuilderKHR::BlasInput input;
        input.asGeometry.emplace_back(asGeom);
        input.asBuildOffsetInfo.emplace_back(offset);

        return input;
    }
};

// Library Exports

DLEXPORT RT64_MESH *RT64_CreateMesh(RT64_DEVICE *devicePtr, int flags) {
	RT64::Device *device = (RT64::Device *)(devicePtr);
	return (RT64_MESH *)(new RT64::Mesh(device, flags));
}

DLEXPORT void RT64_SetMesh(RT64_MESH* meshPtr, void* vertexArray, int vertexCount, int vertexStride, unsigned int* indexArray, int indexCount) {
	assert(meshPtr != nullptr);
	assert(vertexArray != nullptr);
	assert(vertexCount > 0);
	assert(indexArray != nullptr);
	assert(indexCount > 0);
	RT64::Mesh *mesh = (RT64::Mesh *)(meshPtr);
	// mesh->updateVertexBuffer(vertexArray, vertexCount, vertexStride);
	// mesh->updateIndexBuffer(indexArray, indexCount);
	// mesh->updateBottomLevelAS();
}

DLEXPORT void RT64_DestroyMesh(RT64_MESH * meshPtr) {
	delete (RT64::Mesh *)(meshPtr);
}
#endif