#pragma once
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include "Utility/EngineState.h"
#include "Primitive/Cubemap.h"

import Buffer;
import Command;
import DescriptorBuffer;
import Texture;

enum class MaterialType { PHONG, PBR, COLOR };

enum class MaterialTarget { SIMPLE = 1, TERRAIN = 4 };

enum class MaterialTexture {
  COLOR = 0,
  NORMAL,
  SPECULAR,
  METALLIC,
  ROUGHNESS,
  OCCLUSION,
  EMISSIVE,
  IBL_DIFFUSE,
  IBL_SPECULAR,
  BRDF_SPECULAR
};

struct MaterialInfoUpdate {
  int frameInFlight;
  std::map<MaterialTexture, std::uint32_t> materialInfo;
};

class Material {
 protected:
  struct AlphaCutoff {
    bool alphaMask = false;
    float alphaCutoff = 0.f;
  };
  AlphaCutoff _alphaCutoff;
  bool _doubleSided = false;

  std::vector<std::shared_ptr<RenderGraph::Buffer>> _uniformBufferCoefficients;
  std::vector<std::shared_ptr<RenderGraph::Buffer>> _uniformBufferAlphaCutoff;
  std::map<std::shared_ptr<RenderGraph::DescriptorHandler>, MaterialInfoUpdate> _descriptorsUpdate;
  std::shared_ptr<EngineState> _engineState;
  std::map<MaterialTexture, std::vector<bool>> _changedTextures;
  std::vector<bool> _changedCoefficients;
  std::vector<bool> _changedAlpha;
  std::mutex _mutex;
  std::map<MaterialTexture, std::vector<std::shared_ptr<RenderGraph::Texture>>> _textures;

  void _updateAlphaCutoffDescriptors(int currentFrame);
  void _updateDescriptor(int currentFrame, MaterialTexture type, const RenderGraph::CommandBuffer& commandBuffer);
  virtual void _updateCoefficientBuffer(int currentFrame) = 0;

 public:
  explicit Material(std::shared_ptr<EngineState> engineState);
  void setDoubleSided(bool doubleSided);
  void setAlphaCutoff(bool alphaMask, float alphaCutoff);
  bool getDoubleSided();
  std::vector<std::shared_ptr<RenderGraph::Buffer>> getBufferCoefficients();
  std::vector<std::shared_ptr<RenderGraph::Buffer>> getBufferAlphaCutoff();
  void registerUpdate(std::shared_ptr<RenderGraph::DescriptorHandler> descriptor, MaterialInfoUpdate info);
  void unregisterUpdate(std::shared_ptr<RenderGraph::DescriptorHandler> descriptor);
  void update(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer);
};

class MaterialColor : public Material {
 private:
  MaterialTarget _target;
  void _updateCoefficientBuffer(int currentFrame) override;

 public:
  // number of textures color
  MaterialColor(MaterialTarget target, std::shared_ptr<EngineState> engineState);
  const std::vector<std::shared_ptr<RenderGraph::Texture>> getBaseColor();

  void setBaseColor(std::vector<std::shared_ptr<RenderGraph::Texture>> color);
};

class MaterialPBR : public Material {
 private:
  struct Coefficients {
    float metallicFactor{1};
    float roughnessFactor{1};
    // occludedColor = lerp(color, color * <sampled occlusion texture value>, <occlusion strength>)
    float occlusionStrength{0};
    alignas(16) glm::vec3 emissiveFactor{0};
  };
  MaterialTarget _target;

  Coefficients _coefficients;

  void _updateCoefficientBuffer(int currentFrame) override;

 public:
  // number of textures color, normal, metallic, roughness, occluded, emissive
  MaterialPBR(MaterialTarget target, std::shared_ptr<EngineState> engineState);
  const std::vector<std::shared_ptr<RenderGraph::Texture>> getBaseColor();
  const std::vector<std::shared_ptr<RenderGraph::Texture>> getNormal();
  const std::vector<std::shared_ptr<RenderGraph::Texture>> getMetallic();
  const std::vector<std::shared_ptr<RenderGraph::Texture>> getRoughness();
  const std::vector<std::shared_ptr<RenderGraph::Texture>> getOccluded();
  const std::vector<std::shared_ptr<RenderGraph::Texture>> getEmissive();
  std::shared_ptr<RenderGraph::Texture> getDiffuseIBL();
  std::shared_ptr<RenderGraph::Texture> getSpecularIBL();
  std::shared_ptr<RenderGraph::Texture> getSpecularBRDF();

  void setBaseColor(std::vector<std::shared_ptr<RenderGraph::Texture>> color);
  void setNormal(std::vector<std::shared_ptr<RenderGraph::Texture>> normal);
  void setMetallic(std::vector<std::shared_ptr<RenderGraph::Texture>> metallic);
  void setRoughness(std::vector<std::shared_ptr<RenderGraph::Texture>> roughness);
  void setOccluded(std::vector<std::shared_ptr<RenderGraph::Texture>> occluded);
  void setEmissive(std::vector<std::shared_ptr<RenderGraph::Texture>> emissive);
  void setDiffuseIBL(std::shared_ptr<RenderGraph::Texture> diffuseIBL);
  void setSpecularIBL(std::shared_ptr<RenderGraph::Texture> specularIBL,
                      std::shared_ptr<RenderGraph::Texture> specularBRDF);
  void setCoefficients(float metallicFactor, float roughnessFactor, float occlusionStrength, glm::vec3 emissiveFactor);
};

class MaterialPhong : public Material {
 private:
  struct Coefficients {
    alignas(16) glm::vec3 _ambient{0.2f};
    alignas(16) glm::vec3 _diffuse{1.f};
    alignas(16) glm::vec3 _specular{0.5f};
    float _shininess{64.f};
  };

  MaterialTarget _target;
  Coefficients _coefficients;

  void _updateCoefficientBuffer(int currentFrame) override;

 public:
  // number of textures color, normal and specular
  MaterialPhong(MaterialTarget target, std::shared_ptr<EngineState> engineState);
  const std::vector<std::shared_ptr<RenderGraph::Texture>> getBaseColor();
  const std::vector<std::shared_ptr<RenderGraph::Texture>> getNormal();
  const std::vector<std::shared_ptr<RenderGraph::Texture>> getSpecular();

  void setBaseColor(std::vector<std::shared_ptr<RenderGraph::Texture>> color);
  void setNormal(std::vector<std::shared_ptr<RenderGraph::Texture>> normal);
  void setSpecular(std::vector<std::shared_ptr<RenderGraph::Texture>> specular);
  void setCoefficients(glm::vec3 ambient, glm::vec3 diffuse, glm::vec3 specular, float shininess);
};
