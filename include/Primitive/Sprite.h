#pragma once
#include "Vulkan/Buffer.h"
#include "Vulkan/Pipeline.h"
#include "Vulkan/Command.h"
#include "Utility/Settings.h"
#include "Utility/GameState.h"
#include "Graphic/Camera.h"
#include "Graphic/LightManager.h"
#include "Graphic/Material.h"
#include "Primitive/Mesh.h"
#include "Primitive/Drawable.h"

class Sprite : public Drawable, public Shadowable {
 private:
  std::shared_ptr<EngineState> _engineState;
  std::shared_ptr<GameState> _gameState;
  std::vector<bool> _changedMaterialRender, _changedMaterialShadow;
  std::mutex _updateShadow;
  std::vector<std::shared_ptr<DescriptorSet>> _descriptorSetColor, _descriptorSetPhong, _descriptorSetPBR;
  std::vector<std::shared_ptr<DescriptorSet>> _descriptorSetCameraFull;
  std::vector<std::shared_ptr<DescriptorSet>> _descriptorSetNormalsMesh;
  std::vector<std::vector<std::vector<std::shared_ptr<DescriptorSet>>>> _descriptorSetCameraDepth;
  std::shared_ptr<DescriptorSetLayout> _descriptorSetLayoutNormalsMesh, _descriptorSetLayoutDepth;
  std::map<MaterialType, std::vector<std::pair<std::string, std::shared_ptr<DescriptorSetLayout>>>>
      _descriptorSetLayout;
  std::vector<std::pair<std::string, std::shared_ptr<DescriptorSetLayout>>> _descriptorSetLayoutNormal;
  std::vector<std::pair<std::string, std::shared_ptr<DescriptorSetLayout>>> _descriptorSetLayoutBRDF;
  std::map<MaterialType, std::shared_ptr<PipelineGraphic>> _pipeline;
  std::map<MaterialType, std::shared_ptr<PipelineGraphic>> _pipelineWireframe;
  std::shared_ptr<PipelineGraphic> _pipelineNormal, _pipelineTangent;
  std::shared_ptr<PipelineGraphic> _pipelineDirectional, _pipelinePoint;
  std::shared_ptr<Shader> _shaderNormal, _shaderTangent, _shaderDirectional, _shaderPoint;
  std::map<MaterialType, std::shared_ptr<Shader>> _shader;

  bool _enableShadow = true;
  bool _enableLighting = true;
  bool _enableDepth = true;
  bool _enableHUD = false;
  std::vector<std::vector<std::vector<std::shared_ptr<Buffer>>>> _cameraUBODepth;
  std::vector<std::shared_ptr<Buffer>> _cameraUBOFull;
  std::shared_ptr<Material> _material;
  std::shared_ptr<MaterialPhong> _defaultMaterialPhong;
  std::shared_ptr<MaterialPBR> _defaultMaterialPBR;
  std::shared_ptr<MaterialColor> _defaultMaterialColor;
  std::shared_ptr<MeshStatic2D> _mesh;
  MaterialType _materialType = MaterialType::PHONG;
  DrawType _drawType = DrawType::FILL;

  void _updateColorDescriptor();
  void _updatePhongDescriptor();
  void _updatePBRDescriptor();

  // we pass base color to shader so alpha part of texture doesn't cast a shadow
  template <class T>
  void _updateShadowDescriptor(std::shared_ptr<T> material) {
    int currentFrame = _engineState->getFrameInFlight();
    for (int d = 0; d < _engineState->getSettings()->getMaxDirectionalLights(); d++) {
      std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor = {
          {0,
           {{.buffer = _cameraUBODepth[currentFrame][d][0]->getData(),
             .offset = 0,
             .range = _cameraUBODepth[currentFrame][d][0]->getSize()}}}};
      std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor = {
          {1,
           {{.sampler = material->getBaseColor()[0]->getSampler()->getSampler(),
             .imageView = material->getBaseColor()[0]->getImageView()->getImageView(),
             .imageLayout = material->getBaseColor()[0]->getImageView()->getImage()->getImageLayout()}}}};
      _descriptorSetCameraDepth[currentFrame][d][0]->createCustom(bufferInfoColor, textureInfoColor);
    }

    for (int p = 0; p < _engineState->getSettings()->getMaxPointLights(); p++) {
      for (int f = 0; f < 6; f++) {
        std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor = {
            {0,
             {{.buffer = _cameraUBODepth[currentFrame][_engineState->getSettings()->getMaxDirectionalLights() + p][f]
                             ->getData(),
               .offset = 0,
               .range = _cameraUBODepth[currentFrame][_engineState->getSettings()->getMaxDirectionalLights() + p][f]
                            ->getSize()}}}};
        std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor = {
            {1,
             {{.sampler = material->getBaseColor()[0]->getSampler()->getSampler(),
               .imageView = material->getBaseColor()[0]->getImageView()->getImageView(),
               .imageLayout = material->getBaseColor()[0]->getImageView()->getImage()->getImageLayout()}}}};
        _descriptorSetCameraDepth[currentFrame][_engineState->getSettings()->getMaxDirectionalLights() + p][f]
            ->createCustom(bufferInfoColor, textureInfoColor);
      }
    }
  }

 public:
  Sprite(std::shared_ptr<CommandBuffer> commandBufferTransfer,
         std::shared_ptr<GameState> gameState,
         std::shared_ptr<EngineState> engineState);
  void enableShadow(bool enable);
  void enableLighting(bool enable);
  void enableDepth(bool enable);
  void enableHUD(bool enable);
  bool isDepthEnabled();

  void setMaterial(std::shared_ptr<MaterialPBR> material);
  void setMaterial(std::shared_ptr<MaterialPhong> material);
  void setMaterial(std::shared_ptr<MaterialColor> material);
  MaterialType getMaterialType();
  void setDrawType(DrawType drawType);
  DrawType getDrawType();

  void draw(std::shared_ptr<CommandBuffer> commandBuffer) override;
  void drawShadow(LightType lightType, int lightIndex, int face, std::shared_ptr<CommandBuffer> commandBuffer) override;
};