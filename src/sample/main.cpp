//
// RT64 SAMPLE
//

#include "rt64.h"

#define WINDOW_TITLE "RT64 Sample"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/ext.hpp>
#include <chrono>
#include <unordered_map>
#include <iostream>

// #define TINYGLTF_IMPLEMENTATION
// #undef TINYGLTF_INTERNAL_NOMINMAX
// #define TINYGLTF_NOEXCEPTION // optional. disable exception handling.
// #include "contrib/tinygltf/tiny_gltf.h"

#ifndef _WIN32
#define _countof(array) (sizeof(array) / sizeof(array[0]))
#endif

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
static void infoMessage(HWND hWnd, const char *message) {
	MessageBox(hWnd, message, WINDOW_TITLE, MB_OK | MB_ICONINFORMATION);
}

static void errorMessage(HWND hWnd, const char *message) {
	MessageBox(hWnd, message, WINDOW_TITLE " Error", MB_OK | MB_ICONERROR);
}
#else
static void infoMessage(void* hWnd, const char *message) {
	std::cout << "[INFO]: " << message << "\n";
}

static void errorMessage(void* hWnd,const char *message) {
	std::cout << "[ERROR]: " << message << "\n";
}
#endif

#ifndef RT64_MINIMAL

#include <stdio.h>

#define _USE_MATH_DEFINES
#include <math.h>

#define STB_IMAGE_IMPLEMENTATION
#include "contrib/stb_image.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "contrib/tiny_obj_loader.h"

typedef struct {
	RT64_VECTOR4 position;
	RT64_VECTOR3 normal;
	RT64_VECTOR2 uv;
	RT64_VECTOR4 input1;
} VERTEX;

struct {
	RT64_LIBRARY lib;
	RT64_LIGHT lights[16];
	int lightCount;
	bool showInspector;
	RT64_DEVICE *device = nullptr;
	RT64_SCENE *scene = nullptr;
	RT64_SCENE_DESC sceneDesc;
	RT64_VIEW *view = nullptr;
	RT64_MATRIX4 viewMatrix;
	RT64_MESH *mesh = nullptr;
	RT64_SHADER *shader = nullptr;
	RT64_TEXTURE *textureDif = nullptr;
	RT64_TEXTURE *textureNrm = nullptr;
	RT64_TEXTURE *textureSpc = nullptr;
	RT64_MATERIAL baseMaterial;
	RT64_MATERIAL frameMaterial;
	RT64_MATERIAL materialMods;
	RT64_MATRIX4 transform;
	RT64_INSTANCE *instance = nullptr;
	std::vector<VERTEX> objVertices;
	std::vector<unsigned int> objIndices;
} RT64;

struct {
#ifdef _WIN32
	std::chrono::steady_clock::time_point startTime = std::chrono::high_resolution_clock::now();;
#else
	std::chrono::_V2::system_clock::time_point startTime = std::chrono::high_resolution_clock::now();;
#endif
	bool doDaylightCycle = true;
	float daylightCycleSpeed = 1.0f / 32.0f;
	float daylightTime = 0.5f;
	float deltaTime = 0.0f;
	float sceneScale = 10.0f;
	float runtime = 0.0f;
	RT64_SHADER* uiShader = nullptr;
} Sample;


// 	case WM_RBUTTONDOWN: {
// 		POINT cursorPos = {};
// 		GetCursorPos(&cursorPos);
// 		ScreenToClient(hWnd, &cursorPos);
// 		RT64_INSTANCE *instance = RT64.lib.GetViewRaytracedInstanceAt(RT64.view, cursorPos.x, cursorPos.y);
// 		fprintf(stdout, "GetViewRaytracedInstanceAt: %p\n", instance);
// 		break;

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	if (action == GLFW_PRESS) {
		switch (key)
		{
			case GLFW_KEY_F1:
				RT64.showInspector = !RT64.showInspector;
				RT64.lib.SetInspectorVisibility(RT64.device, RT64.showInspector);
				break;
			case GLFW_KEY_F3:
				Sample.daylightTime = 0.0f;
				break;
			case GLFW_KEY_F4:
				Sample.doDaylightCycle = !Sample.doDaylightCycle;
				break;
			case GLFW_KEY_UP:
				Sample.daylightCycleSpeed *= 2.0f;
				break;
			case GLFW_KEY_DOWN:
				if (Sample.daylightCycleSpeed / 2.0f > FLT_EPSILON) {
					Sample.daylightCycleSpeed /= 2.0f;
				}
				break;
			case GLFW_KEY_LEFT:
				Sample.daylightTime -= 0.125f;
				break;
			case GLFW_KEY_RIGHT:
				Sample.daylightTime += 0.125f;
				break;
		}
	} else if (action == GLFW_REPEAT) {
		switch (key)
		{
			case GLFW_KEY_LEFT:
				Sample.daylightTime -= 0.125f;
				break;
			case GLFW_KEY_RIGHT:
				Sample.daylightTime += 0.125f;
				break;
		}
	}
}

void mouseButtonCallBack(GLFWwindow* window, int button, int action, int mods) {
	double xpos, ypos = 0.0;
	if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
		glfwGetCursorPos(window, &xpos, &ypos);
	}
}

#ifndef M_PI
	#define M_PI  3.14159265358979323846f
#endif

// Process drawing 
void draw(GLFWwindow* window ) {
	int focused = glfwGetWindowAttrib(window, GLFW_FOCUSED);
	// if (!focused) {
	// 	return;
	// }
	
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - Sample.startTime).count() / 4.0f;

	// Code to make the view spin around
    #define RADIUS 2.0f
    #define YOFF 5.0f
	glm::vec3 eye = glm::vec3(sinf(time) * glm::radians(90.0f) * 6.50 - 1.0, YOFF, cosf(time) * glm::radians(90.0f) * 1.750 - 1.6250);
	eye.x *= Sample.sceneScale;
	eye.y *= Sample.sceneScale;
	eye.z *= Sample.sceneScale;
	glm::mat4 v = glm::lookAt(eye, glm::vec3(0.0f, (cosf(time / 2.0f) * glm::radians(90.0f) * -2.00 + 3.0) * Sample.sceneScale, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	RT64.viewMatrix.m[0][0] =  v[0][0];
	RT64.viewMatrix.m[1][0] =  v[1][0];
	RT64.viewMatrix.m[2][0] =  v[2][0];
	RT64.viewMatrix.m[3][0] =  v[3][0];
	RT64.viewMatrix.m[0][1] =  v[0][1];
	RT64.viewMatrix.m[1][1] =  v[1][1];
	RT64.viewMatrix.m[2][1] =  v[2][1];
	RT64.viewMatrix.m[3][1] =  v[3][1];
	RT64.viewMatrix.m[0][2] =  v[0][2];
	RT64.viewMatrix.m[1][2] =  v[1][2];
	RT64.viewMatrix.m[2][2] =  v[2][2];
	RT64.viewMatrix.m[3][2] =  v[3][2];
	RT64.viewMatrix.m[0][3] =  v[0][3];
	RT64.viewMatrix.m[1][3] =  v[1][3];
	RT64.viewMatrix.m[2][3] =  v[2][3];
	RT64.viewMatrix.m[3][3] =  v[3][3];
	RT64.lib.SetViewPerspective(RT64.view, RT64.viewMatrix, (45.0f * (float)(M_PI)) / 180.0f, 0.1f, 1000.0f, true);

	// Day-night cycle

	float daylightSin =   sinf(Sample.daylightTime);
	float daylightSinM = -sinf(Sample.daylightTime);
	float daylightCos =   cosf(Sample.daylightTime);
	float daylightCosM = -cosf(Sample.daylightTime);
	RT64_VECTOR3 sunColor = {10.0f, 8.75f, 7.50f};
	RT64_VECTOR3 moonColor = {0.125f, 0.5f, 0.75f};
	RT64.lights[0].position = { daylightSin * 1500000.0f, daylightSin * 3000000.0f, daylightCos * 3000000.0f };
	RT64.lights[1].position = { daylightSinM * 15000.0f, daylightSinM * 30000.0f, daylightCosM * 30000.0f };
	RT64.lights[0].diffuseColor = { 
		glm::clamp(sunColor.x * (daylightSin < 0.f ? 0.f : daylightSin * daylightSin), 0.0f, sunColor.x),
		glm::clamp(sunColor.y * daylightSin * daylightSin * daylightSin, 0.0f, sunColor.y),
		glm::clamp(sunColor.z * daylightSin * daylightSin * daylightSin * daylightSin * daylightSin, 0.0f, sunColor.z),
	};
	RT64.lights[1].diffuseColor = { 
		glm::clamp(moonColor.x * daylightSinM - 0.0025f, 0.0f, moonColor.x),
		glm::clamp(moonColor.y * daylightSinM - 0.0025f, 0.0f, moonColor.y),
		glm::clamp(moonColor.z * daylightSinM - 0.0025f, 0.0f, moonColor.z),
	};
	RT64.lights[0].specularColor = RT64.lights[0].diffuseColor;
	RT64.lights[1].specularColor = RT64.lights[1].diffuseColor;
	float daylight = glm::clamp(daylightSin, 0.f, 1.f);
	RT64.sceneDesc.skyDiffuseMultiplier = { 
		0.075f + daylight * 1.925f,
		0.125f + daylight * 1.875f,
		0.25f + daylight * 1.75f,
	};
	RT64.sceneDesc.ambientBaseColor = { 
		0.00125f + glm::clamp(daylightSin * daylightSin * daylightSin * 0.125f, 0.0f, 1.0f), 
		0.0015f + glm::clamp(daylightSin * daylightSin * daylightSin * 0.15f, 0.0f, 1.0f),  
		0.0025f + glm::clamp(daylightSin * daylightSin * daylightSin * 0.25f, 0.0f, 1.0f), 
	};
	RT64.lib.SetSceneDescription(RT64.scene, RT64.sceneDesc);
	
	RT64.frameMaterial = RT64.baseMaterial;
	RT64_ApplyMaterialAttributes(&RT64.frameMaterial, &RT64.materialMods);
	RT64_INSTANCE_DESC instDesc;
	instDesc.scissorRect = { 0, 0, 0, 0 };
	instDesc.viewportRect = { 0, 0, 0, 0 };
	instDesc.mesh = RT64.mesh;
	instDesc.transform = RT64.transform;
	instDesc.previousTransform = RT64.transform;
	instDesc.diffuseTexture = RT64.textureDif;
	instDesc.normalTexture = RT64.textureNrm;
	instDesc.specularTexture = RT64.textureSpc;
	instDesc.material = RT64.frameMaterial;
	instDesc.shader = RT64.shader;
	instDesc.flags = 0;
	RT64.lib.SetInstanceDescription(RT64.instance, instDesc);
	RT64.lib.SetSceneLights(RT64.scene, RT64.lights, RT64.lightCount);
	RT64.lib.DrawDevice(RT64.device, 1, Sample.deltaTime);

    auto postDrawTime = std::chrono::high_resolution_clock::now();
    Sample.deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(postDrawTime - currentTime).count();
	Sample.runtime += Sample.deltaTime;
	if (Sample.doDaylightCycle) {
		Sample.daylightTime += (Sample.deltaTime * Sample.daylightCycleSpeed);
	}

	// Inspector stuff
	if (RT64.showInspector) {
		RT64.lib.PrintClearInspector(RT64.device);
		RT64.lib.SetMaterialInspector(RT64.device, &RT64.materialMods, "Sphere");
		RT64.lib.SetSceneInspector(RT64.device, &RT64.sceneDesc);
		RT64.lib.SetLightsInspector(RT64.device, RT64.lights, &RT64.lightCount, _countof(RT64.lights));
		std::string print = "Draw time: ";
		print.append(std::to_string(Sample.deltaTime));
		print.append("\nRuntime: ");
		print.append(std::to_string(Sample.runtime));
		RT64.lib.PrintMessageInspector(RT64.device, print.c_str());
	}
}

bool createRT64(GLFWwindow* glfwWindow) {
	// Setup library.
	RT64.lib = RT64_LoadLibrary();
	if (RT64.lib.handle == 0) {
		errorMessage(nullptr, "Failed to load RT64 library.");
		return false;
	}

	// Setup device.
	RT64.device = RT64.lib.CreateDevice(glfwWindow);
	if (RT64.device == nullptr) {
		errorMessage(nullptr, RT64.lib.GetLastError());
		return false;
	}

	return true;
}

RT64_TEXTURE* loadTexturePNG(const char* path) {
	RT64_TEXTURE_DESC texDesc;
	int texChannels;
	texDesc.format = RT64_TEXTURE_FORMAT_RGBA8;
	texDesc.bytes = stbi_load(path, &texDesc.width, &texDesc.height, &texChannels, STBI_rgb_alpha);
	texDesc.rowPitch = texDesc.width * 4;
	texDesc.byteCount = texDesc.rowPitch * texDesc.height;
	texDesc.name = path;
	RT64_TEXTURE* texture = RT64.lib.CreateTexture(RT64.device, texDesc);
	stbi_image_free((void*)(texDesc.bytes));
	return texture;
}

RT64_TEXTURE* loadTextureDDS(const char* path, bool normal) {
	RT64_TEXTURE* texture = nullptr;
	FILE* ddsFp = stbi__fopen("res/grass_dif.dds", "rb");
	if (ddsFp != nullptr) {
		fseek(ddsFp, 0, SEEK_END);
		int ddsDataSize = ftell(ddsFp);
		fseek(ddsFp, 0, SEEK_SET);
		if (ddsDataSize > 0) {
			void *ddsData = malloc(ddsDataSize);
			fread(ddsData, ddsDataSize, 1, ddsFp);
			fclose(ddsFp);

			RT64_TEXTURE_DESC texDesc;
			texDesc.bytes = ddsData;
			texDesc.byteCount = ddsDataSize;
			texDesc.format = RT64_TEXTURE_FORMAT_DDS;
			texDesc.width = texDesc.height = texDesc.rowPitch = -1;
			texture = RT64.lib.CreateTexture(RT64.device, texDesc);
			free(ddsData);
		}
		else {
			fclose(ddsFp);
		}
	}

	return texture;
}

void setupRT64Scene() {
	// Setup scene.
	RT64.scene = RT64.lib.CreateScene(RT64.device);
	RT64.sceneDesc.ambientBaseColor = { 0.0125f, 0.0125f, 0.0125f };
	RT64.sceneDesc.ambientNoGIColor = { 0.0f, 0.0f, 0.0f };
	RT64.sceneDesc.eyeLightDiffuseColor = { 0.00f, 0.00f, 0.00f };
	RT64.sceneDesc.eyeLightSpecularColor = { 0.00f, 0.00f, 0.00f };
	RT64.sceneDesc.skyDiffuseMultiplier = { 1.0f, 1.0f, 1.0f };
	RT64.sceneDesc.skyHSLModifier = { 0.0f, 0.0f, 0.0f };
	RT64.sceneDesc.skyYawOffset = 0.0f;
	RT64.sceneDesc.giDiffuseStrength = 1.0f;
	RT64.sceneDesc.giSkyStrength = 1.0f;
	RT64.lib.SetSceneDescription(RT64.scene, RT64.sceneDesc);

	// Setup shader.
	int shaderFlags = RT64_SHADER_RASTER_ENABLED | RT64_SHADER_RASTER_TRANSFORMS_ENABLED | RT64_SHADER_RAYTRACE_ENABLED | RT64_SHADER_NORMAL_MAP_ENABLED | RT64_SHADER_SPECULAR_MAP_ENABLED;
	RT64.shader = RT64.lib.CreateShader(RT64.device, 0x01200a00, RT64_SHADER_FILTER_LINEAR, RT64_SHADER_ADDRESSING_WRAP, RT64_SHADER_ADDRESSING_WRAP, shaderFlags);
	shaderFlags = RT64_SHADER_RASTER_ENABLED;
	Sample.uiShader = RT64.lib.CreateShader(RT64.device, 0x01200a01, RT64_SHADER_FILTER_LINEAR, RT64_SHADER_ADDRESSING_WRAP, RT64_SHADER_ADDRESSING_WRAP, shaderFlags);

	// Setup lights.
	RT64.lights[0].position = { 0.0f, 0.0f, 0.0f };
	RT64.lights[0].attenuationRadius = 1e9;
	RT64.lights[0].pointRadius = 10000.0f;
	RT64.lights[0].diffuseColor = { 0.8f, 0.75f, 0.65f };
	RT64.lights[0].specularColor = { 0.8f, 0.75f, 0.65f };
	RT64.lights[0].shadowOffset = 0.0f;
	RT64.lights[0].attenuationExponent = 1.0f;
	RT64.lights[1] = RT64.lights[0];
	RT64.lights[1].pointRadius = 1000.0f;
	RT64.lightCount = 2;

	for (int i = 0; i < _countof(RT64.lights); i++) {
		RT64.lights[i].groupBits = RT64_LIGHT_GROUP_DEFAULT;
	}

	// Setup view.
	RT64.view = RT64.lib.CreateView(RT64.scene);

	// Load textures.
	// RT64.textureDif = loadTextureDDS("res/grass_dif.dds");
	RT64.textureDif = loadTexturePNG("res/Marble012_1K_Color.png");
	RT64.textureNrm = loadTexturePNG("res/Marble012_1K_NormalGL.png");
	RT64.textureSpc = nullptr /* loadTexturePNG("res/grass_spc.png") */;
	RT64_TEXTURE *textureSky = loadTexturePNG("res/clouds2.png");
	RT64.lib.SetViewSkyPlane(RT64.view, textureSky);

	// Make initial transform with a 0.1f scale.
	float scale = 1.0f;
	memset(RT64.transform.m, 0, sizeof(RT64_MATRIX4));
	RT64.transform.m[0][0] = scale;
	RT64.transform.m[1][1] = scale;
	RT64.transform.m[2][2] = scale;
	RT64.transform.m[3][3] = 1.0f;

	// Make initial view.
	memset(RT64.viewMatrix.m, 0, sizeof(RT64_MATRIX4));
	RT64.viewMatrix.m[0][0] = 1.0f;
	RT64.viewMatrix.m[1][1] = 1.0f;
	RT64.viewMatrix.m[2][2] = 1.0f;
	RT64.viewMatrix.m[3][0] = 0.0f;
	RT64.viewMatrix.m[3][1] = -2.0f;
	RT64.viewMatrix.m[3][2] = -10.0f;
	RT64.viewMatrix.m[3][3] = 1.0f;

	// Create mesh from obj file.
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn;
	std::string err;
	bool loaded = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, "res/teapot.obj", NULL, true);
	assert(loaded);
	
	for (size_t i = 0; i < shapes.size(); i++) {
		size_t index_offset = 0;
		for (size_t f = 0; f < shapes[i].mesh.num_face_vertices.size(); f++) {
			size_t fnum = shapes[i].mesh.num_face_vertices[f];
			for (size_t v = 0; v < fnum; v++) {
				tinyobj::index_t idx = shapes[i].mesh.indices[index_offset + v];
				VERTEX vertex;
				vertex.position = { attrib.vertices[3 * idx.vertex_index + 0], attrib.vertices[3 * idx.vertex_index + 1], attrib.vertices[3 * idx.vertex_index + 2], 1.0f };
				vertex.normal = { attrib.normals[3 * idx.normal_index + 0], attrib.normals[3 * idx.normal_index + 1], attrib.normals[3 * idx.normal_index + 2] };
				vertex.uv = { acosf(vertex.normal.x), acosf(vertex.normal.y) };
				vertex.input1 = { 1.0f, 1.0f, 1.0f, 1.0f };

				RT64.objIndices.push_back((unsigned int)(RT64.objVertices.size()));
				RT64.objVertices.push_back(vertex);
			}

			index_offset += fnum;
		}
	}
	
	RT64.mesh = RT64.lib.CreateMesh(RT64.device, RT64_MESH_RAYTRACE_ENABLED | RT64_MESH_RAYTRACE_FAST_TRACE | RT64_MESH_RAYTRACE_COMPACT);
	RT64.lib.SetMesh(RT64.mesh, RT64.objVertices.data(), (int)(RT64.objVertices.size()), sizeof(VERTEX), RT64.objIndices.data(), (int)(RT64.objIndices.size()));
	
	// Configure material.
	RT64.baseMaterial.ignoreNormalFactor = 0.0f;
	RT64.baseMaterial.uvDetailScale = 1.0f;
	RT64.baseMaterial.reflectionFactor = 0.0f;
	RT64.baseMaterial.reflectionFresnelFactor = 0.5f;
	RT64.baseMaterial.reflectionShineFactor = 0.0f;
	RT64.baseMaterial.refractionFactor = 0.0f;
	RT64.baseMaterial.specularColor = { 10.0f, 10.0f, 10.0f };
	RT64.baseMaterial.specularExponent = 5.0f;
	RT64.baseMaterial.solidAlphaMultiplier = 1.0f;
	RT64.baseMaterial.shadowAlphaMultiplier = 1.0f;
	RT64.baseMaterial.diffuseColorMix = { 0.0f, 0.0f, 0.0f, 0.0f };
	RT64.baseMaterial.selfLight = { 0.0f , 0.0f, 0.0f };
	RT64.baseMaterial.lightGroupMaskBits = 0xFFFFFFFF;
	RT64.baseMaterial.fogColor = { 0.3f, 0.5f, 0.7f };
	RT64.baseMaterial.fogMul = 1.0f;
	RT64.baseMaterial.fogOffset = 0.0f;
	RT64.baseMaterial.fogEnabled = 0;
	
	// Vertices for the UI instance
	VERTEX vertices[3];
	vertices[2].position = { -1.0f, -0.7f, 0.0f, 1.0f } ;
	vertices[2].normal = { 0.0f, 1.0f, 0.0f };
	vertices[2].uv = { 0.0f, 0.0f };
	vertices[2].input1 = { 1.0f, 1.0f, 1.0f, 1.0f };

	vertices[1].position = { -0.5f, -0.7f, 0.0f, 1.0f };
	vertices[1].normal = { 0.0f, 1.0f, 0.0f };
	vertices[1].uv = { 1.0f, 0.0f };
	vertices[1].input1 = { 1.0f, 1.0f, 1.0f, 1.0f };

	vertices[0].position = { -0.75f, -0.9f, 0.0f, 1.0f };
	vertices[0].normal = { 0.0f, 1.0f, 0.0f };
	vertices[0].uv = { 0.0f, 1.0f };
	vertices[0].input1 = { 1.0f, 1.0f, 1.0f, 1.0f };
	
	unsigned int indices[] = { 0, 1, 2 };
	RT64_TEXTURE* altTexture = loadTexturePNG("res/tiles_dif.png");
	RT64_TEXTURE* normalTexture = loadTexturePNG("res/tiles_nrm.png");
	RT64_TEXTURE* specularTexture = loadTexturePNG("res/tiles_spc.png");

	RT64_MESH* mesh = RT64.lib.CreateMesh(RT64.device, 0);
	RT64.lib.SetMesh(mesh, vertices, _countof(vertices), sizeof(VERTEX), indices, _countof(indices));

	vertices[0].position.y += 0.15f;
	vertices[1].position.y += 0.15f;
	vertices[2].position.y += 0.15f;

	RT64_MESH* altMesh = RT64.lib.CreateMesh(RT64.device, 0);
	RT64.lib.SetMesh(altMesh, vertices, _countof(vertices), sizeof(VERTEX), indices, _countof(indices));

	RT64_INSTANCE_DESC instDesc {};
	instDesc.scissorRect = { 0, 0, 0, 0 };
	instDesc.viewportRect = { 0, 0, 0, 0 };
	instDesc.mesh = altMesh;
	instDesc.transform = RT64.transform;
	instDesc.previousTransform = RT64.transform;
	instDesc.diffuseTexture = altTexture;
	instDesc.normalTexture = normalTexture;
	instDesc.specularTexture = specularTexture;
	instDesc.material = RT64.baseMaterial;
	instDesc.shader = Sample.uiShader;
	instDesc.flags = RT64_INSTANCE_RASTER_UI;

	// Create HUD B Instance.
	RT64_INSTANCE *instanceB = RT64.lib.CreateInstance(RT64.scene);
	RT64.lib.SetInstanceDescription(instanceB, instDesc);

	// Create RT Instance.
	RT64.instance = RT64.lib.CreateInstance(RT64.scene);
	instDesc.mesh = RT64.mesh;
	instDesc.shader = RT64.shader;
	instDesc.flags = 0;
	instDesc.diffuseTexture = RT64.textureDif;
	instDesc.normalTexture = RT64.textureNrm;
	instDesc.specularTexture = RT64.textureSpc;
	RT64.lib.SetInstanceDescription(RT64.instance, instDesc);

	// Create HUD A Instance.
	RT64_INSTANCE* instanceA = RT64.lib.CreateInstance(RT64.scene);
	instDesc.mesh = mesh;
	instDesc.normalTexture = nullptr;
	instDesc.specularTexture = nullptr;
	instDesc.flags = RT64_INSTANCE_RASTER_BACKGROUND;
	RT64.lib.SetInstanceDescription(instanceA, instDesc);

	// Create floor.
	VERTEX floorVertices[4];
	RT64_MATRIX4 floorTransform;
	unsigned int floorIndices[6] = { 2, 1, 0, 1, 2, 3 };
	floorVertices[0].position = { -1.5f, 0.0f, -1.0f, 1.0f };
	floorVertices[0].uv = { 0.0f, 0.0f };
	floorVertices[1].position = { 1.0f, 0.0f, -1.0f, 1.0f };
	floorVertices[1].uv = { 1.0f, 0.0f };
	floorVertices[2].position = { -1.5f, 0.0f, 1.0f, 1.0f };
	floorVertices[2].uv = { 0.0f, 1.0f };
	floorVertices[3].position = { 1.0f, 0.0f, 1.0f, 1.0f };
	floorVertices[3].uv = { 1.0f, 1.0f };

	for (int i = 0; i < _countof(floorVertices); i++) {
		floorVertices[i].normal = { 0.0f, 1.0f, 0.0f };
		floorVertices[i].input1 = { 1.0f, 1.0f, 1.0f, 1.0f };
	}

	memset(&floorTransform, 0, sizeof(RT64_MATRIX4));
	floorTransform.m[0][0] = 10.0f;
	floorTransform.m[1][1] = 10.0f;
	floorTransform.m[2][2] = 10.0f;
	floorTransform.m[3][3] = 1.0f;

	RT64_MESH* floorMesh = RT64.lib.CreateMesh(RT64.device, RT64_MESH_RAYTRACE_ENABLED);
	RT64.lib.SetMesh(floorMesh, floorVertices, _countof(floorVertices), sizeof(VERTEX), floorIndices, _countof(floorIndices));
	// RT64_INSTANCE* floorInstance = RT64.lib.CreateInstance(RT64.scene);
	RT64_MATERIAL floorMat = RT64.baseMaterial;
	floorMat.reflectionFactor = 0.0f;
	instDesc.mesh = floorMesh;
	instDesc.transform = floorTransform;
	instDesc.previousTransform = floorTransform;
	instDesc.diffuseTexture = altTexture;
	instDesc.normalTexture = normalTexture;
	instDesc.specularTexture = specularTexture;
	instDesc.shader = RT64.shader;
	instDesc.material = floorMat;
	instDesc.flags = 0;
	// RT64.lib.SetInstanceDescription(floorInstance, instDesc);
}

void destroyRT64() {
	RT64.lib.DestroyDevice(RT64.device);
	RT64_UnloadLibrary(RT64.lib);
}

/*	How I think this whole thing works so far:
		- The library creates a device
		- Then you create a scene, as well as describe that scene
		- Then you create meshes, textures, transform matrices, shaders viewport and scissor rectangles and whatnot. 
		- Then you pass them into an Instance
			- This instance will get passed into the scene
		- Then you create a view and pass the scene into it
			- The view is a actually a member of the scene
		- The screen then gets rendered
			- It first calls device to draw the scene
			- In the draw call, device calls the function in scene to render the scene
			- The scene render call has a call to the function to render the view
			- Basically, you call device, who then calls scene, who then calls view
		- Hierarchy:
			- Device
				- Scene
					- View
					- Instances
						- Mesh
						- Textures
						- Materials
*/

void setupSponza()
{
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn;
	std::string err;
	bool loaded = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, "res/Sponza/obj/sponza.obj", "res/Sponza/obj/", true);
	assert(loaded);
	std::cout << warn;
	const int matCount = materials.size();

	std::vector<std::vector<VERTEX>> objVertices;
	objVertices.resize(matCount);
	std::vector<std::vector<unsigned int>> objIndices;
	objIndices.resize(matCount);
	for (size_t i = 0; i < shapes.size(); i++) {
		size_t index_offset = 0;
		for (size_t f = 0; f < shapes[i].mesh.num_face_vertices.size(); f++) {
			size_t fnum = shapes[i].mesh.num_face_vertices[f];
			int matIndex = shapes[i].mesh.material_ids[f];
			for (size_t v = 0; v < fnum; v++) {
				tinyobj::index_t idx = shapes[i].mesh.indices[index_offset + v];
				VERTEX vertex;
				vertex.position = { attrib.vertices[3 * idx.vertex_index + 0], attrib.vertices[3 * idx.vertex_index + 1], attrib.vertices[3 * idx.vertex_index + 2], 1.0f };
				vertex.normal = { attrib.normals[3 * idx.normal_index + 0], attrib.normals[3 * idx.normal_index + 1], attrib.normals[3 * idx.normal_index + 2] };
				vertex.uv = { attrib.texcoords[2 * idx.texcoord_index + 0], 1.0f - attrib.texcoords[2 * idx.texcoord_index + 1] };
				vertex.input1 = { 1.0f, 1.0f, 1.0f, 1.0f };

				objIndices[matIndex].push_back((unsigned int)(objVertices[matIndex].size()));
				objVertices[matIndex].push_back(vertex);
			}

			index_offset += fnum;
		}
	}

	for (int i = 0; i < matCount; i++) {
		// Create mesh
		RT64_MESH* mesh = RT64.lib.CreateMesh(RT64.device, RT64_MESH_RAYTRACE_ENABLED | RT64_MESH_RAYTRACE_FAST_TRACE | RT64_MESH_RAYTRACE_COMPACT);
		RT64.lib.SetMesh(mesh, objVertices[i].data(), (int)(objVertices[i].size()), sizeof(VERTEX), objIndices[i].data(), (int)(objIndices[i].size()));

		// Load textures
		std::string diffPath = "res/Sponza/obj/";
		std::string normPath = "res/Sponza/obj/";
		std::string specPath = "res/Sponza/obj/";
		RT64_TEXTURE* diffTex = materials[i].diffuse_texname.empty() ? RT64.textureDif : loadTexturePNG(diffPath.append(materials[i].diffuse_texname.c_str()).c_str());
		RT64_TEXTURE* normalTex = materials[i].bump_texname.empty() ? RT64.textureNrm : loadTexturePNG(normPath.append(materials[i].bump_texname.c_str()).c_str());
		RT64_TEXTURE* specTex = materials[i].specular_highlight_texname.empty() ? RT64.textureSpc : loadTexturePNG(specPath.append(materials[i].specular_highlight_texname.c_str()).c_str());

		// Create instance
		RT64_INSTANCE_DESC instDesc;
		RT64_INSTANCE* instance = RT64.lib.CreateInstance(RT64.scene);
		RT64_MATRIX4 sceneTransform {};
		RT64_MATERIAL mat = RT64.baseMaterial;
		// if (i % 5 == 0) {
		// 	mat.reflectionFactor = 0.250f;
		// }
		const float sceneScale = Sample.sceneScale;
		sceneTransform.m[0][0] = sceneScale;
		sceneTransform.m[1][1] = sceneScale;
		sceneTransform.m[2][2] = sceneScale;
		sceneTransform.m[3][0] = 0.00f;
		sceneTransform.m[3][1] = 0.00f;
		sceneTransform.m[3][2] = 0.00f;
		sceneTransform.m[3][3] = 1.00f;
		instDesc.scissorRect = { 0, 0, 0, 0 };
		instDesc.viewportRect = { 0, 0, 0, 0 };
		instDesc.mesh = mesh;
		instDesc.transform = sceneTransform;
		instDesc.previousTransform = sceneTransform;
		instDesc.diffuseTexture = diffTex;
		instDesc.normalTexture = normalTex;
		instDesc.specularTexture = specTex;
		instDesc.material = mat;
		instDesc.shader = RT64.shader;
		instDesc.flags = 0;
		RT64.lib.SetInstanceDescription(instance, instDesc);
	}

}

int main(int argc, char *argv[]) {
	// Show a basic message to the user so they know what the sample is meant to do.
	// infoMessage(NULL, 
	// 	"This sample application will test if your system has the required hardware to run RT64.\n"
	// 	"If you see some shapes on the screen after clicking the Enter key, then you're good to go!");
	// std::cin.get();
	// Set-up window.
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

	// Create window.
	const int Width = 800;
	const int Height = 600;
    GLFWwindow* window = glfwCreateWindow(Width, Height, "RT64VK Sample", nullptr, nullptr);

	// Create RT64.
	if (!createRT64(window)) {
		errorMessage(nullptr,
			"Failed to initialize RT64! \n"
			"Please make sure your GPU drivers are up to date and the driver supports Vulkan 1.2 \n"
#ifdef _WIN32
			"Windows 10 version 2004 or newer is also required for this feature level to work properly\n"
#else
#endif
			"If you're a mobile user, make sure that the high performance device is selected for this application on your system's settings");

		return 1;
	}

	// Setup scene in RT64.
	setupRT64Scene();
	setupSponza();

	glfwSetKeyCallback(window, keyCallback);
	glfwSetMouseButtonCallback(window, mouseButtonCallBack);

	RT64_VIEW_DESC viewDesc {};
	viewDesc.diSamples = 1;
	viewDesc.giSamples = 1;
	viewDesc.giBounces = 2;
	viewDesc.denoiserEnabled = true;
	viewDesc.maxLights = 12;
	viewDesc.motionBlurStrength = 0.25f;
	viewDesc.tonemapMode = 4;
	viewDesc.tonemapExposure = 2.5f;
	viewDesc.tonemapWhite = 1.0f;
	viewDesc.tonemapBlack = 0.0f;
	viewDesc.tonemapGamma = 1.25f;
	viewDesc.resolutionScale = 1.0f;
	RT64.lib.SetViewDescription(RT64.view, viewDesc);

	// GLFW Window loop.
	while (!glfwWindowShouldClose(window)) {
		// Process any poll evenets.
        glfwPollEvents();
		draw(window);
	}

	destroyRT64();

	// return static_cast<char>(msg.wParam);
}

#else

// Minimal sample that only verifies if a raytracing device can be detected.

int main(int argc, char* argv[]) {
	RT64_LIBRARY lib = RT64_LoadLibrary();
	if (lib.handle == 0) {
		errorMessage(NULL, "Failed to load RT64 library.");
		return 1;
	}

	RT64_DEVICE* device = lib.CreateDevice(0);
	if (device == nullptr) {
		errorMessage(NULL, lib.GetLastError());
		return 1;
	}

	infoMessage(NULL, "Raytracing device was detected!");

	lib.DestroyDevice(device);
	RT64_UnloadLibrary(lib);
	return 0;
}

#endif
