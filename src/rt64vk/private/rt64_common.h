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
#include <nvmath/nvmath.h>
#include <glm/glm.hpp>
#include <bits/stl_algo.h>		// Idk why I know have to throw in this include in other than it makes it compile
#include <vulkan/vulkan.hpp>
#define RT64_VULKAN_VERSION VK_API_VERSION_1_2

// Oh hey, that dxc rly do be outputing spir-v code
#ifndef _WIN32
    #include <dxc/include/linux/dxcapi.h>
#endif

#include "../contrib/VulkanMemoryAllocator/vk_mem_alloc.h"

#include "../public/rt64.h"

#ifdef _WIN32
	#define DLEXPORT extern "C" __declspec(dllexport)
#else
	#define DLEXPORT extern "C" __attribute__((visibility("default")))
#endif

#define CBV_SHIFT		0
#define UAV_SHIFT		1024
#define SRV_SHIFT		2048
#define SAMPLER_SHIFT	3072

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
		protected:
			VmaAllocation* allocation = nullptr;
			VmaAllocator* allocator = nullptr;
			bool mapped = false;
		public:
			AllocatedResource() {}

			virtual ~AllocatedResource() {}

			// Maps a portion of memory to the allocation
			// Returns the pointer to the first byte in memory
			virtual void* mapMemory(void** ppData) {
				assert(!isNull() && !mapped);
				vmaMapMemory(*allocator, *allocation, ppData);
				mapped = true;
				return *ppData;
			}

			// Unmaps that portion of memory
			virtual void unmapMemory() {
				assert(!isNull() && mapped);
				vmaUnmapMemory(*allocator, *allocation);
				mapped = false;
			}

			// Copies a portion of memory into the mapped memory
			// Returns the pointer to the first byte in memory
			virtual void* setData(void* pData, uint64_t size) {
				assert(!isNull() && !mapped);
				void* ppData = pData;		// Make a new reference that's just a pointer to a pointer to the data
				vmaMapMemory(*allocator, *allocation, &ppData);
				memcpy(ppData, pData, size);
				vmaUnmapMemory(*allocator, *allocation);
				return ppData;
			}

			virtual void setAllocationName(const char* name) {
				if (!isNull()) {
					vmaSetAllocationName(*allocator, *allocation, name);
				}
			}

			virtual void* getResource() const { return nullptr; }

			virtual bool isNull() const {
				return (allocation == nullptr);
			}

			virtual void destroyResource() {
				if (!isNull()) {
					if (mapped) {
						vmaUnmapMemory(*allocator, *allocation);
					}
					allocation = nullptr;
					allocator = nullptr;
					mapped = false;
				}
			}
	};

	class AllocatedBuffer : public AllocatedResource {
		private:
			VkBuffer* buffer = nullptr;
			VkDescriptorBufferInfo info;
			
		public:
			AllocatedBuffer() { }

			AllocatedBuffer(AllocatedBuffer& albo) {
				copy(albo);
			}

			// Copies an outside Albo into this one
			void copy(AllocatedBuffer& albo) {
				allocation = albo.allocation;
				allocator = albo.allocator;
				buffer = albo.buffer;
				mapped = albo.mapped;
			}

			void init(VmaAllocator* allocator, VmaAllocation* allocation, VkBuffer* buffer) {
				assert(isNull());
				this->allocation = allocation;
				this->allocator = allocator;
				this->buffer = buffer;
			}
			
			AllocatedBuffer(VmaAllocator* allocator, VmaAllocation* allocation, VkBuffer* buffer) {
				init(allocator, allocation, buffer);
			}

			~AllocatedBuffer() { 
				allocator = nullptr;
				allocation = nullptr;
				buffer = nullptr;
			}

			inline void* getResource() const { return getBuffer(); }

			inline VkBuffer* getBuffer() const {
				assert(!isNull());
				return buffer;
			}

			void destroyResource() {
				if (!isNull()) {
					if (mapped) {
						vmaUnmapMemory(*allocator, *allocation);
					}
					vmaDestroyBuffer(*allocator, *buffer, *allocation);
					// vmaFreeMemory(*allocator, *allocation);
					allocation = nullptr;
					allocator = nullptr;
					buffer = nullptr;
					mapped = false;
					info.buffer = nullptr;
					info.offset = 0;
					info.range = 0;
				}
			}

			void setDescriptorInfo(VkDeviceSize size, uint64_t offset) {
				info.buffer = *buffer;
				info.offset = offset;
				info.range = size;
			}

			VkDescriptorBufferInfo& getDescriptorInfo() {
				return info;
			}
		};

	class AllocatedImage :  public AllocatedResource {
		private:
			VkImage* image = nullptr;
			VkDescriptorImageInfo info;
			
		public:
			AllocatedImage() { }

			AllocatedImage(AllocatedImage& albo) {
				copy(albo);
			}

			// Copies an outside Albo into this one
			void copy(AllocatedImage& alime) {
				allocation = alime.allocation;
				allocator = alime.allocator;
				image = alime.image;
				mapped = alime.mapped;
			}

			void init(VmaAllocator* allocator, VmaAllocation* allocation, VkImage* image) {
				assert(isNull());
				this->allocation = allocation;
				this->allocator = allocator;
				this->image = image;
			}
			
			AllocatedImage(VmaAllocator* allocator, VmaAllocation* allocation, VkImage* image) {
				init(allocator, allocation, image);
			}

			~AllocatedImage() { 
				allocator = nullptr;
				allocation = nullptr;
				image = nullptr;
			}

			inline void* getResource() const { return getImage(); }

			inline VkImage* getImage() const {
				assert(!isNull());
				return image;
			}

			void destroyResource() {
				if (!isNull()) {
					if (mapped) {
						vmaUnmapMemory(*allocator, *allocation);
					}
					vmaDestroyImage(*allocator, *image, *allocation);
					// vmaFreeMemory(*allocator, *allocation);
					allocation = nullptr;
					allocator = nullptr;
					image = nullptr;
					mapped = false;
				}
			}

			void setDescriptorInfo(VkImageView* imageView, VkSampler* sampler) {
				info.imageView = *imageView;
				info.sampler = *sampler;
			}

			VkDescriptorImageInfo& getDescriptorInfo() {
				return info;
			}
	};

	inline glm::mat4 convertNVMATHtoGLMMatrix(const nvmath::mat4f& b) {
		glm::mat4 a;
		a[0][0] = b.a00;
		a[0][1] = b.a01;
		a[0][2] = b.a02;
		a[0][3] = b.a03;

		a[1][0] = b.a10;
		a[1][1] = b.a11;
		a[1][2] = b.a12;
		a[1][3] = b.a13;
		
		a[2][0] = b.a20;
		a[2][1] = b.a21;
		a[2][2] = b.a22;
		a[2][3] = b.a23;
		
		a[3][0] = b.a30;
		a[3][1] = b.a31;
		a[3][2] = b.a32;
		a[3][3] = b.a33;
		return a;
	}

	inline nvmath::mat4f convertGLMtoNVMATHMatrix(const glm::mat4& b) {
		nvmath::mat4f a;
		a.a00 = b[0][0];
		a.a01 = b[0][1];
		a.a02 = b[0][2];
		a.a03 = b[0][3];

		a.a10 = b[1][0];
		a.a11 = b[1][1];
		a.a12 = b[1][2];
		a.a13 = b[1][3];

		a.a20 = b[2][0];
		a.a21 = b[2][1];
		a.a22 = b[2][2];
		a.a23 = b[2][3];

		a.a30 = b[3][0];
		a.a31 = b[3][1];
		a.a32 = b[3][2];
		a.a33 = b[3][3];
		return a;
	}

	inline void operator+=(RT64_VECTOR3 &a, const RT64_VECTOR3& b) {
		a.x += b.x;
		a.y += b.y;
		a.z += b.z;
	}

	inline RT64_VECTOR3 operator+(const RT64_VECTOR3& a, const RT64_VECTOR3& b) {
		return { a.x + b.x, a.y + b.y, a.z + b.z };
	}

	inline RT64_VECTOR3 operator-(const RT64_VECTOR3& a, const RT64_VECTOR3& b) {
		return { a.x - b.x, a.y - b.y, a.z - b.z };
	}

	inline RT64_VECTOR3 operator*(const RT64_VECTOR3& a, const float v) {
		return { a.x * v, a.y * v, a.z * v };
	}

	inline RT64_VECTOR3 operator/(const RT64_VECTOR3& a, const float v) {
		return { a.x / v, a.y / v, a.z / v };
	}

	inline float Length(const RT64_VECTOR3& a) {
		float sqrLength = a.x * a.x + a.y * a.y + a.z * a.z;
		return sqrt(sqrLength);
	}

	inline RT64_VECTOR3 Normalize(const RT64_VECTOR3& a) {
		float l = Length(a);
		return (l > 0.0f) ? (a / l) : a;
	}

	inline RT64_VECTOR3 Cross(const RT64_VECTOR3& a, const RT64_VECTOR3& b) {
		return {
			a.y * b.z - b.y * a.z,
			a.z * b.x - b.z * a.x,
			a.x * b.y - b.x * a.y
		};
	}

	inline RT64_VECTOR3 DirectionFromTo(const RT64_VECTOR3& a, const RT64_VECTOR3& b) {
		RT64_VECTOR3 dir = { b.x - a.x,  b.y - a.y, b.z - a.z };
		float length = Length(dir);
		return dir / length;
	}

	inline RT64_VECTOR4 ToVector4(const RT64_VECTOR3& a, float w) {
		return { a.x, a.y, a.z, w };
	}

	inline void CalculateTextureRowWidthPadding(uint32_t rowPitch, uint32_t& rowWidth, uint32_t& rowPadding) {
		const int RowMultiple = 256;
		rowWidth = rowPitch;
		rowPadding = (rowWidth % RowMultiple) ? RowMultiple - (rowWidth % RowMultiple) : 0;
		rowWidth += rowPadding;
	}

	inline float HaltonSequence(int i, int b) {
		float f = 1.0;
		float r = 0.0;
		while (i > 0) {
			f = f / float(b);
			r = r + f * float(i % b);
			i = i / b;
		}

		return r;
	}

	inline RT64_VECTOR2 HaltonJitter(int frame, int phases) {
		return { HaltonSequence(frame % phases + 1, 2) - 0.5f, HaltonSequence(frame % phases + 1, 3) - 0.5f };
	}

	struct InstanceTransforms {
		glm::mat4x4 objectToWorld;
		glm::mat4x4 objectToWorldNormal;
		glm::mat4x4 objectToWorldPrevious;
	};

	struct AccelerationStructureBuffers {
		AllocatedBuffer scratch;
		uint64_t scratchSize;
		AllocatedBuffer result;
		uint64_t resultSize;
		AllocatedBuffer instanceDesc;
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

#define D3D12_CHECK( call )                                                         \
    do                                                                              \
    {                                                                               \
        HRESULT hr = call;                                                          \
        if (FAILED(hr))														        \
        {																	        \
			char errorMessage[512];													\
			snprintf(errorMessage, sizeof(errorMessage), "D3D12 call " #call " "	\
				"failed with error code %X.", hr);									\
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

#define CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT			256

#define ROUND_UP(v, powerOf2Alignment) (((v) + (powerOf2Alignment)-1) & ~((powerOf2Alignment)-1))