#pragma once
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include "Graphic/Light.h"
#include "Graphic/Material.h"
#include "Utility/EngineState.h"
#include "Utility/Logger.h"
#include "Utility/Settings.h"
#include "Utility/ResourceManager.h"
#include "Graphic/Shadow.h"

import Buffer;
import Command;
import DescriptorBuffer;
import Texture;

class LightManager {
 private:
  std::vector<std::shared_ptr<DirectionalLight>> _directionalLights;
  std::vector<std::shared_ptr<PointLight>> _pointLights;
  std::vector<std::shared_ptr<AmbientLight>> _ambientLights;
  std::vector<std::shared_ptr<DirectionalShadowResources>> _directionalShadows;
  std::vector<std::shared_ptr<PointShadowResources>> _pointShadows;

  std::shared_ptr<EngineState> _engineState;
  std::vector<std::shared_ptr<RenderGraph::Buffer>> _lightDirectionalSSBO, _lightPointSSBO, _lightAmbientSSBO;
  std::shared_ptr<RenderGraph::Buffer> _lightDirectionalSSBOStub, _lightPointSSBOStub, _lightAmbientSSBOStub;
  std::vector<std::shared_ptr<RenderGraph::Buffer>> _lightDirectionalSSBOViewProjection, _lightPointSSBOViewProjection;
  std::shared_ptr<RenderGraph::Buffer> _lightDirectionalSSBOViewProjectionStub, _lightPointSSBOViewProjectionStub;
  std::shared_ptr<RenderGraph::Texture> _stubTexture;
  std::shared_ptr<Cubemap> _stubCubemap;
  std::vector<std::shared_ptr<RenderGraph::DescriptorSet>> _descriptorSetGlobalPhong, _descriptorSetGlobalPBR,
      _descriptorSetGlobalModelPhong, _descriptorSetGlobalModelPBR, _descriptorSetGlobalTerrainPhong,
      _descriptorSetGlobalTerrainPBR;
  std::shared_ptr<RenderGraph::DescriptorSetLayout> _descriptorSetLayoutGlobalPhong, _descriptorSetLayoutGlobalPBR,
      _descriptorSetLayoutGlobalTerrainPhong, _descriptorSetLayoutGlobalTerrainPBR;
  std::vector<std::shared_ptr<RenderGraph::Buffer>> _shadowParametersBuffer;
  std::map<LightType, ShadowAlgorithm> _shadowAlgorithm = {{LightType::DIRECTIONAL, ShadowAlgorithm::VSM},
                                                           {LightType::POINT, ShadowAlgorithm::PCF}};
  // for 2 frames
  std::mutex _accessMutex;

  std::map<LightType, std::vector<bool>> _changed;
  void _reallocateDirectionalBuffers(int currentFrame);
  void _updateDirectionalBuffers(int currentFrame);
  void _reallocatePointBuffers(int currentFrame);
  void _updatePointBuffers(int currentFrame);
  void _reallocateAmbientBuffers(int currentFrame);
  void _updateAmbientBuffers(int currentFrame);
  void _setLightDescriptors(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer);
  void _updateShadowParametersBuffer(int currentFrame);
  void _addGlobalResources(RenderGraph::DescriptorSet& descriptor, int currentFrame, bool includeAmbient) const;

 public:
  LightManager(std::shared_ptr<ResourceManager> resourceManager,
               std::shared_ptr<EngineState> engineState,
               const RenderGraph::CommandBuffer& commandBuffer);

  // Lights can't be added AFTER draw for current frame, only before draw.
  std::shared_ptr<AmbientLight> createAmbientLight();
  std::vector<std::shared_ptr<AmbientLight>> getAmbientLights();
  std::shared_ptr<PointLight> createPointLight();
  std::shared_ptr<PointShadowResources> createPointShadow(std::shared_ptr<PointLight> pointLight,
                                                          const RenderGraph::CommandBuffer* commandBufferTransfer);
  const std::vector<std::shared_ptr<PointLight>>& getPointLights();
  const std::vector<std::shared_ptr<PointShadowResources>>& getPointShadows();
  void removePointLight(std::shared_ptr<PointLight> pointLight);
  void removePointShadow(std::shared_ptr<PointShadowResources> pointShadow);

  std::shared_ptr<DirectionalLight> createDirectionalLight();
  std::shared_ptr<DirectionalShadowResources> createDirectionalShadow(
      std::shared_ptr<DirectionalLight> directionalLight,
      const RenderGraph::CommandBuffer* commandBufferTransfer);
  const std::vector<std::shared_ptr<DirectionalLight>>& getDirectionalLights();
  const std::vector<std::shared_ptr<DirectionalShadowResources>>& getDirectionalShadows();
  void removeDirectionalLight(std::shared_ptr<DirectionalLight> directionalLight);
  void removeDirectionalShadow(std::shared_ptr<DirectionalShadowResources> directionalShadow);
  void setShadowAlgorithm(LightType lightType, ShadowAlgorithm shadowAlgorithm);

  std::shared_ptr<RenderGraph::DescriptorSetLayout> getDSLGlobalPhong();
  std::shared_ptr<RenderGraph::DescriptorSetLayout> getDSLGlobalPBR();
  std::shared_ptr<RenderGraph::DescriptorSetLayout> getDSLGlobalTerrainColor();
  std::shared_ptr<RenderGraph::DescriptorSetLayout> getDSLGlobalTerrainPhong();
  std::shared_ptr<RenderGraph::DescriptorSetLayout> getDSLGlobalTerrainPBR();
  std::shared_ptr<RenderGraph::DescriptorSet> getDSGlobalPhong();
  std::shared_ptr<RenderGraph::DescriptorSet> getDSGlobalPBR();
  std::shared_ptr<RenderGraph::DescriptorSet> getDSGlobalModelPhong();
  std::shared_ptr<RenderGraph::DescriptorSet> getDSGlobalModelPBR();
  std::shared_ptr<RenderGraph::DescriptorSet> getDSGlobalTerrainPhong();
  std::shared_ptr<RenderGraph::DescriptorSet> getDSGlobalTerrainPBR();

  void draw(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer);
  void update(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer);
};
