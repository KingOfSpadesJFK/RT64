/*
*   RT64VK
*/

#ifndef __RT64COMMON
#define __RT64COMMON

#ifdef _WIN32
	#define NOMINMAX
	#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <string>
#include <vector>
#include <stdio.h>
#include <glm/glm.hpp>
#define RT64_VULKAN_VERSION VK_API_VERSION_1_3
#include <vulkan/vulkan.hpp>

#ifndef _WIN32
	#include <bits/stl_algo.h>
	#include "../contrib/dxc/include/linux/dxcapi.h"
#else
	#include "../contrib/dxc/include/win/dxcapi.h"
#endif

#include "../contrib/VulkanMemoryAllocator/vk_mem_alloc.h"

#include "../public/rt64.h"

#ifdef _WIN32
	#define DLEXPORT extern "C" __declspec(dllexport)
#else
	#define DLEXPORT extern "C" __attribute__((visibility("default")))
#endif

#define CBV_SHIFT		0
#define UAV_SHIFT		100
#define SRV_SHIFT		200
#define SAMPLER_SHIFT	300

#define HEAP_INDEX(x) (int)(RT64::HeapIndices::x)
#define UAV_INDEX(x) (int)(RT64::UAVIndices::x)
#define SRV_INDEX(x) (int)(RT64::SRVIndices::x)
#define CBV_INDEX(x) (int)(RT64::CBVIndices::x)
#define SHADER_INDEX(x) (int)(RT64::ShaderIndices::x)
#define SRV_TEXTURES_MAX 512

namespace RT64 {
    enum ShaderIndices
    {
        primaryRayGen,
        directRayGen,
        indirectRayGen,
        reflectionRayGen,
        refractionRayGen,
		surfaceMiss,
		shadowMiss,
        MAX
    };

	enum class UAVIndices : int {
		gViewDirection,
		gShadingPosition,
		gShadingNormal,
		gShadingSpecular,
		gDiffuse,
		gInstanceId,
		gDirectLightAccum,
		gIndirectLightAccum,
		gReflection,
		gRefraction,
		gTransparent,
		gFlow,
		gReactiveMask,
		gLockMask,
		gNormal,
		gDepth,
		gPrevNormal,
		gPrevDepth,
		gPrevDirectLightAccum,
		gPrevIndirectLightAccum,
		gFilteredDirectLight,
		gFilteredIndirectLight,
		gDiffuseBG,
		gHitDistAndFlow,
		gHitColor,
		gHitNormal,
		gHitSpecular,
		gHitInstanceId,
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
		XeSS,
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

	inline unsigned int uniqueSamplerRegisterIndex(unsigned int filter, unsigned int hAddr, unsigned int vAddr) {
		// Index 0 is reserved by the sampler used in the tracer.
		unsigned int uniqueID = 1;
		uniqueID += (unsigned int)(filter) * 9;
		uniqueID += (unsigned int)(hAddr) * 3;
		uniqueID += (unsigned int)(vAddr);
		return uniqueID;
	}

	class AllocatedResource {
		protected:
			VmaAllocation allocation;
			VmaAllocator* allocator = nullptr;
			bool mapped = false;
			bool resourceInit = false;
		public:
			AllocatedResource() {}

			virtual ~AllocatedResource() {}

			AllocatedResource(AllocatedResource& alre) {
				allocation = alre.allocation;
				allocator = alre.allocator;
				mapped = alre.mapped;
				resourceInit = alre.resourceInit;
			}

			// Maps a portion of memory to the allocation
			// Returns the pointer to the first byte of the resource in memory
			virtual void* mapMemory(void** ppData) {
				assert(resourceInit && !mapped);
				vmaMapMemory(*allocator, allocation, ppData);
				mapped = true;
				return *ppData;
			}

			// Unmaps that portion of memory
			virtual void unmapMemory() {
				assert(resourceInit && mapped);
				vmaUnmapMemory(*allocator, allocation);
				mapped = false;
			}

			// Copies a portion of memory into the mapped memory
			// Returns the pointer to the first byte in memory
			virtual void* setData(void* pData, uint64_t size) {
				assert(resourceInit && !mapped);
				void* ppData = pData;		// Make a new reference that's just a pointer to a pointer to the data
				vmaMapMemory(*allocator, allocation, &ppData);
				memcpy(ppData, pData, size);
				vmaUnmapMemory(*allocator, allocation);
				return ppData;
			}

			void setAllocationName(const char* name) {
				if (resourceInit) {
					vmaSetAllocationName(*allocator, allocation, name);
				}
			}

			VmaAllocation* getAllocation() { return &allocation; }

			virtual bool isNull() const {
				return !resourceInit;
			}

			virtual void destroyResource() {
				if (mapped) {
					vmaUnmapMemory(*allocator, allocation);
				}
				allocator = nullptr;
				mapped = false;
				resourceInit = false;
			}
	};

	class AllocatedBuffer : public AllocatedResource {
		private:
			VkBuffer buffer;
			VkBufferView bufferView;
			VkDeviceSize size;
			VkDescriptorBufferInfo descriptorInfo {};
			VkFormat viewFormat = VK_FORMAT_UNDEFINED;
			bool bufferViewCreated = false;
			VkAccessFlags accessMask = VK_ACCESS_NONE;
			VkPipelineStageFlags pipelineStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			
		public:
			AllocatedBuffer() { }

			AllocatedBuffer(AllocatedBuffer& albo) {
				allocation = albo.allocation;
				allocator = albo.allocator;
				mapped = albo.mapped;
				resourceInit = albo.resourceInit;
				buffer = albo.buffer;
				bufferView = albo.bufferView;
				size = albo.size;
				descriptorInfo = albo.descriptorInfo;
				bufferViewCreated = albo.bufferViewCreated;
			}

			VkResult init(VmaAllocator* allocator, VkBufferCreateInfo& bufferInfo, VmaAllocationCreateInfo& allocCreateInfo, VmaAllocationInfo& allocInfo) {
				assert(!resourceInit);
        		VkResult res = vmaCreateBuffer(*allocator, &bufferInfo, &allocCreateInfo, &buffer, &allocation, &allocInfo);
				if (res != VK_SUCCESS) {
					return res;
				}
				this->resourceInit = true;
				this->allocator = allocator;
				this->size = bufferInfo.size;
				this->descriptorInfo = { this->buffer, 0, this->size };
				return res;
			}
			
			AllocatedBuffer(VmaAllocator* allocator, VkBufferCreateInfo& bufferInfo, VmaAllocationCreateInfo& allocCreateInfo, VmaAllocationInfo& allocInfo) {
				init(allocator, bufferInfo, allocCreateInfo, allocInfo);
			}

			~AllocatedBuffer() { 
				allocator = nullptr;
				destroyResource();
			}

			void destroyResource() {
				if (!resourceInit) {return;}
				VmaAllocatorInfo allocatorInfo;
				vmaGetAllocatorInfo(*allocator, &allocatorInfo);
				if (bufferViewCreated) {
					bufferViewCreated = false;
					vkDestroyBufferView(allocatorInfo.device, bufferView, nullptr);
				}
				if (mapped) {
					vmaUnmapMemory(*allocator, allocation);
				}
				vmaDestroyBuffer(*allocator, buffer, allocation);
				allocator = nullptr;
				mapped = false;
				resourceInit = false;
			}

			inline VkBuffer& getBuffer() {
				return buffer;
			}

			VkDescriptorBufferInfo getDescriptorInfo() {
				return descriptorInfo;
			}

			VkWriteDescriptorSet generateDescriptorWrite(int count, uint32_t dstBinding, VkDescriptorType type, VkDescriptorSet& descSet) {
				VkWriteDescriptorSet write {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
				write.descriptorCount = count;
				write.descriptorType = type;
				write.dstBinding = dstBinding;
				write.pBufferInfo = &descriptorInfo;
				write.dstSet = descSet;
				if (bufferViewCreated) {
					write.pTexelBufferView = &bufferView;
				}
				return write;
			}

			void createBufferView(VkFormat format) {
				if (resourceInit) {
					VmaAllocatorInfo allocatorInfo;
					vmaGetAllocatorInfo(*allocator, &allocatorInfo);
					if (bufferViewCreated) {
						vkDestroyBufferView(allocatorInfo.device, bufferView, nullptr);
					}
					VkBufferViewCreateInfo createInfo { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO };
					createInfo.buffer = buffer;
					createInfo.format = format;
					createInfo.range = size;
					vkCreateBufferView(allocatorInfo.device, &createInfo, nullptr, &bufferView);
					bufferViewCreated = true;
				}
			}

			void memoryBarrier(VkBufferMemoryBarrier barrier, VkPipelineStageFlags newStage, VkCommandBuffer* commandBuffer) {
				vkCmdPipelineBarrier(*commandBuffer, 
					pipelineStage, newStage, 
					0, 
					0, nullptr, 
					1, &barrier, 
					0, nullptr
				);
				pipelineStage = newStage;
				accessMask = barrier.dstAccessMask;
			}

			VkDeviceAddress getAddress() const {
				VmaAllocatorInfo allocatorInfo;
				vmaGetAllocatorInfo(*allocator, &allocatorInfo);
				VkBufferDeviceAddressInfo addrInfo { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
				addrInfo.buffer = buffer;
				return vkGetBufferDeviceAddress(allocatorInfo.device, &addrInfo);
			}

			VkAccessFlags getAccessFlags() const { return accessMask; }
			VkDeviceSize getSize() const { return size; }

			static void memoryBarrier(AllocatedBuffer* buffers, uint32_t bufferCount, std::vector<VkBufferMemoryBarrier> barriers, VkPipelineStageFlags oldStage, VkPipelineStageFlags newStage, VkCommandBuffer* commandBuffer) {
				vkCmdPipelineBarrier(*commandBuffer, 
					oldStage, newStage, 
					0, 
					0, nullptr, 
					barriers.size(), barriers.data(), 
					0, nullptr
				);

				for (int i = 0; i < bufferCount; i++) {
					buffers[i].accessMask = barriers[i].dstAccessMask;
					buffers[i].pipelineStage = newStage;
				}
			}
		};

	class AllocatedImage :  public AllocatedResource {
		private:
			VkImage image {};
			VkImageView imageView {};
			VkAccessFlags accessFlags = VK_ACCESS_NONE;
			VkPipelineStageFlags pipelineStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			VkExtent3D dimensions { 0, 0, 0 };
			VkFormat format = VK_FORMAT_UNDEFINED;
			std::vector<VkImageLayout> layouts { VK_IMAGE_LAYOUT_UNDEFINED };
			VkImageType type = VK_IMAGE_TYPE_1D;
			VkDescriptorImageInfo descriptorInfo {};
			unsigned int mipLevels;
			bool imageViewCreated = false;
			
		public:
			AllocatedImage() { }

			AllocatedImage(AllocatedImage& alime) {
				allocation = alime.allocation;
				allocator = alime.allocator;
				mapped = alime.mapped;
				resourceInit = alime.resourceInit;
				image = alime.image;
				imageView = alime.imageView;
				dimensions = alime.dimensions;
				format = alime.format;
				type = alime.type;
				descriptorInfo = alime.descriptorInfo;
				imageViewCreated = alime.imageViewCreated;
			}

			VkResult init(VmaAllocator* allocator, VkImageCreateInfo& createInfo, VmaAllocationCreateInfo& allocCreateInfo, VmaAllocationInfo& allocInfo) {
				assert(!resourceInit);
				VkResult res = vmaCreateImage(*allocator, &createInfo, &allocCreateInfo, &image, &allocation, &allocInfo);
				if (res != VK_SUCCESS) {
					return res;
				}
				this->allocator = allocator;
				this->resourceInit = true;
				this->dimensions = createInfo.extent;
				this->format = createInfo.format;
				this->type = createInfo.imageType;
				this->mipLevels = createInfo.mipLevels;
				this->layouts.resize(this->mipLevels, createInfo.initialLayout);
				
				return res;
			}

			void createImageView(VkImageViewType viewType, VkImageAspectFlags aspectFlags) {
				if (resourceInit) {
					VmaAllocatorInfo allocatorInfo;
					vmaGetAllocatorInfo(*allocator, &allocatorInfo);
					if (imageViewCreated) {
						vkDestroyImageView(allocatorInfo.device, imageView, nullptr);
					}
					imageViewCreated = true;
					VkImageViewCreateInfo viewInfo{};
					viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
					viewInfo.image = image;
					viewInfo.viewType = viewType;
					viewInfo.format = format;
					viewInfo.subresourceRange.aspectMask = aspectFlags;
					viewInfo.subresourceRange.baseMipLevel = 0;
					viewInfo.subresourceRange.levelCount = mipLevels;
					viewInfo.subresourceRange.baseArrayLayer = 0;
					viewInfo.subresourceRange.layerCount = 1;
					vkCreateImageView(allocatorInfo.device, &viewInfo, nullptr, &imageView);
					descriptorInfo = { nullptr, imageView, layouts[0] };
				}
			}

			VkImageView& getImageView() {
				assert(imageViewCreated);
				return imageView;
			}

			VkDescriptorImageInfo getDescriptorInfo() {
				assert(imageViewCreated);
				return descriptorInfo;
			}

			VkWriteDescriptorSet generateDescriptorWrite(int count, uint32_t dstBinding, VkDescriptorType type, VkDescriptorSet& descSet) {
				assert(imageViewCreated);
        		VkWriteDescriptorSet write {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
				write.descriptorCount = count;
				write.descriptorType = type;
				write.dstBinding = dstBinding;
				write.dstSet = descSet;
				write.pImageInfo = &descriptorInfo;
				return write;
			}

			void transitionLayout(VkCommandBuffer* commandBuffer, VkPipelineStageFlags sourceStage, VkPipelineStageFlags destStage, VkImageMemoryBarrier barrier) {
				vkCmdPipelineBarrier(
					*commandBuffer,
					sourceStage, destStage,
					0,
					0, nullptr,
					0, nullptr,
					1, &barrier
				);
				layouts[barrier.subresourceRange.baseMipLevel] = barrier.newLayout;
				accessFlags = barrier.dstAccessMask;
				pipelineStage = destStage;
			}
			
			AllocatedImage(VmaAllocator* allocator, VkImageCreateInfo& createInfo, VmaAllocationCreateInfo& allocCreateInfo, VmaAllocationInfo& allocInfo) {
				init(allocator, createInfo, allocCreateInfo, allocInfo);
			}

			~AllocatedImage() { 
				allocator = nullptr;
				destroyResource();
			}

			inline VkImage& getImage() {
				return image;
			}

			void destroyResource() {
				if (!resourceInit) { return; }
				VmaAllocatorInfo allocatorInfo;
				vmaGetAllocatorInfo(*allocator, &allocatorInfo);
				if (imageViewCreated) {
					imageViewCreated = false;
					vkDestroyImageView(allocatorInfo.device, imageView, nullptr);
				}
				if (mapped) {
					vmaUnmapMemory(*allocator, allocation);
				}
				vmaDestroyImage(*allocator, image, allocation);
				this->layouts.clear();
				this->format = VK_FORMAT_UNDEFINED;
				this->accessFlags = VK_ACCESS_NONE;
				this->resourceInit = false;
			}

			VkImageLayout getLayout() { return layouts[0]; }
			VkImageLayout getLayoutAtMipLevel(int mipLevel) { return layouts[mipLevel]; }
			VkAccessFlags getAccessFlags() { return accessFlags; }
			VkAccessFlags getPieplineStage() { return pipelineStage; }
			VkExtent3D getDimensions() { return dimensions; }
			unsigned int getWidth() { return dimensions.width; }
			unsigned int getHeight() { return dimensions.height; }
			unsigned int getDepth() { return dimensions.depth; }
			unsigned int getMipLevels() { return mipLevels; }
			VkFormat getFormat() const { return format; }

			static void transitionLayouts(AllocatedImage** images, uint32_t imageCount, VkCommandBuffer* commandBuffer, VkPipelineStageFlags sourceStage, VkPipelineStageFlags destStage, VkImageMemoryBarrier* barriers, VkImageLayout newLayout) {
				vkCmdPipelineBarrier(
					*commandBuffer,
					sourceStage, destStage,
					0,
					0, nullptr,
					0, nullptr,
					imageCount, barriers
				);
				for (int i = 0; i < imageCount; i++) {
					images[i]->layouts[barriers[i].subresourceRange.baseMipLevel] = newLayout;
					images[i]->accessFlags = barriers[i].dstAccessMask;
					images[i]->pipelineStage = destStage;
				}
			}
	};

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

#endif