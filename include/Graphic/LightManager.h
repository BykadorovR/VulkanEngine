#pragma once
#include "Graphic/Light.h"
#include "Graphic/Material.h"
#include "Utility/EngineState.h"
#include "Utility/Logger.h"
#include "Utility/Settings.h"
#include "Utility/ResourceManager.h"
#include "Vulkan/Command.h"
#include "Vulkan/Pipeline.h"
#include "Vulkan/Descriptor.h"
#include "Graphic/Shadow.h"
#include <vector>
#include <memory>

enum class LightType { DIRECTIONAL = 0, POINT = 1, AMBIENT = 2 };
enum class ShadowAlgorithm { PCF = 0, VSM = 1 };

class LightManager {
 private:
  std::vector<std::shared_ptr<DirectionalLight>> _directionalLights;
  std::vector<std::shared_ptr<PointLight>> _pointLights;
  std::vector<std::shared_ptr<AmbientLight>> _ambientLights;
  std::vector<std::shared_ptr<DirectionalShadow>> _directionalShadows;
  std::vector<std::shared_ptr<PointShadow>> _pointShadows;

  std::shared_ptr<EngineState> _engineState;
  std::vector<std::shared_ptr<Buffer>> _lightDirectionalSSBO, _lightPointSSBO, _lightAmbientSSBO;
  std::shared_ptr<Buffer> _lightDirectionalSSBOStub, _lightPointSSBOStub, _lightAmbientSSBOStub;
  std::vector<std::shared_ptr<Buffer>> _lightDirectionalSSBOViewProjection, _lightPointSSBOViewProjection;
  std::shared_ptr<Buffer> _lightDirectionalSSBOViewProjectionStub, _lightPointSSBOViewProjectionStub;
  std::shared_ptr<Texture> _stubTexture;
  std::shared_ptr<Cubemap> _stubCubemap;
  std::vector<std::shared_ptr<DescriptorSet>> _descriptorSetGlobalPhong, _descriptorSetGlobalPBR,
      _descriptorSetGlobalTerrainPhong, _descriptorSetGlobalTerrainPBR;
  std::shared_ptr<DescriptorSetLayout> _descriptorSetLayoutGlobalPhong, _descriptorSetLayoutGlobalPBR,
      _descriptorSetLayoutGlobalTerrainPhong, _descriptorSetLayoutGlobalTerrainPBR;
  std::vector<std::shared_ptr<Buffer>> _shadowParametersBuffer;
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
  void _setLightDescriptors(int currentFrame);
  void _updateShadowParametersBuffer(int currentFrame);

 public:
  LightManager(std::shared_ptr<ResourceManager> resourceManager, std::shared_ptr<EngineState> engineState);

  // Lights can't be added AFTER draw for current frame, only before draw.
  std::shared_ptr<AmbientLight> createAmbientLight();
  std::vector<std::shared_ptr<AmbientLight>> getAmbientLights();
  std::shared_ptr<PointLight> createPointLight();
  std::shared_ptr<PointShadow> createPointShadow(std::shared_ptr<PointLight> pointLight,
                                                 std::shared_ptr<CommandBuffer> commandBufferTransfer);
  const std::vector<std::shared_ptr<PointLight>>& getPointLights();
  const std::vector<std::shared_ptr<PointShadow>>& getPointShadows();
  void removePointLight(std::shared_ptr<PointLight> pointLight);
  void removePoinShadow(std::shared_ptr<PointShadow> pointShadow);

  std::shared_ptr<DirectionalLight> createDirectionalLight();
  std::shared_ptr<DirectionalShadow> createDirectionalShadow(std::shared_ptr<DirectionalLight> directionalLight,
                                                             std::shared_ptr<CommandBuffer> commandBufferTransfer);
  const std::vector<std::shared_ptr<DirectionalLight>>& getDirectionalLights();
  const std::vector<std::shared_ptr<DirectionalShadow>>& getDirectionalShadows();
  void removeDirectionalLight(std::shared_ptr<DirectionalLight> directionalLight);
  void removeDirectionalShadow(std::shared_ptr<DirectionalShadow> directionalShadow);
  void setShadowAlgorithm(LightType lightType, ShadowAlgorithm shadowAlgorithm);

  std::shared_ptr<DescriptorSetLayout> getDSLGlobalPhong();
  std::shared_ptr<DescriptorSetLayout> getDSLGlobalPBR();
  std::shared_ptr<DescriptorSetLayout> getDSLGlobalTerrainColor();
  std::shared_ptr<DescriptorSetLayout> getDSLGlobalTerrainPhong();
  std::shared_ptr<DescriptorSetLayout> getDSLGlobalTerrainPBR();
  std::shared_ptr<DescriptorSet> getDSGlobalPhong();
  std::shared_ptr<DescriptorSet> getDSGlobalPBR();
  std::shared_ptr<DescriptorSet> getDSGlobalTerrainPhong();
  std::shared_ptr<DescriptorSet> getDSGlobalTerrainPBR();

  void draw(int currentFrame);
};