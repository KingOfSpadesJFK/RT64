/*
*   RT64VK
*/

#pragma once

#include "rt64_common.h"

namespace RT64 {
	class Mesh;
	class Scene;
	class Shader;
	class Texture;

	class Instance {
		private:
			Scene* scene;
			Mesh* mesh;
			Texture* diffuseTexture;
			Texture* normalTexture;
			Texture* specularTexture;
			glm::mat4 transform;
			glm::mat4 previousTransform;
			RT64_MATERIAL material;
			Shader* shader;
			RT64_RECT scissorRect;
			RT64_RECT viewportRect;
			unsigned int flags;
		public:
			Instance(Scene* scene);
			virtual ~Instance();
			void setMesh(Mesh* mesh);
			Mesh* getMesh() const;
			void setMaterial(const RT64_MATERIAL& material);
			const RT64_MATERIAL& getMaterial() const;
			void setShader(Shader* shader);
			Shader* getShader() const;
			void setDiffuseTexture(Texture* texture);
			Texture* getDiffuseTexture() const;
			void setNormalTexture(Texture* texture);
			Texture* getNormalTexture() const;
			void setSpecularTexture(Texture* texture);
			Texture* getSpecularTexture() const;
			void setTransform(float m[4][4]);
			glm::mat4 getTransform() const;
			void setPreviousTransform(float m[4][4]);
			glm::mat4 getPreviousTransform() const;
			void setScissorRect(const RT64_RECT &rect);
			RT64_RECT getScissorRect() const;
			bool hasScissorRect() const;
			void setViewportRect(const RT64_RECT &rect);
			RT64_RECT getViewportRect() const;
			bool hasViewportRect() const;
			void setFlags(int v);
			unsigned int getFlags() const;
	};
};