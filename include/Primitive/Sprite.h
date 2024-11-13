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
  std::shared_ptr<DescriptorSet> _descriptorSetColor, _descriptorSetPhong, _descriptorSetPBR;
  std::shared_ptr<DescriptorSet> _descriptorSetCameraFull;
  std::shared_ptr<DescriptorSetLayout> _descriptorSetLayoutNormalsMesh, _descriptorSetLayoutDepth;
  std::shared_ptr<DescriptorSet> _descriptorSetNormalsMesh;
  std::vector<std::vector<std::shared_ptr<DescriptorSet>>> _descriptorSetCameraDepth;
  std::map<MaterialType, std::vector<std::pair<std::string, std::shared_ptr<DescriptorSetLayout>>>>
      _descriptorSetLayout;
  std::vector<std::pair<std::string, std::shared_ptr<DescriptorSetLayout>>> _descriptorSetLayoutNormal;
  std::vector<std::pair<std::string, std::shared_ptr<DescriptorSetLayout>>> _descriptorSetLayoutBRDF;
  std::map<MaterialType, std::shared_ptr<Pipeline>> _pipeline;
  std::map<MaterialType, std::shared_ptr<Pipeline>> _pipelineWireframe;
  std::shared_ptr<RenderPass> _renderPass, _renderPassDepth;
  std::shared_ptr<Pipeline> _pipelineNormal, _pipelineTangent;
  std::shared_ptr<Pipeline> _pipelineDirectional, _pipelinePoint;

  bool _enableShadow = true;
  bool _enableLighting = true;
  bool _enableDepth = true;
  bool _enableHUD = false;
  std::vector<std::vector<std::shared_ptr<UniformBuffer>>> _cameraUBODepth;
  std::shared_ptr<UniformBuffer> _cameraUBOFull;
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
      std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor;
      std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor;
      std::vector<VkDescriptorBufferInfo> bufferInfoCamera(1);
      // write to binding = 0 for vertex shader
      bufferInfoCamera[0].buffer = _cameraUBODepth[d][0]->getBuffer()[currentFrame]->getData();
      bufferInfoCamera[0].offset = 0;
      bufferInfoCamera[0].range = sizeof(BufferMVP);
      bufferInfoColor[0] = bufferInfoCamera;

      // write for binding = 1 for textures
      std::vector<VkDescriptorImageInfo> bufferInfoTexture(1);
      bufferInfoTexture[0].imageLayout = material->getBaseColor()[0]->getImageView()->getImage()->getImageLayout();
      bufferInfoTexture[0].imageView = material->getBaseColor()[0]->getImageView()->getImageView();
      bufferInfoTexture[0].sampler = material->getBaseColor()[0]->getSampler()->getSampler();
      textureInfoColor[1] = bufferInfoTexture;
      _descriptorSetCameraDepth[d][0]->createCustom(currentFrame, bufferInfoColor, textureInfoColor);
    }

    for (int p = 0; p < _engineState->getSettings()->getMaxPointLights(); p++) {
      for (int f = 0; f < 6; f++) {
        std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor;
        std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor;
        std::vector<VkDescriptorBufferInfo> bufferInfoCamera(1);
        // write to binding = 0 for vertex shader
        bufferInfoCamera[0].buffer = _cameraUBODepth[_engineState->getSettings()->getMaxDirectionalLights() + p][f]
                                         ->getBuffer()[currentFrame]
                                         ->getData();
        bufferInfoCamera[0].offset = 0;
        bufferInfoCamera[0].range = sizeof(BufferMVP);
        bufferInfoColor[0] = bufferInfoCamera;

        // write for binding = 1 for textures
        std::vector<VkDescriptorImageInfo> bufferInfoTexture(1);
        bufferInfoTexture[0].imageLayout = material->getBaseColor()[0]->getImageView()->getImage()->getImageLayout();
        bufferInfoTexture[0].imageView = material->getBaseColor()[0]->getImageView()->getImageView();
        bufferInfoTexture[0].sampler = material->getBaseColor()[0]->getSampler()->getSampler();
        textureInfoColor[1] = bufferInfoTexture;
        _descriptorSetCameraDepth[_engineState->getSettings()->getMaxDirectionalLights() + p][f]->createCustom(
            currentFrame, bufferInfoColor, textureInfoColor);
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