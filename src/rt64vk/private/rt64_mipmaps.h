/*
*  RT64VK
*/

#pragma once

#ifndef RT64_MINIMAL

#include "rt64_common.h"

namespace RT64 {
	class Device;

	class Mipmaps {
		private:
			Device* device;
		public:
			Mipmaps(Device* device);
			void generate(AllocatedImage& sourceTexture, VkCommandBuffer* commandBuffer = nullptr);
	};
};

#endif