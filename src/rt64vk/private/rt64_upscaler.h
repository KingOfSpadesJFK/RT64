/*
*   RT64VK
*/

#pragma once

#include "rt64_common.h"

namespace RT64 {
	class Device;

	class Upscaler {
		public:
			enum class QualityMode : int {
				UltraPerformance = 0,
				Performance,
				Balanced,
				Quality,
				UltraQuality,
				Native,
				Auto,
				MAX
			};

			struct UpscaleParameters {
				RT64_RECT inRect;
				AllocatedImage* inColor;
				AllocatedImage* inFlow;
				AllocatedImage* inReactiveMask;
				AllocatedImage* inLockMask;
				AllocatedImage* inDepth;
				AllocatedImage* outColor;
				float jitterX;
				float jitterY;
				float sharpness;
				float deltaTime;
				float nearPlane;
				float farPlane;
				float fovY;
				bool resetAccumulation;
			};
		public:
			virtual void set(QualityMode inQuality, int renderWidth, int renderHeight, int displayWidth, int displayHeight) = 0;
			virtual bool getQualityInformation(QualityMode quality, int displayWidth, int displayHeight, int &renderWidth, int &renderHeight) = 0;
			virtual int  getJitterPhaseCount(int renderWidth, int displayWidth) = 0;
			virtual void upscale(const UpscaleParameters &p) = 0;
			virtual bool isInitialized() const = 0;
			virtual bool requiresNonShaderResourceInputs() const = 0;

			static QualityMode getQualityAuto(int displayWidth, int displayHeight);
		};
};