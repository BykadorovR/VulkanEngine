#pragma once
#include "Light.h"
#include "Command.h"
#include "Pipeline.h"
#include "Descriptor.h"
#include "Settings.h"
#include <vector>
#include <memory>
#include "State.h"

class LightManager {
 private:
  int _descriptorPoolSize = 100;
  // if changed have to be change in shaders too
  int _maxDirectionalLights = 2;
  int _maxPointLights = 4;

  std::vector<std::shared_ptr<PointLight>> _pointLights;
  std::vector<std::shared_ptr<DirectionalLight>> _directionalLights;

  std::shared_ptr<State> _state;
  std::shared_ptr<DescriptorPool> _descriptorPool;
  std::shared_ptr<Buffer> _lightDirectionalSSBO = nullptr, _lightPointSSBO = nullptr;
  std::shared_ptr<Buffer> _lightDirectionalSSBOViewProjection = nullptr, _lightPointSSBOViewProjection = nullptr;
  std::shared_ptr<Texture> _stubTexture;
  std::shared_ptr<DescriptorSet> _descriptorSetLight;
  std::shared_ptr<DescriptorSetLayout> _descriptorSetLayoutLight;
  std::shared_ptr<DescriptorSet> _descriptorSetViewProjection;
  std::shared_ptr<DescriptorSetLayout> _descriptorSetLayoutViewProjection;
  // for 2 frames
  std::vector<std::shared_ptr<DescriptorSet>> _descriptorSetDepthTexture;
  std::shared_ptr<DescriptorSetLayout> _descriptorSetLayoutDepthTexture;
  bool _changed = true;

 public:
  LightManager(std::shared_ptr<State> state);
  std::shared_ptr<PointLight> createPointLight();
  std::vector<std::shared_ptr<PointLight>> getPointLights();
  std::shared_ptr<DirectionalLight> createDirectionalLight();
  std::vector<std::shared_ptr<DirectionalLight>> getDirectionalLights();
  std::shared_ptr<DescriptorSetLayout> getDSLLight();
  std::shared_ptr<DescriptorSet> getDSLight();
  std::shared_ptr<DescriptorSetLayout> getDSLViewProjection();
  std::shared_ptr<DescriptorSet> getDSViewProjection();
  std::shared_ptr<DescriptorSetLayout> getDSLShadowTexture();
  std::vector<std::shared_ptr<DescriptorSet>> getDSShadowTexture();
  void draw(int frame);
};