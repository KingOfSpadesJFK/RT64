/*
*   RT64VK
*/

#pragma once

#include "rt64_common.h"

#include "rt64_device.h"

namespace RT64 {
    class Texture {
        private:
            Device* device;
            AllocatedImage texture;
            VkImageView textureImageView;
            VkFormat format;
		    int currentIndex;
            int width, height;

		    void setRawWithFormat(VkFormat format, void* pixels, int byteCount, int width, int height, int rowPitch, bool generateMipmaps);
        public:
            Texture(Device* device);
            virtual ~Texture();
            void setRGBA8(void* bytes, int byteCount, int width, int height, int rowPitch, bool generateMipmaps);
            // void setDDS(const void* bytes, int byteCount);
            AllocatedImage& getTexture();
            VkImageView& getTextureImageView();
            VkFormat getFormat() const;
            void setCurrentIndex(int v);
            int getCurrentIndex() const;
            int getWidth();
            int getHeight();
    };
};