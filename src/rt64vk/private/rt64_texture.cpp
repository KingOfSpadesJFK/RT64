/*
*   RT64VK
*/

#ifndef RT64_MINIMAL

#include "rt64_texture.h"

namespace RT64 {

    Texture::Texture(Device* device) {
        this->device = device;
        format = VK_FORMAT_UNDEFINED;
        currentIndex = -1;

		device->addTexture(this);
    }

    Texture::~Texture() {
        texture.destroyResource();

		device->removeTexture(this);
    }

    void Texture::setRawWithFormat(
        VkFormat format, 
        void* pixels, 
        int byteCount, 
        int width, int height, 
        int rowPitch, 
        bool generateMipmaps)
    {
        assert(pixels);
        this->format = format;
        Mipmaps* mipmaps = device->getMipmaps();
        if (mipmaps == nullptr) {
            generateMipmaps = false;
        }

        uint32_t mipLevels = generateMipmaps ? (static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1) : 1;

        AllocatedBuffer stagingTexture;

        // Stage the upload
        VkDeviceSize imageSize = width * height * 4;
        VK_CHECK(device->allocateBuffer(imageSize, 
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
            VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT,
            &stagingTexture
        ));

        // Set the data
        stagingTexture.setData(pixels, imageSize);

        // Describe the real image
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = mipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.flags = 0;
        // Allocate the real image
        device->allocateImage(
            &texture, imageInfo,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT
        );

        // Copy the command the buffer into the image
        VkCommandBuffer* commandBuffer = nullptr;
        commandBuffer = device->beginSingleTimeCommands(commandBuffer);
        device->transitionImageLayout(texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
            VK_ACCESS_TRANSFER_WRITE_BIT, 
            VK_PIPELINE_STAGE_TRANSFER_BIT, 
            commandBuffer);
        device->copyBufferToImage(stagingTexture.getBuffer(), texture.getImage(), static_cast<uint32_t>(width), static_cast<uint32_t>(height), commandBuffer);

        if (generateMipmaps) {
            mipmaps->generate(texture, commandBuffer);
            device->endSingleTimeCommands(commandBuffer);
        } else {
            device->transitionImageLayout(texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
            VK_ACCESS_SHADER_READ_BIT, 
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 
            commandBuffer);
            device->endSingleTimeCommands(commandBuffer);
        }
        
        // Destroy the staged resource
        stagingTexture.destroyResource();
        this->width = width;
        this->height = height;

        // Create an image view and sampler
        texture.createImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);

        // VkPhysicalDeviceProperties properties{};
        // vkGetPhysicalDeviceProperties(device->getPhysicalDevice(), &properties);
    }

    void Texture::setRGBA8(void* bytes, int byteCount, int width, int height, int rowPitch, bool generateMipmaps) {
        setRawWithFormat(VK_FORMAT_R8G8B8A8_UNORM, bytes, byteCount, width, height, rowPitch, generateMipmaps);
    }

    // Public

    AllocatedImage& Texture::getTexture() { return texture; };
    VkImageView& Texture::getTextureImageView() { return texture.getImageView(); };
    VkFormat Texture::getFormat() const { return texture.getFormat(); }
    void Texture::setCurrentIndex(int v) { currentIndex = v; }
    int Texture::getCurrentIndex() const { return currentIndex; }
    int Texture::getWidth() { return width; }
    int Texture::getHeight() { return height; }
    void Texture::setName(const char* newName) { name = newName; }
};

// Library exports

DLEXPORT RT64_TEXTURE* RT64_CreateTexture(RT64_DEVICE *devicePtr, RT64_TEXTURE_DESC textureDesc) {
	assert(devicePtr != nullptr);
	RT64::Device* device = (RT64::Device *)(devicePtr);
	RT64::Texture* texture = new RT64::Texture(device);

	// Try to load the texture data.
	try {
		switch (textureDesc.format) {
            case RT64_TEXTURE_FORMAT_RGBA8:
                texture->setRGBA8(textureDesc.bytes, textureDesc.byteCount, textureDesc.width, textureDesc.height, textureDesc.rowPitch, true);
                break;
            // case RT64_TEXTURE_FORMAT_DDS:
            // 	texture->setDDS(textureDesc.bytes, textureDesc.byteCount);
            // 	break;
		}

		return (RT64_TEXTURE*)(texture);
	}
	RT64_CATCH_EXCEPTION();

	delete texture;
	return nullptr;
}

DLEXPORT void RT64_DestroyTexture(RT64_TEXTURE* texturePtr) {
	delete (RT64::Texture*)(texturePtr);
}

#endif