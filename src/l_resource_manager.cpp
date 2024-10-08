#include "l_resource_manager.h"
#include "assimp/Importer.hpp"
#include "assimp/material.h"
#include "assimp/mesh.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "assimp/types.h"
#include "l_common.h"
#include "l_entity_system.h"
#include "l_math.h"
#include "l_model.h"
#include "l_shader.h"
#include "l_texture.h"
#include <array>
#include <cassert>
#include <cfloat>
#include <fstream>
#include <iostream>
#include <memory>
#include <unordered_map>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace lain
{
  namespace resource_manager {
    std::unordered_map<i32, std::unique_ptr<shader>> _shaders;
    std::unordered_map<i32, std::unique_ptr<texture>> _textures;
    std::unordered_map<model_type, std::unique_ptr<model>> _models;
    std::unordered_map<entity_id, model_type> _entityModelRelationship;
    std::unordered_map<std::string, mesh_texture> _meshTexturesCache;

    static bool ShaderHasCompilationErrors(u32 program, shader_type type);
    static u32 CompileAndLinkShaders(std::filesystem::path const& vertex, std::filesystem::path const& fragment);
    static std::vector<mesh_texture> LoadMaterialTextures(aiMaterial* material, aiTextureType type, std::string const& typeName, model* model);
    static mesh ProcessMesh(aiMesh* aiMesh, aiScene const* scene, model* model);
    static void ProcessNode(aiNode* node, aiScene const* scene, model* model);
    static std::filesystem::path GetPathFromModelType(model_type type);
    static void LoadModels();

    void Initialise()
    {
      // ---------------------------------------------------------------------------------------------
      // shaders
      // ---------------------------------------------------------------------------------------------
      u32 id{CompileAndLinkShaders("./res/shaders/LevelEditor_ModelWithTextures.vert",
				   "./res/shaders/LevelEditor_ModelWithTextures.frag")};

      assert(id != 0 && "couldn't create shader for models with textures in object editor");

      _shaders[kLevelEditorModelWithTextureShaderId] = std::make_unique<shader>(id);

      id = CompileAndLinkShaders("./res/shaders/LevelEditor_ModelWithoutTextures.vert",
				 "./res/shaders/LevelEditor_ModelWithoutTextures.frag");

      assert(id != 0 && "couldn't create shader for models without textures in object editor");

      _shaders[kLevelEditorModelWithoutTextureShaderId] = std::make_unique<shader>(id);

      id = CompileAndLinkShaders("./res/shaders/Primitive.vert", "./res/shaders/Primitive.frag");

      assert(id != 0 && "couldn't create primitive shader");

      _shaders[kPrimitiveShaderId] = std::make_unique<shader>(id);

      //
      // Load every model here, don't lazy load them. Reasons:
      //
      // 1) Try to do allocations always at startup and minimise doing them while the game is running.
      // 2) To load levels from files, it's very useful to have models already loaded.
      //
      LoadModels();
    }

    bool LoadTextureFromFile(std::filesystem::path const& file,
			     bool const flip,
			     i32 const wrapS,
			     i32 const wrapT,
			     i32 const id)
    {
      i32 width, height, channels;

      if (flip) {
	stbi_set_flip_vertically_on_load(true);
      }

      unsigned char* data{stbi_load(file.c_str(), &width, &height, &channels, 0)};

      if (data == nullptr) {
	std::cerr << __FUNCTION__ << ": couldn't load image " << file << '\n';
	return false;
      }

      GLenum format;

      switch (channels) {
      case 1:
	format = GL_RED;
	break;
      case 2:
	format = GL_RG;
	break;
      case 3:
	format = GL_RGB;
	break;
      case 4:
	format = GL_RGBA;
	break;
      }

      // don't assume dimensions are multiple of 4
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      u32 glTexId;
      glGenTextures(1, &glTexId);
      glBindTexture(GL_TEXTURE_2D, glTexId);
      glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
      glGenerateMipmap(GL_TEXTURE_2D);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glBindTexture(GL_TEXTURE_2D, 0);

      stbi_image_free(data);

      if (flip) {
	stbi_set_flip_vertically_on_load(false);
      }

      _textures[id] = std::make_unique<texture>(glTexId, width, height, channels);

      return true;
    }

    u32 LoadTextureFromFile(std::filesystem::path const& file)
    {
      i32 width, height, channels;

      unsigned char* data{stbi_load(file.c_str(), &width, &height, &channels, 0)};

      if (data == nullptr) {
	std::cerr << __FUNCTION__ << ": couldn't load image " << file << '\n';
	return 0;
      }

      GLenum format;

      switch (channels) {
      case 1:
	format = GL_RED;
	break;
      case 2:
	format = GL_RG;
	break;
      case 3:
	format = GL_RGB;
	break;
      case 4:
	format = GL_RGBA;
	break;
      }

      // don't assume dimensions are multiple of 4
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      u32 glTexId;
      glGenTextures(1, &glTexId);
      glBindTexture(GL_TEXTURE_2D, glTexId);
      glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
      glGenerateMipmap(GL_TEXTURE_2D);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glBindTexture(GL_TEXTURE_2D, 0);

      stbi_image_free(data);

      return glTexId;
    }

    void AddEntityModelRelationship(entity_id const id, model_type const type)
    {
      _entityModelRelationship[id] = type;
    }

    void RemoveEntityModelRelationship(entity_id const id)
    {
      auto it = _entityModelRelationship.find(id);
      _entityModelRelationship.erase(it);
    }

    model const* GetModelDataFromEntity(entity_id const id)
    {
      return _models.at(_entityModelRelationship.at(id)).get();
    }

    shader const* GetShader(i32 const id)
    {
      return _shaders.at(id).get();
    }

    texture const* GetTexture(i32 const id)
    {
      return _textures.at(id).get();
    }

    shader CreatePrimitiveVAO(std::vector<f32> const& vertices, GLenum const usage)
    {
      u32 vao, vbo;

      glGenVertexArrays(1, &vao);
      glGenBuffers(1, &vbo);

      glBindVertexArray(vao);

      glBindBuffer(GL_ARRAY_BUFFER, vbo);
      glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(f32), vertices.data(), usage);

      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(f32) * 3, reinterpret_cast<void*>(0));

      return shader(0, vao, vbo, 0);
    }

    shader CreateCubeVAO(std::vector<f32> const& vertices,
			 GLenum const usage,
			 std::vector<u32> const& indices)
    {
      u32 vao, vbo, ebo;

      glGenVertexArrays(1, &vao);
      glGenBuffers(1, &vbo);
      glGenBuffers(1, &ebo);

      glBindVertexArray(vao);

      glBindBuffer(GL_ARRAY_BUFFER, vbo);
      glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(f32), vertices.data(), usage);

      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(u32), indices.data(), usage);

      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(f32) * 3, reinterpret_cast<void*>(0));

      return shader(0, vao, vbo, ebo);
    }

    void LoadModel(model_type const type)
    {
      if (_models.find(type) != _models.end()) {
	return;
      }

      auto newModel = std::make_unique<model>();

      std::filesystem::path path{GetPathFromModelType(type)};

      Assimp::Importer importer;

      aiScene const* scene{importer.ReadFile(path.c_str(), aiProcess_Triangulate | aiProcess_FlipUVs)};

      if (scene == nullptr || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || scene->mRootNode == nullptr) {
	std::cerr << __FUNCTION__ << ": couldn't load model: " << importer.GetErrorString() << '\n';
	assert(false && "couldn't load model");
      }

      newModel->_directory = path.parent_path();

      ProcessNode(scene->mRootNode, scene, newModel.get());

      _models[type] = std::move(newModel);
    }

    model_type GetModelType(entity_id const id)
    {
      return _entityModelRelationship[id];
    }

    static bool ShaderHasCompilationErrors(u32 program, shader_type type)
    {
      i32 success{0};
      std::array<char, 512> log{'\0'};

      switch (type) {
      case shader_type::vertex:
	glGetShaderiv(program, GL_COMPILE_STATUS, &success);
	if (success != GL_TRUE) {
	  glGetShaderInfoLog(program, log.size(), nullptr, log.data());
	  std::cerr << __FUNCTION__ << ": couldn't compile vertex shader:" << log.data() << '\n';
	  return true;
	}
	break;
      case shader_type::fragment:
	glGetShaderiv(program, GL_COMPILE_STATUS, &success);
	if (success != GL_TRUE) {
	  glGetShaderInfoLog(program, log.size(), nullptr, log.data());
	  std::cerr << __FUNCTION__ << ": couldn't compile fragment shader: " << log.data() << '\n';
	  return true;
	}
	break;
      case shader_type::program:
	glGetProgramiv(program, GL_LINK_STATUS, &success);
	if (success != GL_TRUE) {
	  glGetProgramInfoLog(program, log.size(), nullptr, log.data());
	  std::cerr << __FUNCTION__ << ": couldn't link program: " << log.data() << '\n';
	  return true;
	}
	break;
      }

      return false;
    }

    static u32 CompileAndLinkShaders(std::filesystem::path const& vertex, std::filesystem::path const& fragment)
    {
      std::ifstream vertfs(vertex);
      std::ifstream fragfs(fragment);

      if (!vertfs) {
	std::cerr << __FUNCTION__ << ": couldn't open vertex file: " << vertex << '\n';
	return false;
      }

      if (!fragfs) {
	std::cerr << __FUNCTION__ << ": couldn't open fragment file: " << fragment << '\n';
	return false;
      }

      std::stringstream vertss;
      std::stringstream fragss;
      vertss << vertfs.rdbuf();
      fragss << fragfs.rdbuf();

      std::string const vertcode{vertss.str()};
      std::string const fragcode{fragss.str()};

      char const* vertcodec{vertcode.c_str()};
      char const* fragcodec{fragcode.c_str()};

      u32 vertexShaderId{glCreateShader(GL_VERTEX_SHADER)};
      glShaderSource(vertexShaderId, 1, &vertcodec, nullptr);
      glCompileShader(vertexShaderId);
      if (ShaderHasCompilationErrors(vertexShaderId, shader_type::vertex)) {
	glDeleteShader(vertexShaderId);
	return 0;
      }

      u32 fragmentShaderId{glCreateShader(GL_FRAGMENT_SHADER)};
      glShaderSource(fragmentShaderId, 1, &fragcodec, nullptr);
      glCompileShader(fragmentShaderId);
      if (ShaderHasCompilationErrors(fragmentShaderId, shader_type::fragment)) {
	glDeleteShader(vertexShaderId);
	glDeleteShader(fragmentShaderId);
	return 0;
      }

      u32 shaderProgram{glCreateProgram()};
      glAttachShader(shaderProgram, vertexShaderId);
      glAttachShader(shaderProgram, fragmentShaderId);
      glLinkProgram(shaderProgram);
      if (ShaderHasCompilationErrors(shaderProgram, shader_type::program)) {
	glDeleteShader(vertexShaderId);
	glDeleteShader(fragmentShaderId);
	return 0;
      }

      glDeleteShader(vertexShaderId);
      glDeleteShader(fragmentShaderId);

      return shaderProgram;
    }

    static std::vector<mesh_texture> LoadMaterialTextures(aiMaterial* material,
							  aiTextureType type,
							  std::string const& typeName,
							  model* model)
    {
      std::vector<mesh_texture> textures;

      for (u32 i{0}; i < material->GetTextureCount(type); ++i) {
	aiString str;
	material->GetTexture(type, i, &str);

	auto it = _meshTexturesCache.find(str.C_Str());

	if (it != _meshTexturesCache.end()) {
	  textures.push_back(it->second);
	  continue;
	}

	std::filesystem::path filepath{model->_directory};
	filepath /= str.C_Str();

	u32 const textureId{resource_manager::LoadTextureFromFile(filepath)};

	if (textureId == 0) {
	  std::cerr << __FUNCTION__ << ": couldn't load texture from file " << filepath << '\n';
	  continue;
	}

	mesh_texture const newTexture{textureId, typeName, str.C_Str()};

	_meshTexturesCache[str.C_Str()] = newTexture;

	textures.push_back(newTexture);
      }

      return textures;
    }

    static mesh ProcessMesh(aiMesh* aiMesh, aiScene const* scene, model* model)
    {
      aabb aabb{glm::vec3(FLT_MAX), glm::vec3(-FLT_MAX)};
      std::vector<vertex_data> vertices;
      std::vector<u32> indices;
      std::vector<mesh_texture> textures;
      bool const meshHasTexCoords{aiMesh->mTextureCoords[0] != 0};

      for (u32 i{0}; i < aiMesh->mNumVertices; ++i) {
	glm::vec3 position, normal;
	glm::vec2 texCoords{0.f};

	position.x = aiMesh->mVertices[i].x;
	position.y = aiMesh->mVertices[i].y;
	position.z = aiMesh->mVertices[i].z;

	if (aabb._min.x == FLT_MAX) {
	  aabb._min = position;
	  aabb._max = position;
	} else {
	  aabb._min = glm::min(position, aabb._min);
	  aabb._max = glm::max(position, aabb._max);
	}

	normal.x = aiMesh->mNormals[i].x;
	normal.y = aiMesh->mNormals[i].y;
	normal.z = aiMesh->mNormals[i].z;

	if (meshHasTexCoords) {
	  texCoords.x = aiMesh->mTextureCoords[0][i].x;
	  texCoords.y = aiMesh->mTextureCoords[0][i].y;
	}

	vertices.emplace_back(vertex_data{position, normal, texCoords});
      }

      for (u32 i{0}; i < aiMesh->mNumFaces; ++i) {
	aiFace face{aiMesh->mFaces[i]};

	for (u32 j{0}; j < face.mNumIndices; ++j) {
	  indices.push_back(face.mIndices[j]);
	}
      }

      aiMaterial* material{scene->mMaterials[aiMesh->mMaterialIndex]};

      aiColor3D aiColour{1.f};
      material->Get(AI_MATKEY_COLOR_DIFFUSE, aiColour);

      // TODO: add specular as well
      glm::vec3 diffuseColour{aiColour.r, aiColour.g, aiColour.b};

      std::vector<mesh_texture> diffuseMaps{LoadMaterialTextures(material, aiTextureType_DIFFUSE, "textureDiffuse", model)};

      textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());

      std::vector<mesh_texture> specularMaps{LoadMaterialTextures(material, aiTextureType_SPECULAR, "textureSpecular", model)};

      textures.insert(textures.end(), specularMaps.begin(), specularMaps.end());

      return mesh{std::move(vertices), std::move(indices), std::move(textures), diffuseColour, std::move(aabb)};
    }

    static void ProcessNode(aiNode* node, aiScene const* scene, model* model)
    {
      for (u32 i{0}; i < node->mNumMeshes; ++i) {
	model->_meshes.emplace_back(ProcessMesh(scene->mMeshes[node->mMeshes[i]], scene, model));
      }

      for (u32 i{0}; i < node->mNumChildren; ++i) {
	ProcessNode(node->mChildren[i], scene, model);
      }
    }

    static std::filesystem::path GetPathFromModelType(model_type type)
    {
      switch (type) {
      case model_type::ball:
	return std::filesystem::path("./res/models/ball.obj");
      case model_type::maze:
	return std::filesystem::path("./res/models/maze.obj");
      default:
	return "";
      }
    }

    static void LoadModels()
    {
      //
      // TODO: is there a way to freaking do this automatically?
      //
      LoadModel(model_type::maze);
      LoadModel(model_type::ball);
    }
  };
};
