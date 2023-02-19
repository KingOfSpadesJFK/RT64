//
// RT64
//

#ifndef RT64_MINIMAL

#include "rt64_fsr.h"

#include "fsr/include/ffx_fsr2.h"
#include "fsr/include/vk/ffx_fsr2_vk.h"

#include "rt64_device.h"

// FSR::Context

class RT64::FSR::Context {
private:
    Device *device;
    bool initialized;
    FfxFsr2Context fsrContext;
    bool fsrFilled;
    FfxFsr2ContextDescription fsrDesc;
    std::vector<uint8_t> fsrScratchVk;
public:
    Context(Device *device) {
        assert(device != nullptr);

        this->device = device;
        initialized = false;
        fsrFilled = false;

        size_t scratchSize = ffxFsr2GetScratchMemorySizeVK(device->getPhysicalDevice());
        fsrScratchVk.resize(scratchSize);
        FfxErrorCode fsrRes = ffxFsr2GetInterfaceVK(&fsrDesc.callbacks, fsrScratchVk.data(), scratchSize, device->getPhysicalDevice(), vkGetDeviceProcAddr);
        if (fsrRes != FFX_OK) {
            RT64_LOG_PRINTF("ffxFsr2GetInterfaceVK failed: %d\n", fsrRes);
            return;
        }

        initialized = true;
    }

    ~Context() {
        release();
    }

    FfxFsr2QualityMode toFSRQuality(QualityMode q) {
        switch (q) {
        case QualityMode::UltraPerformance:
            return FFX_FSR2_QUALITY_MODE_ULTRA_PERFORMANCE;
        case QualityMode::Performance:
            return FFX_FSR2_QUALITY_MODE_PERFORMANCE;
        case QualityMode::Balanced:
            return FFX_FSR2_QUALITY_MODE_BALANCED;
        case QualityMode::Quality:
        case QualityMode::UltraQuality:
        case QualityMode::Native:
            return FFX_FSR2_QUALITY_MODE_QUALITY;
        default:
            return FFX_FSR2_QUALITY_MODE_BALANCED;
        }
    }

    bool set(QualityMode quality, int renderWidth, int renderHeight, int displayWidth, int displayHeight) {
        if (quality == QualityMode::Auto) {
            quality = getQualityAuto(displayWidth, displayHeight);
        }

        release();

        fsrDesc.flags = FFX_FSR2_ENABLE_AUTO_EXPOSURE;
        fsrDesc.maxRenderSize.width = renderWidth;
        fsrDesc.maxRenderSize.height = renderHeight;
        fsrDesc.displaySize.width = displayWidth;
        fsrDesc.displaySize.height = displayHeight;
        fsrDesc.device = ffxGetDeviceVK(device->getVkDevice());

        FfxErrorCode fsrRes = ffxFsr2ContextCreate(&fsrContext, &fsrDesc);
        if (fsrRes != FFX_OK) {
            RT64_LOG_PRINTF("ffxFsr2ContextCreate failed: %d\n", fsrRes);
            return false;
        }

        fsrFilled = true;

        return true;
    }

    void release() {
        device->waitForGPU();

        if (fsrFilled) {
            ffxFsr2ContextDestroy(&fsrContext);
            fsrFilled = false;
        }
    }

    bool getQualityInformation(QualityMode quality, int displayWidth, int displayHeight, int &renderWidth, int &renderHeight) {
        if (quality == QualityMode::Auto) {
            quality = getQualityAuto(displayWidth, displayHeight);
        }

        // FSR doesn't provide these quality settings, so we force them instead.
        if (quality == QualityMode::Native) {
            renderWidth = displayWidth;
            renderHeight = displayHeight;
        }
        else if (quality == QualityMode::UltraQuality) {
            renderWidth = (displayWidth * 77) / 100;
            renderHeight = (displayHeight * 77) / 100;
        }
        else {
            uint32_t fsrRenderWidth = 0;
            uint32_t fsrRenderHeight = 0;
            FfxErrorCode fsrRes = ffxFsr2GetRenderResolutionFromQualityMode(&fsrRenderWidth, &fsrRenderHeight, displayWidth, displayHeight, toFSRQuality(quality));
            if (fsrRes != FFX_OK) {
                RT64_LOG_PRINTF("ffxFsr2GetRenderResolutionFromQualityMode failed: %d\n", fsrRes);
                return false;
            }

            renderWidth = fsrRenderWidth;
            renderHeight = fsrRenderHeight;
        }

        return true;
    }

    uint32_t getJitterPhaseCount(int renderWidth, int displayWidth) {
        return ffxFsr2GetJitterPhaseCount(renderWidth, displayWidth);
    }

    void upscale(const UpscaleParameters &p) {
        FfxFsr2DispatchDescription dispatchDesc {};
        dispatchDesc.commandList = ffxGetCommandListVK(device->getCurrentCommandBuffer());
        dispatchDesc.color = ffxGetTextureResourceVK(&fsrContext, p.inColor->getImage(), p.inColor->getImageView(), p.inColor->getDimensions().width, p.inColor->getDimensions().height, p.inColor->getFormat(), L"inColor");
        dispatchDesc.depth = ffxGetTextureResourceVK(&fsrContext, p.inDepth->getImage(), p.inDepth->getImageView(), p.inDepth->getDimensions().width, p.inDepth->getDimensions().height, p.inDepth->getFormat(), L"inDepth");
        dispatchDesc.motionVectors = ffxGetTextureResourceVK(&fsrContext, p.inFlow->getImage(), p.inFlow->getImageView(), p.inFlow->getDimensions().width, p.inFlow->getDimensions().height, p.inFlow->getFormat(), L"inFlow");
        dispatchDesc.exposure = ffxGetTextureResourceVK(&fsrContext, nullptr, nullptr, 1, 1, VK_FORMAT_R16_SFLOAT, L"inExposure");
        dispatchDesc.reactive = ffxGetTextureResourceVK(&fsrContext, p.inReactiveMask->getImage(), p.inReactiveMask->getImageView(), p.inReactiveMask->getDimensions().width, p.inReactiveMask->getDimensions().height, p.inReactiveMask->getFormat(), L"inReactive");
        dispatchDesc.transparencyAndComposition = ffxGetTextureResourceVK(&fsrContext, p.inLockMask->getImage(), p.inLockMask->getImageView(), p.inLockMask->getDimensions().width, p.inLockMask->getDimensions().height, p.inLockMask->getFormat(), L"inTransparencyAndComposition");
        dispatchDesc.output = ffxGetTextureResourceVK(&fsrContext, p.outColor->getImage(), p.outColor->getImageView(), p.outColor->getDimensions().width, p.outColor->getDimensions().height, p.outColor->getFormat(), L"outColor");
        dispatchDesc.jitterOffset.x = p.jitterX;
        dispatchDesc.jitterOffset.y = p.jitterY;
        dispatchDesc.motionVectorScale = { 1.0f, 1.0f };
        dispatchDesc.reset = p.resetAccumulation;
        dispatchDesc.renderSize.width = p.inRect.w;
        dispatchDesc.renderSize.height = p.inRect.h;
        dispatchDesc.enableSharpening = (p.sharpness > 0.0f);
        dispatchDesc.sharpness = p.sharpness;
        dispatchDesc.frameTimeDelta = p.deltaTime;
        dispatchDesc.preExposure = 1.0f;
        dispatchDesc.cameraNear = p.nearPlane;
        dispatchDesc.cameraFar = p.farPlane;
        dispatchDesc.cameraFovAngleVertical = p.fovY;

        FfxErrorCode fsrRes = ffxFsr2ContextDispatch(&fsrContext, &dispatchDesc);
        if (fsrRes != FFX_OK) {
            RT64_LOG_PRINTF("ffxFsr2ContextDispatch failed: %d\n", fsrRes);
        }
    }

    bool isInitialized() const {
        return initialized;
    }
};

// FSR

RT64::FSR::FSR(Device *device) {
    ctx = new Context(device);
}

RT64::FSR::~FSR() {
    delete ctx;
}

void RT64::FSR::set(QualityMode inQuality, int renderWidth, int renderHeight, int displayWidth, int displayHeight) {
    ctx->set(inQuality, renderWidth, renderHeight, displayWidth, displayHeight);
}

bool RT64::FSR::getQualityInformation(QualityMode quality, int displayWidth, int displayHeight, int &renderWidth, int &renderHeight) {
    return ctx->getQualityInformation(quality, displayWidth, displayHeight, renderWidth, renderHeight);
}

int RT64::FSR::getJitterPhaseCount(int renderWidth, int displayWidth) {
    return ctx->getJitterPhaseCount(renderWidth, displayWidth);
}

void RT64::FSR::upscale(const UpscaleParameters &p) {
    ctx->upscale(p);
}

bool RT64::FSR::isInitialized() const {
    return ctx->isInitialized();
}

bool RT64::FSR::requiresNonShaderResourceInputs() const {
    return false;
}

#endif