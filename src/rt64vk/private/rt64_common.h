/*
*   RT64VK
*/

#pragma once

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <string>
#include <vector>
#include <stdio.h>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include "../contrib/VulkanMemoryAllocator/vk_mem_alloc.h"

#include "../public/rt64.h"

#ifdef _WIN32
	#define DLEXPORT extern "C" __declspec(dllexport)
#else
	#define DLEXPORT extern "C" __attribute__((visibility("default")))
#endif

#define HEAP_INDEX(x) (int)(RT64::HeapIndices::x)
#define UAV_INDEX(x) (int)(RT64::UAVIndices::x)
#define SRV_INDEX(x) (int)(RT64::SRVIndices::x)
#define CBV_INDEX(x) (int)(RT64::CBVIndices::x)
#define SRV_TEXTURES_MAX 512

namespace RT64 {
	// Matches order in heap used in shader binding table.
	enum class HeapIndices : int {
		gViewDirection,
		gShadingPosition,
		gShadingNormal,
		gShadingSpecular,
		gShadingEmissive,
		gShadingRoughness,
		gShadingMetalness,
		gShadingAmbient,
		gDiffuse,
		gInstanceId,
		gDirectLightAccum,
		gSpecularLightAccum,
		gIndirectLightAccum,
		gReflection,
		gRefraction,
		gTransparent,
		gVolumetricFog,
		gFlow,
		gNormal,
		gDepth,
		gPrevNormal,
		gPrevDepth,
		gPrevDirectLightAccum,
		gPrevIndirectLightAccum,
		gFilteredDirectLight,
		gFilteredIndirectLight,
		gFog,
		gHitDistAndFlow,
		gHitColor,
		gHitNormal,
		gHitSpecular,
		gHitInstanceId,
		gHitEmissive,
		gHitRoughness,
		gHitMetalness,
		gHitAmbient,
		gBackground,
		gParams,
		SceneBVH,
		SceneLights,
		instanceTransforms,
		instanceMaterials,
		gBlueNoise,
		gTextures,
		MAX
	};

	enum class UAVIndices : int {
		gViewDirection,
		gShadingPosition,
		gShadingNormal,
		gShadingSpecular,
		gShadingEmissive,
		gShadingRoughness,
		gShadingMetalness,
		gShadingAmbient,
		gDiffuse,
		gInstanceId,
		gDirectLightAccum,
		gSpecularLightAccum,
		gIndirectLightAccum,
		gReflection,
		gRefraction,
		gTransparent,
		gVolumetricFog,
		gFlow,
		gNormal,
		gDepth,
		gPrevNormal,
		gPrevDepth,
		gPrevDirectLightAccum,
		gPrevIndirectLightAccum,
		gFilteredDirectLight,
		gFilteredIndirectLight,
		gFog,
		gHitDistAndFlow,
		gHitColor,
		gHitNormal,
		gHitSpecular,
		gHitInstanceId,
		gHitEmissive,
		gHitRoughness,
		gHitMetalness,
		gHitAmbient,
		MAX
	};

	enum class SRVIndices : int {
		SceneBVH,
		gBackground,
		vertexBuffer,
		indexBuffer,
		SceneLights,
		instanceTransforms,
		instanceMaterials,
		gBlueNoise,
		gTextures
	};

	enum class CBVIndices : int {
		gParams
	};

	enum class UpscaleMode {
		Bilinear,
		FSR,
		DLSS,
		XeSS
	};

	// Some shared shader constants.
	static const unsigned int VisualizationModeFinal = 0;
	static const unsigned int VisualizationModeShadingPosition = 1;
	static const unsigned int VisualizationModeShadingNormal = 2;
	static const unsigned int VisualizationModeShadingSpecular = 3;
	static const unsigned int VisualizationModeDiffuse = 4;
	static const unsigned int VisualizationModeInstanceID = 5;
	static const unsigned int VisualizationModeDirectLightRaw = 6;
	static const unsigned int VisualizationModeDirectLightFiltered = 7;
	static const unsigned int VisualizationModeIndirectLightRaw = 8;
	static const unsigned int VisualizationModeIndirectLightFiltered = 9;
	static const unsigned int VisualizationModeReflection = 10;
	static const unsigned int VisualizationModeRefraction = 11;
	static const unsigned int VisualizationModeTransparent = 12;
	static const unsigned int VisualizationModeMotionVectors = 13;
	static const unsigned int VisualizationModeDepth = 14;

	// Error string for last error or exception that was caught.
	extern std::string GlobalLastError;

#ifndef RT64_MINIMAL
	class AllocatedResource {
		private:
			VmaAllocation* allocation = nullptr;
			VmaAllocator* allocator = nullptr;
			VkBuffer* buffer = nullptr;
			VkImage* image = nullptr;
			bool mapped = false;
			bool isImage = false;
		public:
			AllocatedResource() { }

			AllocatedResource(AllocatedResource& alre) {
				copy(alre);
			}

			// Copies an outside Alre into this one
			void copy(AllocatedResource& alre) {
				allocation = alre.allocation;
				allocator = alre.allocator;
				buffer = alre.buffer;
				image = alre.image;
				isImage = alre.isImage;
			}

			void init(VmaAllocator* allocator, VmaAllocation* allocation, VkBuffer* buffer) {
				assert(isNull());
				this->allocation = allocation;
				this->allocator = allocator;
				this->buffer = buffer;
			}

			void init(VmaAllocator* allocator, VmaAllocation* allocation, VkImage* image) {
				assert(isNull());
				this->allocation = allocation;
				this->allocator = allocator;
				this->image = image;
				isImage = true;
			}
			AllocatedResource(VmaAllocator* allocator, VmaAllocation* allocation, VkBuffer* buffer) {
				init(allocator, allocation, buffer);
			}
			AllocatedResource(VmaAllocator* allocator, VmaAllocation* allocation, VkImage* image) {
				init(allocator, allocation, image);
			}

			~AllocatedResource() { 
				allocator = nullptr;
				allocation = nullptr;
				buffer = nullptr;
				image = nullptr;
			}

			// Maps a portion of memory to the allocation
			// Returns the pointer to the first byte in memory
			void* mapMemory(void** ppData) {
				assert(!isNull() && !mapped);
				vmaMapMemory(*allocator, *allocation, ppData);
				mapped = true;
				// vmaUnmapMemory(*allocator, *allocation);
				return *ppData;
			}

			// Unmaps that portion of memory
			void unmapMemory() {
				assert(!isNull() && mapped);
				vmaUnmapMemory(*allocator, *allocation);
				mapped = false;
			}

			// Copies a portion of memory into the mapped memory
			// Returns the pointer to the first byte in memory
			void* setData(void* pData, size_t size) {
				assert(!isNull() && !mapped);
				void* ppData = pData;		// Make a new reference that's just a pointer to a pointer to the data
				vmaMapMemory(*allocator, *allocation, &ppData);
				memcpy(ppData, pData, size);
				vmaUnmapMemory(*allocator, *allocation);
				return ppData;
			}

			void setAllocationName(const char* name) {
				if (!isNull()) {
					vmaSetAllocationName(*allocator, *allocation, name);
				}
			}

			inline VkBuffer* getBuffer() const {
				if (!isNull()) {
					if (isImage) {
						return nullptr;
					}
					return buffer;
				}
				return nullptr;
			}

			inline VkImage* getImage() const {
				if (!isNull()) {
					if (isImage) {
						return image;
					}
					return nullptr;
				}
				return nullptr;
			}

			inline bool isNull() const {
				return (allocation == nullptr);
			}

			void destroyResource() {
				if (!isNull()) {
					if (mapped) {
						vmaUnmapMemory(*allocator, *allocation);
					}
					if (isImage) {
						vmaDestroyImage(*allocator, *image, *allocation);
					} else {
						vmaDestroyBuffer(*allocator, *buffer, *allocation);
					} 
					// vmaFreeMemory(*allocator, *allocation);
					allocation = nullptr;
					allocator = nullptr;
					buffer = nullptr;
					image = nullptr;
					isImage = false;
					mapped = false;
				}
			}
	};

	struct InstanceTransforms {
		glm::mat4x4 objectToWorld;
		glm::mat4x4 objectToWorldNormal;
		glm::mat4x4 objectToWorldPrevious;
	};

	struct AccelerationStructureBuffers {
		AllocatedResource scratch;
		uint64_t scratchSize;
		AllocatedResource result;
		uint64_t resultSize;
		AllocatedResource instanceDesc;
		uint64_t instanceDescSize;

		AccelerationStructureBuffers() {
			scratchSize = resultSize = instanceDescSize = 0;
		}

		void destroyResource() {
			scratch.destroyResource();
			result.destroyResource();
			instanceDesc.destroyResource();
			scratchSize = resultSize = instanceDescSize = 0;
		}
	};
#endif

#ifdef NDEBUG
#	define RT64_LOG_OPEN(x)
#	define RT64_LOG_CLOSE()
#	define RT64_LOG_PRINTF(x, ...)
#else
	extern FILE *GlobalLogFile;
#	define RT64_LOG_OPEN(x) do { GlobalLogFile = fopen(x, "wt"); } while (0)
#	define RT64_LOG_CLOSE() do { fclose(GlobalLogFile); } while (0)
#	define RT64_LOG_PRINTF(x, ...) do { fprintf(GlobalLogFile, (x), ##__VA_ARGS__); fprintf(GlobalLogFile, " (%s in %s:%d)\n", __FUNCTION__, __FILE__, __LINE__); fflush(GlobalLogFile); } while (0)
#endif

#define VK_CHECK( call )                                                            \
    do                                                                              \
    {                                                                               \
		VkResult vr = call;															\
        if (vr != VK_SUCCESS)														\
        {																	        \
			char errorMessage[512];													\
			snprintf(errorMessage, sizeof(errorMessage), "Vulkan call " #call " "	    \
				"failed with error code %X.", vr);									\
																					\
            throw std::runtime_error(errorMessage);                                 \
        }                                                                           \
    } while( 0 )

#define RT64_CATCH_EXCEPTION()							\
	catch (const std::runtime_error &e) {				\
		RT64::GlobalLastError = std::string(e.what());	\
		fprintf(stderr, "%s\n", e.what());				\
	}
}

#define ROUND_UP(v, powerOf2Alignment) (((v) + (powerOf2Alignment)-1) & ~((powerOf2Alignment)-1))