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
    }

    Texture::~Texture() {
        texture.destroyResource();
        vkDestroyImageView(device->getVkDevice(), textureImageView, nullptr);
        vkDestroySampler(device->getVkDevice(), textureSampler, nullptr);
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
        // Mipmaps go here
        
        // uint32_t rowWidth, rowPadding;
        // CalculateTextureRowWidthPadding(rowPitch, rowWidth, rowPadding);

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

        // Allocate the real image
        VK_CHECK(device->allocateImage(
            width, height,
            VK_IMAGE_TYPE_2D,
            format,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT,
            &texture
        ));

        // Copy the command the buffer into the image
        VkCommandBuffer* commandBuffer = nullptr;
        commandBuffer = device->beginSingleTimeCommands(commandBuffer);
        device->transitionImageLayout(*texture.getImage(), format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, nullptr);
        device->copyBufferToImage(*stagingTexture.getBuffer(), *texture.getImage(), static_cast<uint32_t>(width), static_cast<uint32_t>(height), nullptr);
        device->transitionImageLayout(*texture.getImage(), format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, nullptr);
        device->endSingleTimeCommands(commandBuffer);
        
        // Destroy the staged resource
        stagingTexture.destroyResource();
        this->width = width;
        this->height = height;

        // Create an image view and sampler
        textureImageView = device->createImageView(*texture.getImage(), VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device->getPhysicalDevice(), &properties);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        vkCreateSampler(device->getVkDevice(), &samplerInfo, nullptr, &textureSampler);
    }

    void Texture::setRGBA8(void* bytes, int byteCount, int width, int height, int rowPitch, bool generateMipmaps) {
        setRawWithFormat(VK_FORMAT_R8G8B8A8_SRGB, bytes, byteCount, width, height, rowPitch, generateMipmaps);
    }

    // Public

    AllocatedImage* Texture::getTexture() { return &texture; };
    VkImageView* Texture::getTextureImageView() { return &textureImageView; };
    VkSampler* Texture::getTextureSampler() { return &textureSampler; };
    void Texture::setCurrentIndex(int v) { currentIndex = v; }
    int Texture::getCurrentIndex() const { return currentIndex; }
    int Texture::getWidth() { return width; }
    int Texture::getHeight() { return height; }
};

// Library exports

DLEXPORT RT64_TEXTURE *RT64_CreateTexture(RT64_DEVICE *devicePtr, RT64_TEXTURE_DESC textureDesc) {
	assert(devicePtr != nullptr);
	RT64::Device *device = (RT64::Device *)(devicePtr);
	RT64::Texture *texture = new RT64::Texture(device);

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

		return (RT64_TEXTURE *)(texture);
	}
	RT64_CATCH_EXCEPTION();

	delete texture;
	return nullptr;
}

DLEXPORT void RT64_DestroyTexture(RT64_TEXTURE *texturePtr) {
	delete (RT64::Texture *)(texturePtr);
}

#endif