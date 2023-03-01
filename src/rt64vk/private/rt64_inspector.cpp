//
// RT64
//

#ifndef RT64_MINIMAL

#include "rt64_inspector.h"

#include "rt64_device.h"
#include "rt64_scene.h"
#include "rt64_view.h"

#include "../contrib/im3d/im3d.h"
#include "../contrib/im3d/im3d_math.h"

#include "../contrib/imgui/imgui.h"
#include "../contrib/imgui/backends/imgui_impl_glfw.h"
#include "../contrib/imgui/backends/imgui_impl_vulkan.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>

std::string dateAsFilename() {
    std::time_t time = std::time(nullptr);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%F_%T");
    std::string s = ss.str();
    std::replace(s.begin(), s.end(), ':', '-');
    return s;
}

namespace RT64 {
	Inspector::Inspector() {}

	bool Inspector::init(Device* device) {
		if (initialized) { return true; }
		this->device = device;
		prevCursorX = prevCursorY = 0;
		cameraControl = false;
		invertCameraX = false;
		invertCameraY = false;
		cameraPanX = 0.0f;
		cameraPanY = 0.0f;
		cameraRoll = 0.0f;
		cameraPanSpeed = 1.0f;
		dumpFrameCount = 0;
		sceneDesc = nullptr;
		material = nullptr;
		lights = nullptr;
		lightCount = 0;
		maxLightCount = 0;

		// Im3D
		Im3d::AppData &appData = Im3d::GetAppData();

		// ImGui
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		ImGui::StyleColorsDark();

		// Create descriptor pool for ImGui
		//  This is just copied from the imgui demo lmao
		VkDescriptorPoolSize pool_sizes[] =
		{
			{ VK_DESCRIPTOR_TYPE_SAMPLER, 10 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10 },
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 10 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 10 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 10 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 10 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 10 },
			{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 10 }
		};

		VkDescriptorPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		pool_info.maxSets = 10;
		pool_info.poolSizeCount = std::size(pool_sizes);
		pool_info.pPoolSizes = pool_sizes;
		VK_CHECK(vkCreateDescriptorPool(device->getVkDevice(), &pool_info, nullptr, &descPool));

		ImGui_ImplVulkan_InitInfo initInfo = device->generateImguiInitInfo();
        initInfo.DescriptorPool = descPool;
		ImGui_ImplGlfw_InitForVulkan(device->getWindow(), true);
		ImGui_ImplVulkan_Init(&initInfo, device->getPresentRenderPass());

		ImGui_ImplVulkan_CreateDeviceObjects();
		VkCommandBuffer* cmd = device->beginSingleTimeCommands();
		ImGui_ImplVulkan_CreateFontsTexture(*cmd);
		device->endSingleTimeCommands(cmd);
		ImGui_ImplVulkan_DestroyFontUploadObjects();
		initialized = true;
		return false;
	}

	Inspector::~Inspector() {
		if (initialized) {
			destroy();
		}
	}

	void Inspector::destroy() {
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
		vkDestroyDescriptorPool(device->getVkDevice(), descPool, nullptr);
		initialized = false;
	}

	void Inspector::render(View *activeView, long cursorX, long cursorY) {
		setupWithView(activeView, cursorX, cursorY);
		
		// Start the frame.
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		Im3d::NewFrame();

		ImGui::Begin("RT64 Inspector");
		if (ImGui::BeginTabBar("##tab")) {
			renderViewParams(activeView);
			renderSceneInspector();
			renderPostInspector(activeView);
			renderMaterialInspector();
			renderLightInspector();
			renderCameraControl();
			ImGui::EndTabBar();
		}
		renderPrint();
		ImGui::End();

		Im3d::EndFrame();

		// TODO: Implement dump target
		// If dumping frames is active, save the current state of the RTV into a file.
		// if (!dumpPath.empty()) {
		// 	const int LeadingZeroes = 8;
		// 	std::ostringstream oss;
		// 	oss << dumpPath << "/" << std::setw(LeadingZeroes) << std::setfill('0') << dumpFrameCount++ << ".bmp";
		// 	// device->dumpRenderTarget(oss.str());
		// }

		// TODO: Finish implementing this
		// activeView->renderInspector(this);

		// Get the current command buffer and then render inspector
		ImGui::Render();
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), device->getCurrentCommandBuffer());
	}

	void Inspector::resize() {
		ImGui_ImplVulkan_CreateDeviceObjects();
	}

	void Inspector::renderViewParams(View *view) {
		assert(view != nullptr);

		if (ImGui::BeginTabItem("View"))
		{
			int diSamples = view->getDISamples();
			int giSamples = view->getGISamples();
			int giBounces = view->getGIBounces();
			int maxLights = view->getMaxLights();
			int anisotropicLevels = device->getAnisotropyLevel();
			int maxReflections = view->getMaxReflections();
			float motionBlurStrength = view->getMotionBlurStrength();
			int motionBlurSamples = view->getMotionBlurSamples();
			int visualizationMode = view->getVisualizationMode();
			int resScale = lround(view->getResolutionScale() * 100.0f);
			int upscaleMode = (int)(view->getUpscaleMode());
			bool denoiser = view->getDenoiserEnabled();

			ImGui::DragInt("DI samples", &diSamples, 0.1f, 0, 32);
			ImGui::DragInt("GI samples", &giSamples, 0.1f, 0, 32);
			ImGui::DragInt("GI bounces", &giBounces, 0.1f, 0, 32);
			ImGui::DragInt("Anisotropic Filtering", &anisotropicLevels, 0.1f, 1, 16);
			ImGui::DragInt("Max lights", &maxLights, 0.1f, 0, 16);
			ImGui::DragInt("Max reflections", &maxReflections, 0.1f, 0, 32);
			ImGui::DragFloat("Motion blur strength", &motionBlurStrength, 0.1f, 0.0f, 10.0f);
			ImGui::DragInt("Motion blur samples", &motionBlurSamples, 0.1f, 0, 256);
			ImGui::Combo("Visualization Mode", &visualizationMode, "Final\0Shading position\0Shading normal\0Shading specular\0"
				"Color\0Instance ID\0Direct light raw\0Direct light filtered\0Indirect light raw\0Indirect light filtered\0"
				"Reflection\0Refraction\0Transparent\0Motion vectors\0Reactive mask\0Lock mask\0Depth\0");

#ifdef _WIN32
			ImGui::Combo("Upscale Mode", &upscaleMode, "Bilinear\0AMD FSR 2\0NVIDIA DLSS\0"/* Intel XeSS\0" */);
#else
			ImGui::Combo("Upscale Mode", &upscaleMode, "Bilinear\0AMD FSR 2\0"/* NVIDIA DLSS\0Intel XeSS\0" */);
#endif

			const UpscaleMode eUpscaleMode = static_cast<UpscaleMode>(upscaleMode);
			if (view->getUpscalerInitialized(eUpscaleMode)) {
				int upscalerQualityMode = (int)(view->getUpscalerQualityMode());
				float upscalerSharpness = view->getUpscalerSharpness();
				bool upscalerResolutionOverride = view->getUpscalerResolutionOverride();
				bool upscalerReactiveMask = view->getUpscalerReactiveMask();
				bool upscalerLockMask = view->getUpscalerLockMask();
				if (eUpscaleMode != UpscaleMode::Bilinear) {
					ImGui::Combo("Quality", &upscalerQualityMode, "Ultra Performance\0Performance\0Balanced\0Quality\0Ultra Quality\0Native\0Auto\0");

					if (eUpscaleMode != UpscaleMode::XeSS) {
						ImGui::DragFloat("Sharpness", &upscalerSharpness, 0.01f, -1.0f, 1.0f);
					}

					ImGui::Checkbox("Resolution Override", &upscalerResolutionOverride);
					if (upscalerResolutionOverride) {
						ImGui::SameLine();
						ImGui::DragInt("Resolution %", &resScale, 1, 1, 200);
					}

					if (eUpscaleMode == UpscaleMode::FSR) {
						ImGui::Checkbox("Reactive Mask", &upscalerReactiveMask);
					}

					ImGui::Checkbox("Lock Mask", &upscalerLockMask);

					view->setUpscalerQualityMode((Upscaler::QualityMode)(upscalerQualityMode));
					view->setUpscalerSharpness(upscalerSharpness);
					view->setUpscalerResolutionOverride(upscalerResolutionOverride);
					view->setUpscalerReactiveMask(upscalerReactiveMask);
					view->setUpscalerLockMask(upscalerLockMask);
				}
				else {
					ImGui::DragInt("Resolution %", &resScale, 1, 1, 200);
				}

				view->setUpscaleMode(eUpscaleMode);
			}

			ImGui::Checkbox("Denoiser", &denoiser);

			// Dumping toggle.
			bool isDumping = !dumpPath.empty();
			if (ImGui::Button(isDumping ? "Stop dump" : "Dump frames")) {
				if (isDumping) {
					dumpPath = std::string();
				}
				else {
					dumpPath = "dump/" + dateAsFilename();
					std::filesystem::create_directories(dumpPath);
					dumpFrameCount = 0;
				}
			}

			// Update viewport parameters.
			view->setDISamples(diSamples);
			view->setGISamples(giSamples);
			view->setGIBounces(giBounces);
			device->setAnisotropyLevel(std::clamp(anisotropicLevels, 1, (int)device->getPhysicalDeviceProperties().limits.maxSamplerAnisotropy));
			view->setMaxLights(maxLights);
			view->setMaxReflections(maxReflections);
			view->setMotionBlurStrength(motionBlurStrength);
			view->setMotionBlurSamples(motionBlurSamples);
			view->setVisualizationMode(visualizationMode);
			view->setResolutionScale(resScale / 100.0f);
			view->setDenoiserEnabled(denoiser);

			ImGui::EndTabItem();
		}
	}

	// Render the Post-processing window
	void Inspector::renderPostInspector(View* view) {
		assert(view != nullptr);

		if (ImGui::BeginTabItem("Post Processing")) {
			int tonemapMode = view->getTonemappingMode();
			float tonemapExposure = view->getTonemappingExposure();
			float tonemapWhite = view->getTonemappingWhite();
			float tonemapBlack = view->getTonemappingBlack();
			// float tonemapSaturation = view->getToneMapSaturation();
			float tonemapGamma = view->getTonemappingGamma();
			// bool eyeAdaption = view->getEyeAdaptionEnabledFlag();

			ImGui::Combo("Tonemapping Mode", &tonemapMode, "Raw Image\0Reinhard Tonemapper\0Reinhard-Luma\0Reinhard-Jodie\0Uncharted 2\0ACES Filmic\0Simple\0");
			ImGui::DragFloat("Exposure", &tonemapExposure, 0.01f, 0.0f, 20.0f);
			ImGui::DragFloat("White Point", &tonemapWhite, 0.01f, 0.0f, 10.0f);
			ImGui::DragFloat("Black Level", &tonemapBlack, 0.01f, 0.0f, 10.0f);
			// ImGui::DragFloat("Saturation", &tonemapSaturation, 0.001f, 0.0f, 5.0f);
			ImGui::DragFloat("Gamma", &tonemapGamma, 0.001f, 0.0f, 2.0f);
			// ImGui::Checkbox("Eye Adaption", &eyeAdaption);

			// if (eyeAdaption)
			// {
			// 	float minLogLuminance = view->getMinLogLuminance();
			// 	float logLuminanceRange = view->getLogLuminanceRange();
			// 	float lumaUpdateTime = view->getLuminanceUpdateTime();
			// 	float eyeAdaptionBrightnessFactor = view->getEyeAdaptionBrightnessFactor();

			// 	ImGui::DragFloat("Eye Adaption Minimum", &minLogLuminance, 0.01f, -20.0f, 20.0f);
			// 	ImGui::DragFloat("Eye Adaption Range", &logLuminanceRange, 0.01f, -20.0f, 20.0f);
			// 	ImGui::DragFloat("Eye Adaption Update Time", &lumaUpdateTime, 0.01f, 0.0f, 4.0f);
			// 	ImGui::DragFloat("Eye Adaption Brightening Factor", &eyeAdaptionBrightnessFactor, 0.01f, 1.0f, 20.0f);

			// 	view->setMinLogLuminance(minLogLuminance);
			// 	view->setLogLuminanceRange(logLuminanceRange);
			// 	view->setLuminanceUpdateTime(lumaUpdateTime);
			// 	view->setEyeAdaptionBrightnessFactor(eyeAdaptionBrightnessFactor);
			// }

			view->setTonemappingMode(tonemapMode);
			view->setTonemappingExposure(tonemapExposure);
			view->setTonemappingBlack(tonemapBlack);
			view->setTonemappingWhite(tonemapWhite);
			view->setTonemappingGamma(tonemapGamma);
			// view->setEyeAdaptionEnabledFlag(eyeAdaption);
			ImGui::EndTabItem();
		}
	}

	void Inspector::renderSceneInspector() {
		if (sceneDesc != nullptr && ImGui::BeginTabItem("Scene")) {
			ImGui::DragFloat3("Ambient Base Color", &sceneDesc->ambientBaseColor.x, 0.01f, 0.0f, 100.0f);
			ImGui::DragFloat3("Ambient No GI Color", &sceneDesc->ambientNoGIColor.x, 0.01f, 0.0f, 100.0f);
			ImGui::DragFloat3("Eye Light Diffuse Color", &sceneDesc->eyeLightDiffuseColor.x, 0.01f, 0.0f, 100.0f);
			ImGui::DragFloat3("Eye Light Specular Color", &sceneDesc->eyeLightSpecularColor.x, 0.01f, 0.0f, 100.0f);
			ImGui::DragFloat3("Sky Diffuse Multiplier", &sceneDesc->skyDiffuseMultiplier.x, 0.01f, 0.0f, 5.0f);
			ImGui::DragFloat3("Sky HSL Modifier", &sceneDesc->skyHSLModifier.x, 0.01f, -1.0f, 1.0f);
			ImGui::DragFloat("Sky Yaw Offset", &sceneDesc->skyYawOffset, 0.01f, 0.0f, Im3d::TwoPi);
			ImGui::DragFloat("GI Diffuse Strength", &sceneDesc->giDiffuseStrength, 0.01f, 0.0f, 100.0f);
			ImGui::DragFloat("GI Sky Strength", &sceneDesc->giSkyStrength, 0.01f, 0.0f, 100.0f);
			ImGui::EndTabItem();
		}
	}

	void Inspector::renderMaterialInspector() {
		if (material != nullptr && ImGui::BeginTabItem("Material")) {
			ImGui::Text("Name: %s", materialName.c_str());

			auto pushCommon = [](const char* name, int mask, int* attributes) {
				bool checkboxValue = *attributes & mask;
				ImGui::PushID(name);

				ImGui::Checkbox("", &checkboxValue);
				if (checkboxValue) {
					*attributes |= mask;
				}
				else {
					*attributes &= ~(mask);
				}

				ImGui::SameLine();

				if (checkboxValue) {
					ImGui::Text("%s", name);
				}
				else {
					ImGui::TextDisabled("%s", name);
				}

				return checkboxValue;
			};

			auto pushFloat = [pushCommon](const char* name, int mask, float *v, int *attributes, float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f) {
				if (pushCommon(name, mask, attributes)) {
					ImGui::SameLine();
					ImGui::DragFloat("V", v, v_speed, v_min, v_max);
				}

				ImGui::PopID();
			};

			auto pushVector3 = [pushCommon](const char *name, int mask, RT64_VECTOR3 *v, int* attributes, float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f) {
				if (pushCommon(name, mask, attributes)) {
					ImGui::SameLine();
					ImGui::DragFloat3("V", &v->x, v_speed, v_min, v_max);
				}

				ImGui::PopID();
			};

			auto pushVector4 = [pushCommon](const char *name, int mask, RT64_VECTOR4 *v, int *attributes, float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f) {
				if (pushCommon(name, mask, attributes)) {
					ImGui::SameLine();
					ImGui::DragFloat4("V", &v->x, v_speed, v_min, v_max);
				}

				ImGui::PopID();
			};

			auto pushInt = [pushCommon](const char *name, int mask, int *v, int *attributes) {
				if (pushCommon(name, mask, attributes)) {
					ImGui::SameLine();
					ImGui::InputInt("V", v);
				}

				ImGui::PopID();
			};

			pushFloat("Ignore normal factor", RT64_ATTRIBUTE_IGNORE_NORMAL_FACTOR, &material->ignoreNormalFactor, &material->enabledAttributes, 1.0f, 0.0f, 1.0f);
			pushFloat("UV detail scale", RT64_ATTRIBUTE_UV_DETAIL_SCALE, &material->uvDetailScale, &material->enabledAttributes, 0.01f, -50.0f, 50.0f);
			pushFloat("Reflection factor", RT64_ATTRIBUTE_REFLECTION_FACTOR, &material->reflectionFactor, &material->enabledAttributes, 0.01f, 0.0f, 1.0f);
			pushFloat("Reflection fresnel factor", RT64_ATTRIBUTE_REFLECTION_FRESNEL_FACTOR, &material->reflectionFresnelFactor, &material->enabledAttributes, 0.01f, 0.0f, 1.0f);
			pushFloat("Reflection shine factor", RT64_ATTRIBUTE_REFLECTION_SHINE_FACTOR, &material->reflectionShineFactor, &material->enabledAttributes, 0.01f, 0.0f, 1.0f);
			pushFloat("Refraction factor", RT64_ATTRIBUTE_REFRACTION_FACTOR, &material->refractionFactor, &material->enabledAttributes, 0.01f, 0.0f, 2.0f);
			pushVector3("Specular color", RT64_ATTRIBUTE_SPECULAR_COLOR, &material->specularColor, &material->enabledAttributes, 0.01f, 0.0f, 100.0f);
			pushFloat("Specular exponent", RT64_ATTRIBUTE_SPECULAR_EXPONENT, &material->specularExponent, &material->enabledAttributes, 0.1f, 0.0f, 1000.0f);
			pushFloat("Solid alpha multiplier", RT64_ATTRIBUTE_SOLID_ALPHA_MULTIPLIER, &material->solidAlphaMultiplier, &material->enabledAttributes, 0.01f, 0.0f, 10.0f);
			pushFloat("Shadow alpha multiplier", RT64_ATTRIBUTE_SHADOW_ALPHA_MULTIPLIER, &material->shadowAlphaMultiplier, &material->enabledAttributes, 0.01f, 0.0f, 10.0f);
			pushFloat("Depth bias", RT64_ATTRIBUTE_DEPTH_BIAS, &material->depthBias, &material->enabledAttributes, 1.0f, -1000.0f, 1000.0f);
			pushFloat("Shadow ray bias", RT64_ATTRIBUTE_SHADOW_RAY_BIAS, &material->shadowRayBias, &material->enabledAttributes, 1.0f, 0.0f, 1000.0f);
			pushVector3("Self light", RT64_ATTRIBUTE_SELF_LIGHT, &material->selfLight, &material->enabledAttributes, 0.01f, 0.0f, 10.0f);
			pushVector4("Diffuse color mix", RT64_ATTRIBUTE_DIFFUSE_COLOR_MIX, &material->diffuseColorMix, &material->enabledAttributes, 0.01f, 0.0f, 1.0f);
			pushInt("Light group mask bits", RT64_ATTRIBUTE_LIGHT_GROUP_MASK_BITS, (int *)(&material->lightGroupMaskBits), &material->enabledAttributes);

			ImGui::EndTabItem();
		}
	}

	void Inspector::renderLightInspector() {
		if (lights != nullptr && ImGui::BeginTabItem("Lights")) {
			ImGui::InputInt("Light count", lightCount);
			*lightCount = std::min(std::max(*lightCount, 1), maxLightCount);

			for (int i = 0; i < *lightCount; i++) {
				ImGui::PushID(i);
				Im3d::PushId(i);

				if (ImGui::CollapsingHeader("Point light")) {
					const int SphereDetail = 64;
					ImGui::DragFloat3("Position", &lights[i].position.x);
					Im3d::GizmoTranslation("GizmoPosition", &lights[i].position.x);
					ImGui::DragFloat3("Diffuse color", &lights[i].diffuseColor.x, 0.01f);
					ImGui::DragFloat("Attenuation radius", &lights[i].attenuationRadius);
					Im3d::SetColor(lights[i].diffuseColor.x, lights[i].diffuseColor.y, lights[i].diffuseColor.z);
					Im3d::DrawSphere(Im3d::Vec3(lights[i].position.x, lights[i].position.y, lights[i].position.z), lights[i].attenuationRadius, SphereDetail);
					ImGui::DragFloat("Point radius", &lights[i].pointRadius);
					Im3d::SetColor(lights[i].diffuseColor.x * 0.5f, lights[i].diffuseColor.y * 0.5f, lights[i].diffuseColor.z * 0.5f);
					Im3d::DrawSphere(Im3d::Vec3(lights[i].position.x, lights[i].position.y, lights[i].position.z), lights[i].pointRadius, SphereDetail);
					ImGui::DragFloat3("Specular color", &lights[i].specularColor.x, 0.01f);
					ImGui::DragFloat("Shadow offset", &lights[i].shadowOffset);
					Im3d::SetColor(Im3d::Color_Black);
					Im3d::DrawSphere(Im3d::Vec3(lights[i].position.x, lights[i].position.y, lights[i].position.z), lights[i].shadowOffset, SphereDetail);
					ImGui::DragFloat("Attenuation exponent", &lights[i].attenuationExponent);
					ImGui::DragFloat("Flicker intensity", &lights[i].flickerIntensity);
					ImGui::InputInt("Group bits", (int *)(&lights[i].groupBits));

					if ((*lightCount) < maxLightCount) {
						if (ImGui::Button("Duplicate")) {
							lights[*lightCount] = lights[i];
							*lightCount = *lightCount + 1;
						}
					}
				}

				Im3d::PopId();
				ImGui::PopID();
			}
			ImGui::EndTabItem();
		}
	}

	void Inspector::controlCamera(View *view, long cursorX, long cursorY) {
		view->setPerspectiveControlActive(cameraControl);
		if (cameraControl) {
			if ((cameraPanX != 0.0f) || (cameraPanY != 0.0f)) {
				view->movePerspective({ cameraPanX, cameraPanY, 0.0f });
			}
			else if (!ImGui::GetIO().WantCaptureMouse) {
				float cameraSpeed = (view->getFarDistance() - view->getNearDistance()) / 5.0f * cameraPanSpeed;
				bool leftAlt = glfwGetKey(device->getWindow(), GLFW_KEY_LEFT_ALT);
				bool leftCtrl = glfwGetKey(device->getWindow(), GLFW_KEY_LEFT_CONTROL);
				float localX = (cursorX - prevCursorX) / (float)(view->getWidth());
				float localY = (cursorY - prevCursorY) / (float)(view->getHeight());
				localX += cameraPanX;
				localY += cameraPanY;
				if (invertCameraX) { localX *= -1.0f; }
				if (invertCameraY) { localY *= -1.0f; }

				if (glfwGetMouseButton(device->getWindow(), GLFW_MOUSE_BUTTON_MIDDLE)) {
					if (leftCtrl) {
						view->movePerspective({ 0.0f, 0.0f, (-localY) * cameraSpeed });
					}
					else if (leftAlt) {
						float cameraRotationSpeed = 5.0f;
						view->rotatePerspective(localX * cameraRotationSpeed, localY * cameraRotationSpeed, cameraRoll);
					}
					else {
						view->movePerspective({ -localX * cameraSpeed, localY * cameraSpeed, 0.0f });
					}
				}
			}

			prevCursorX = cursorX;
			prevCursorY = cursorY;
		}
	}

	void Inspector::renderCameraControl() {
		if (ImGui::BeginTabItem("Camera")) {
			ImGui::Checkbox("Enable", &cameraControl);
			if (cameraControl) {
				ImGui::DragFloat("Camera pan x", &cameraPanX, 0.1f, -100.0f, 100.0f);
				ImGui::DragFloat("Camera pan y", &cameraPanY, 0.1f, -100.0f, 100.0f);
				ImGui::DragFloat("Camera roll", &cameraRoll, 0.1f, -100.0f, 100.0f);
				ImGui::DragFloat("Camera pan speed", &cameraPanSpeed, 0.01f, 0.0f, 10.0f);
				ImGui::Checkbox("Invert Camera X", &invertCameraX);
				ImGui::Checkbox("Invert Camera Y", &invertCameraY);

				ImGui::Text("Middle Button: Pan");
				ImGui::Text("Ctrl + Middle Button: Zoom");
				ImGui::Text("Alt + Middle Button: Rotate");
			}
			else {
				cameraPanX = 0.0f;
				cameraPanY = 0.0f;
			}
			ImGui::EndTabItem();
		}
	}

	void Inspector::renderPrint() {
		if (!printMessages.empty()) {
			ImGui::Begin("Print");
			for (size_t i = 0; i < printMessages.size(); i++) {
				ImGui::Text("%s", printMessages[i].c_str());
			}
			ImGui::End();
		}
	}

	void Inspector::setupWithView(View *view, long cursorX, long cursorY) {
		assert(view != nullptr);
		Im3d::AppData& appData = Im3d::GetAppData();
		RT64_VECTOR3 viewPos = view->getViewPosition();
		RT64_VECTOR3 viewDir = view->getViewDirection();
		RT64_VECTOR3 rayDir = view->getRayDirectionAt(cursorX, cursorY);
		appData.m_deltaTime = 1.0f / 30.0f;
		appData.m_viewportSize = Im3d::Vec2((float)(view->getWidth()), (float)(view->getHeight()));
		appData.m_viewOrigin = Im3d::Vec3(viewPos.x, viewPos.y, viewPos.z);
		appData.m_viewDirection = Im3d::Vec3(viewDir.x, viewDir.y, viewDir.z);
		appData.m_worldUp = Im3d::Vec3(0.0f, 1.0f, 0.0f);
		appData.m_projOrtho = false;
		appData.m_projScaleY = tanf(view->getFOVRadians() * 0.5f) * 2.0f;
		appData.m_snapTranslation = 0.0f;
		appData.m_snapRotation = 0.0f;
		appData.m_snapScale = 0.0f;
		appData.m_cursorRayOrigin = Im3d::Vec3(viewPos.x, viewPos.y, viewPos.z);
		appData.m_cursorRayDirection = Im3d::Vec3(rayDir.x, rayDir.y, rayDir.z);
		appData.m_keyDown[Im3d::Mouse_Left] = (glfwGetMouseButton(device->getWindow(), GLFW_MOUSE_BUTTON_LEFT)) != 0;
	}

	void Inspector::setSceneDescription(RT64_SCENE_DESC* sceneDesc) {
		this->sceneDesc = sceneDesc;
	}

	void Inspector::setMaterial(RT64_MATERIAL* material, const std::string &materialName) {
		this->material = material;
		this->materialName = materialName;
	}

	void Inspector::setLights(RT64_LIGHT* lights, int *lightCount, int maxLightCount) {
		this->lights = lights;
		this->lightCount = lightCount;
		this->maxLightCount = maxLightCount;
	}

	void Inspector::printClear() {
		printMessages.clear();
	}

	void Inspector::printMessage(const std::string& message) {
		printMessages.push_back(message);
	}

	Device* Inspector::getDevice() { return device; }
};

// Library Exports

DLEXPORT RT64_INSPECTOR* RT64_CreateInspector(RT64_DEVICE* devicePtr) {
    assert(devicePtr != nullptr);
    RT64::Device* device = (RT64::Device*)(devicePtr);
    RT64::Inspector* inspector = new RT64::Inspector();
	inspector->init(device);
	device->addInspectorOld(inspector);
    return (RT64_INSPECTOR*)(inspector);
}

DLEXPORT void RT64_SetSceneInspector(RT64_INSPECTOR* inspectorPtr, RT64_SCENE_DESC* sceneDesc) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    inspector->setSceneDescription(sceneDesc);
}

DLEXPORT void RT64_SetMaterialInspector(RT64_INSPECTOR* inspectorPtr, RT64_MATERIAL* material, const char* materialName) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    inspector->setMaterial(material, std::string(materialName));
}

DLEXPORT void RT64_SetLightsInspector(RT64_INSPECTOR* inspectorPtr, RT64_LIGHT* lights, int* lightCount, int maxLightCount) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    inspector->setLights(lights, lightCount, maxLightCount);
}

DLEXPORT void RT64_PrintClearInspector(RT64_INSPECTOR* inspectorPtr) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    inspector->printClear();
}

DLEXPORT void RT64_PrintMessageInspector(RT64_INSPECTOR* inspectorPtr, const char* message) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    std::string messageStr(message);
    inspector->printMessage(messageStr);
}

DLEXPORT void RT64_DestroyInspector(RT64_INSPECTOR* inspectorPtr) {
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
	inspector->getDevice()->removeInspectorOld(inspector);
    delete inspector;
}

#endif