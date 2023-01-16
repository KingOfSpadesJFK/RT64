/*
*   RT64VK
*/

#ifndef RT64_MINIMAL

#include "../public/rt64.h"
#include "rt64_mesh.h"

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
        stagingVertexBuffer.destroyResource();
        indexBuffer.destroyResource();
        stagingIndexBuffer.destroyResource();
        blasBuffers.destroyResource();
    }

    // This function copies the passed in vertex array into the buffer
    void Mesh::updateVertexBuffer(void *vertices, int vertexCount, int vertexStride) {
        const VkDeviceSize vertexBufferSize = vertexCount * vertexStride;

        // Delete if the vertex buffers are out of date
        if (!vertexBuffer.isNull() && ((this->vertexCount != vertexCount) || (this->vertexStride != vertexStride))) {
            vertexBuffer.destroyResource();
            stagingVertexBuffer.destroyResource();
            // Discard the BLAS since it won't be compatible anymore even if it's updatable.
            // blasBuffers.destroyResource();
        }

        if (vertexBuffer.isNull()) {
            device->allocateBuffer(vertexBufferSize, 
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VMA_MEMORY_USAGE_AUTO_PREFER_HOST, 
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, 
                &stagingVertexBuffer);
            device->allocateBuffer(vertexBufferSize, 
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 
                VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
                VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, 
                &vertexBuffer);
        }

        // Get the data into the staging buffer's memory
        stagingVertexBuffer.setData(vertices, vertexBufferSize);
        
        // Copy staging buffer to main buffer
        device->copyBuffer(*stagingVertexBuffer.getResource(), *vertexBuffer.getResource(), vertexBufferSize);

        this->vertexCount = vertexCount;
        this->vertexStride = vertexStride;
    }

    // Updates the index buffer with the passed in index array
    // Similar to updateVertexBuffer() but for indices.
    void Mesh::updateIndexBuffer(unsigned int *indices, int indexCount) {
        const VkDeviceSize indexBufferSize = indexCount * sizeof(unsigned int);

        // Delete if the index buffers are out of date
        if (!indexBuffer.isNull() && ((this->indexCount != indexCount))) {
            indexBuffer.destroyResource();
            stagingIndexBuffer.destroyResource();
            // Discard the BLAS since it won't be compatible anymore even if it's updatable.
            // blasBuffers.destroyResource();
        }

        if (indexBuffer.isNull()) {
            device->allocateBuffer(indexBufferSize, 
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VMA_MEMORY_USAGE_AUTO_PREFER_HOST, 
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, 
                &stagingIndexBuffer);
            device->allocateBuffer(indexBufferSize, 
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 
                VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
                VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, 
                &indexBuffer);
        }

        // Get the data into the staging buffer's memory
        stagingIndexBuffer.setData(indices, indexBufferSize);
        
        // Copy staging buffer to main buffer
        device->copyBuffer(*stagingIndexBuffer.getResource(), *indexBuffer.getResource(), indexBufferSize);
        
        this->indexCount = indexCount;
    }

    // vVertexBuffers is a tuple vector, with the 1st thing being a vertex 
    //  buffer and the second thing being the number of verticies
    // vIndexBuffers is another tuple vector, a pairing of an index buffer
    //  (a buffer of pointers into the vertex buffer) and the number of
    //  indicies.
    //      Index buffers are used to point to indexes as a means to not
    //       be redundant and reduce the amount of duplicate verticies
    void Mesh::createBottomLevelAS(std::vector<std::pair<VkBuffer*, uint32_t>> vVertexBuffers, std::vector<std::pair<VkBuffer*, uint32_t>> vIndexBuffers) {
        // bool updatable = flags & RT64_MESH_RAYTRACE_UPDATABLE;
        // bool fastTrace = flags & RT64_MESH_RAYTRACE_FAST_TRACE;
        // bool compact = flags & RT64_MESH_RAYTRACE_COMPACT;
        // if (!updatable) {
        //     // Release the previously stored AS buffers if there's any.
        //     blasBuffers.destroyResource();
        // }

        // std::vector<nvvk::RaytracingBuilderKHR::BlasInput> allBlas;
        // allBlas.reserve(vVertexBuffers.size());
        // for (size_t i = 0; i < vVertexBuffers.size(); i++) {
        //     nvvk::RaytracingBuilderKHR::BlasInput blas = modelIntoVkGeo(vVertexBuffers[i].first, vVertexBuffers[i].second, vIndexBuffers[i].first, vIndexBuffers[i].second);
        //     allBlas.emplace_back(blas);
        // }

        // device->getRTBuilder().buildBlas(allBlas, flags >> 1);
    }

    // Public 

    VkBuffer* Mesh::getVertexBuffer() const { return vertexBuffer.getResource(); }
    VkBuffer* Mesh::getIndexBuffer() const { return indexBuffer.getResource(); }
    int Mesh::getIndexCount() const { return indexCount; }
    int Mesh::getVertexCount() const { return vertexCount; }
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
	RT64::Mesh* mesh = (RT64::Mesh*)(meshPtr);
	mesh->updateVertexBuffer(vertexArray, vertexCount, vertexStride);
	mesh->updateIndexBuffer(indexArray, indexCount);
	// mesh->updateBottomLevelAS();
}

DLEXPORT void RT64_DestroyMesh(RT64_MESH * meshPtr) {
	delete (RT64::Mesh *)(meshPtr);
}
#endif