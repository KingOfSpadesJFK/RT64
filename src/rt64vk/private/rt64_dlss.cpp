/*
*  RT64VK
*/

#include "rt64_dlss.h"

#include <DLSS/include/nvsdk_ngx_helpers.h>
#include <DLSS/include/nvsdk_ngx_helpers_vk.h>

#include "rt64_common.h"
#include "rt64_device.h"

// TODO: Get a unique app id
#define APP_ID 0x4D4152494F3634

namespace RT64
{
    NVSDK_NGX_Resource_VK convertToNGXResource(AllocatedBuffer& in) {
        NVSDK_NGX_BufferInfo_VK buffer {
            in.getBuffer(),
            (unsigned int)in.getSize()
        };

        NVSDK_NGX_Resource_VK resource { };
        resource.Resource.BufferInfo = buffer;
        resource.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_BUFFER;
        resource.ReadWrite = true;
        return resource;
    }

    NVSDK_NGX_Resource_VK convertToNGXResource(AllocatedImage& in) {
        VkImageSubresourceRange subresourceRange {};
        subresourceRange.aspectMask = in.getAccessFlags();
        subresourceRange.levelCount = in.getMipLevels();
        subresourceRange.layerCount = 1;

        NVSDK_NGX_ImageViewInfo_VK imageView;
        imageView.Format = in.getFormat();
        imageView.Width = in.getDimensions().width;
        imageView.Height = in.getDimensions().height;
        imageView.Image = in.getImage();
        imageView.ImageView = in.getImageView();
        imageView.SubresourceRange = subresourceRange;

        NVSDK_NGX_Resource_VK resource { };
        resource.Resource.ImageViewInfo = imageView;
        resource.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
        resource.ReadWrite = true;
        return resource;
    }

    class DLSS::Context {
        private:
            Device* device = nullptr;
            NVSDK_NGX_Parameter* ngxParameters = nullptr;
            NVSDK_NGX_Handle* dlssFeature = nullptr;
            bool initialized = false;
        public:
            Context(Device* device) {
                assert(device != nullptr);
                this->device = device;

                NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_Init(APP_ID, L"./", 
                    device->getVkInstance(), 
                    device->getPhysicalDevice(), 
                    device->getVkDevice());

                if (NVSDK_NGX_FAILED(result)) {
                    RT64_LOG_PRINTF("NVSDK_NGX_VULKAN_Init failed: %ls\n", GetNGXResultAsString(result));
                    return;
                }

                result = NVSDK_NGX_VULKAN_GetCapabilityParameters(&ngxParameters);
                if (NVSDK_NGX_FAILED(result)) {
                    RT64_LOG_PRINTF("NVSDK_NGX_GetCapabilityParameters failed: %ls\n", GetNGXResultAsString(result));
                    return;
                }

                // Check for driver version.
                int needsUpdatedDriver = 0;
                unsigned int minDriverVersionMajor = 0;
                unsigned int minDriverVersionMinor = 0;
                NVSDK_NGX_Result ResultUpdatedDriver = ngxParameters->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsUpdatedDriver);
                NVSDK_NGX_Result ResultMinDriverVersionMajor = ngxParameters->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &minDriverVersionMajor);
                NVSDK_NGX_Result ResultMinDriverVersionMinor = ngxParameters->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &minDriverVersionMinor);
                if (ResultUpdatedDriver == NVSDK_NGX_Result_Success && ResultMinDriverVersionMajor == NVSDK_NGX_Result_Success && ResultMinDriverVersionMinor == NVSDK_NGX_Result_Success) {
                    if (needsUpdatedDriver) {
                        RT64_LOG_PRINTF("NVIDIA DLSS cannot be loaded due to outdated driver. Minimum Driver Version required : %u.%u", minDriverVersionMajor, minDriverVersionMinor);
                        return;
                    }
                    else {
                        RT64_LOG_PRINTF("NVIDIA DLSS Minimum driver version was reported as: %u.%u", minDriverVersionMajor, minDriverVersionMinor);
                    }
                }
                else {
                    RT64_LOG_PRINTF("NVIDIA DLSS Minimum driver version was not reported.");
                }

                // Check if DLSS is available.
                int dlssAvailable = 0;
                NVSDK_NGX_Result ResultDLSS = ngxParameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &dlssAvailable);
                if (ResultDLSS != NVSDK_NGX_Result_Success || !dlssAvailable) {
                    NVSDK_NGX_Result FeatureInitResult = NVSDK_NGX_Result_Fail;
                    NVSDK_NGX_Parameter_GetI(ngxParameters, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, (int *)&FeatureInitResult);
                    RT64_LOG_PRINTF("NVIDIA DLSS not available on this hardware/platform., FeatureInitResult = 0x%08x, info: %ls", FeatureInitResult, GetNGXResultAsString(FeatureInitResult));
                    return;
                }

                initialized = true;
            }

            ~Context() {
                release();
                NVSDK_NGX_VULKAN_Shutdown1(device->getVkDevice());
            }

            NVSDK_NGX_PerfQuality_Value toNGXQuality(QualityMode q) {
                switch (q) {
                case QualityMode::UltraPerformance:
                    return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
                case QualityMode::Performance:
                    return NVSDK_NGX_PerfQuality_Value_MaxPerf;
                case QualityMode::Balanced:
                    return NVSDK_NGX_PerfQuality_Value_Balanced;
                case QualityMode::Quality:
                case QualityMode::UltraQuality:
                    return NVSDK_NGX_PerfQuality_Value_MaxQuality;
                case QualityMode::Native:
                    return NVSDK_NGX_PerfQuality_Value_UltraQuality;
                default:
                    return NVSDK_NGX_PerfQuality_Value_Balanced;
                }
            }

            bool set(QualityMode quality, int renderWidth, int renderHeight, int displayWidth, int displayHeight) {
                if (quality == QualityMode::Auto) {
                    quality = getQualityAuto(displayWidth, displayHeight);
                }

                release();

                unsigned int CreationNodeMask = 1;
                unsigned int VisibilityNodeMask = 1;
                NVSDK_NGX_Result ResultDLSS = NVSDK_NGX_Result_Fail;
                int DlssCreateFeatureFlags = NVSDK_NGX_DLSS_Feature_Flags_None;
                DlssCreateFeatureFlags |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
                DlssCreateFeatureFlags |= NVSDK_NGX_DLSS_Feature_Flags_DoSharpening;

                NVSDK_NGX_DLSS_Create_Params DlssCreateParams;
                memset(&DlssCreateParams, 0, sizeof(DlssCreateParams));
                DlssCreateParams.Feature.InWidth = renderWidth;
                DlssCreateParams.Feature.InHeight = renderHeight;
                DlssCreateParams.Feature.InTargetWidth = displayWidth;
                DlssCreateParams.Feature.InTargetHeight = displayHeight;
                DlssCreateParams.Feature.InPerfQualityValue = toNGXQuality(quality);
                DlssCreateParams.InFeatureCreateFlags = DlssCreateFeatureFlags;

                VkCommandBuffer* commandBuffer = device->beginSingleTimeCommands();
                ResultDLSS =  NGX_VULKAN_CREATE_DLSS_EXT(*commandBuffer, CreationNodeMask, VisibilityNodeMask, &dlssFeature, ngxParameters, &DlssCreateParams);

                device->waitForGPU();

                if (NVSDK_NGX_FAILED(ResultDLSS)) {
                    RT64_LOG_PRINTF("Failed to create DLSS Features = 0x%08x, info: %ls", ResultDLSS, GetNGXResultAsString(ResultDLSS));
                    return false;
                }

                return true;
            }

            void release() {
                device->waitForGPU();

                // Try to release the DLSS feature.
                NVSDK_NGX_Result ResultDLSS = (dlssFeature != nullptr) ? NVSDK_NGX_VULKAN_ReleaseFeature(dlssFeature) : NVSDK_NGX_Result_Success;
                if (NVSDK_NGX_FAILED(ResultDLSS)) {
                    RT64_LOG_PRINTF("Failed to NVSDK_NGX_D3D12_ReleaseFeature, code = 0x%08x, info: %ls", ResultDLSS, GetNGXResultAsString(ResultDLSS));
                }

                dlssFeature = nullptr;
            }

            bool getQualityInformation(QualityMode quality, int displayWidth, int displayHeight, int &renderWidth, int &renderHeight) {
                if (quality == QualityMode::Auto) {
                    quality = getQualityAuto(displayWidth, displayHeight);
                }
                // DLSS doesn't provide settings for this quality setting, so we force it instead.
                if (quality == QualityMode::UltraQuality) {
                    renderWidth = (displayWidth * 77) / 100;
                    renderHeight = (displayHeight * 77) / 100;
                }
                    unsigned int renderOptimalWidth = 0, renderOptimalHeight = 0;
                    unsigned int renderMaxWidth = 0, renderMaxHeight = 0;
                    unsigned int renderMinWidth = 0, renderMinHeight = 0;
                    float renderSharpness;
                    NVSDK_NGX_Result Result = NGX_DLSS_GET_OPTIMAL_SETTINGS(
                        ngxParameters,
                        displayWidth, displayHeight,
                        toNGXQuality(quality),
                        &renderOptimalWidth, &renderOptimalHeight,
                        &renderMaxWidth, &renderMaxHeight,
                        &renderMinWidth, &renderMinHeight,
                        &renderSharpness);

                    // Failed to retrieve the optimal settings.
                    if (NVSDK_NGX_FAILED(Result)) {
                        RT64_LOG_PRINTF("Querying Optimal Settings failed! code = 0x%08x, info: %ls", Result, GetNGXResultAsString(Result));
                        return false;
                    }
                    // Quality mode isn't allowed.
                    else if ((renderOptimalWidth == 0) || (renderOptimalHeight == 0)) {
                        return false;
                    }
                    else {
                        renderWidth = renderOptimalWidth;
                        renderHeight = renderOptimalHeight;
                        return true;
                    }
            }

            int getJitterPhaseCount(int renderWidth, int displayWidth) {
                return 64;
            }

            void upscale(const UpscaleParameters& p) {
                assert(dlssFeature != nullptr);
                int Reset = p.resetAccumulation ? 1 : 0;
                NVSDK_NGX_Coordinates renderingOffset = { (unsigned int)(p.inRect.x), (unsigned int)(p.inRect.y) };
                NVSDK_NGX_Dimensions  renderingSize = { (unsigned int)(p.inRect.w), (unsigned int)(p.inRect.h) };

                NVSDK_NGX_Result Result;
                NVSDK_NGX_VK_DLSS_Eval_Params VKDlssEvalParams;
                memset(&VKDlssEvalParams, 0, sizeof(VKDlssEvalParams));
                
                NVSDK_NGX_Resource_VK inColor = convertToNGXResource(*p.inColor);
                NVSDK_NGX_Resource_VK outColor = convertToNGXResource(*p.outColor);
                NVSDK_NGX_Resource_VK inDepth = convertToNGXResource(*p.inDepth);
                NVSDK_NGX_Resource_VK inFlow = convertToNGXResource(*p.inFlow);
                NVSDK_NGX_Resource_VK inLockMask = convertToNGXResource(*p.inLockMask);

                VKDlssEvalParams.Feature.pInColor = &inColor;
                VKDlssEvalParams.Feature.pInOutput = &outColor;
                VKDlssEvalParams.pInDepth = &inDepth;
                VKDlssEvalParams.pInMotionVectors = &inFlow;
                VKDlssEvalParams.pInExposureTexture = nullptr;
                VKDlssEvalParams.pInBiasCurrentColorMask = &inLockMask;
                VKDlssEvalParams.InJitterOffsetX = p.jitterX;
                VKDlssEvalParams.InJitterOffsetY = p.jitterY;
                VKDlssEvalParams.Feature.InSharpness = p.sharpness;
                VKDlssEvalParams.InReset = Reset;
                VKDlssEvalParams.InMVScaleX = 1.0f;
                VKDlssEvalParams.InMVScaleY = 1.0f;
                VKDlssEvalParams.InColorSubrectBase = renderingOffset;
                VKDlssEvalParams.InDepthSubrectBase = renderingOffset;
                VKDlssEvalParams.InTranslucencySubrectBase = renderingOffset;
                VKDlssEvalParams.InMVSubrectBase = renderingOffset;
                VKDlssEvalParams.InRenderSubrectDimensions = renderingSize;

                VkCommandBuffer* commandBuffer = device->beginSingleTimeCommands();
                Result = NGX_VULKAN_EVALUATE_DLSS_EXT(*commandBuffer, dlssFeature, ngxParameters, &VKDlssEvalParams);

                if (NVSDK_NGX_FAILED(Result)) {
                    RT64_LOG_PRINTF("Failed to NVSDK_NGX_VULKAN_EvaluateFeature for DLSS, code = 0x%08x, info: %ls", Result, GetNGXResultAsString(Result));
                }
            }

            bool isInitialized() const {
                return initialized;
            }
    };

    DLSS::~DLSS() {
        delete ctx;
    }

    void DLSS::set(QualityMode inQuality, int renderWidth, int renderHeight, int displayWidth, int displayHeight) {
        ctx->set(inQuality, renderWidth, renderHeight, displayWidth, displayHeight);
    }

    bool DLSS::getQualityInformation(QualityMode quality, int displayWidth, int displayHeight, int &renderWidth, int &renderHeight) {
        return ctx->getQualityInformation(quality, displayWidth, displayHeight, renderWidth, renderHeight);
    }

    int DLSS::getJitterPhaseCount(int renderWidth, int displayWidth) {
        return ctx->getJitterPhaseCount(renderWidth, displayWidth);
    }

    void DLSS::upscale(const UpscaleParameters &p) {
        ctx->upscale(p);
    }

    bool DLSS::isInitialized() const {
        return ctx->isInitialized();
    }

    bool DLSS::requiresNonShaderResourceInputs() const {
        return false;
    }
};