#pragma once
#include "State.h"

enum class MaterialType { PHONG, PBR };

class Material {
 protected:
  struct AlphaCutoff {
    bool alphaMask = false;
    float alphaCutoff = 0.f;
  };

  AlphaCutoff _alphaCutoff;
  bool _doubleSided = false;

  std::shared_ptr<Texture> _stubTextureZero, _stubTextureOne;

  std::shared_ptr<DescriptorSetLayout> _descriptorSetLayoutCoefficients, _descriptorSetLayoutTextures;
  std::shared_ptr<DescriptorSetLayout> _descriptorSetLayoutAlphaCutoff;
  std::shared_ptr<DescriptorSet> _descriptorSetCoefficients, _descriptorSetTextures;
  std::shared_ptr<DescriptorSet> _descriptorSetAlphaCutoff;
  std::shared_ptr<UniformBuffer> _uniformBufferCoefficients;
  std::shared_ptr<UniformBuffer> _uniformBufferAlphaCutoff;
  std::shared_ptr<State> _state;
  std::vector<bool> _changedTexture;
  std::vector<bool> _changedCoefficients;
  std::mutex _accessMutex;

  void _updateAlphaCutoffDescriptors(int currentFrame);
  virtual void _updateTextureDescriptors(int currentFrame) = 0;
  virtual void _updateCoefficientDescriptors(int currentFrame) = 0;

 public:
  Material(std::shared_ptr<CommandBuffer> commandBufferTransfer, std::shared_ptr<State> state);

  void setDoubleSided(bool doubleSided);
  void setAlphaCutoff(bool alphaCutoff, float alphaMask);
  bool getDoubleSided();

  std::shared_ptr<DescriptorSet> getDescriptorSetAlphaCutoff();
  std::shared_ptr<DescriptorSetLayout> getDescriptorSetLayoutAlphaCutoff();
  std::shared_ptr<DescriptorSet> getDescriptorSetCoefficients(int currentFrame);
  std::shared_ptr<DescriptorSetLayout> getDescriptorSetLayoutCoefficients();
  std::shared_ptr<DescriptorSet> getDescriptorSetTextures(int currentFrame);
  std::shared_ptr<DescriptorSetLayout> getDescriptorSetLayoutTextures();
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

  std::shared_ptr<Texture> _textureColor;
  std::shared_ptr<Texture> _textureNormal;
  std::shared_ptr<Texture> _textureMetallicRoughness;
  std::shared_ptr<Texture> _textureOccluded;
  std::shared_ptr<Texture> _textureEmissive;

  Coefficients _coefficients;

  void _updateTextureDescriptors(int currentFrame);
  void _updateCoefficientDescriptors(int currentFrame);

 public:
  MaterialPBR(std::shared_ptr<CommandBuffer> commandBufferTransfer, std::shared_ptr<State> state);

  void setBaseColor(std::shared_ptr<Texture> color);
  void setNormal(std::shared_ptr<Texture> normal);
  void setMetallicRoughness(std::shared_ptr<Texture> metallicRoughness);
  void setOccluded(std::shared_ptr<Texture> occluded);
  void setEmissive(std::shared_ptr<Texture> emissive);
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

  std::shared_ptr<Texture> _textureColor;
  std::shared_ptr<Texture> _textureNormal;

  Coefficients _coefficients;

  void _updateTextureDescriptors(int currentFrame);
  void _updateCoefficientDescriptors(int currentFrame);

 public:
  MaterialPhong(std::shared_ptr<CommandBuffer> commandBufferTransfer, std::shared_ptr<State> state);

  void setBaseColor(std::shared_ptr<Texture> color);
  void setNormal(std::shared_ptr<Texture> normal);
  void setCoefficients(glm::vec3 ambient, glm::vec3 diffuse, glm::vec3 specular, float shininess);
};