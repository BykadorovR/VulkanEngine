#pragma once
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "Primitive/Drawable.h"
#include "Utility/Settings.h"
#include "Utility/GameState.h"
#include "Graphic/Camera.h"
#include "Graphic/LightManager.h"
#include "Graphic/Material.h"
#include "Primitive/Mesh.h"

import Buffer;
import Command;
import DescriptorBuffer;
import Pipeline;
import Shader;

class Sprite : public Drawable, public Shadowable {
 private:
  std::shared_ptr<EngineState> _engineState;
  std::shared_ptr<GameState> _gameState;
  std::vector<bool> _changedMaterialRender, _changedMaterialShadow;
  std::mutex _updateShadow;
  std::vector<std::shared_ptr<RenderGraph::DescriptorSet>> _descriptorSetColor, _descriptorSetPhong, _descriptorSetPBR;
  std::vector<std::shared_ptr<RenderGraph::DescriptorSet>> _descriptorSetNormalsMesh;
  std::vector<std::vector<std::vector<std::shared_ptr<RenderGraph::DescriptorSet>>>> _descriptorSetCameraDepth;
  std::shared_ptr<RenderGraph::DescriptorSetLayout> _descriptorSetLayoutNormalsMesh, _descriptorSetLayoutDepth;
  std::map<MaterialType, std::vector<std::shared_ptr<RenderGraph::DescriptorSetLayout>>> _descriptorSetLayout;
  std::map<MaterialType, std::shared_ptr<RenderGraph::Pipeline>> _pipeline;
  std::map<MaterialType, std::shared_ptr<RenderGraph::Pipeline>> _pipelineWireframe;
  std::shared_ptr<RenderGraph::Pipeline> _pipelineNormal, _pipelineTangent;
  std::shared_ptr<RenderGraph::Pipeline> _pipelineDirectional, _pipelinePoint;
  std::shared_ptr<RenderGraph::Shader> _shaderNormal, _shaderTangent, _shaderDirectional, _shaderPoint;
  std::map<MaterialType, std::shared_ptr<RenderGraph::Shader>> _shader;

  bool _enableShadow = true;
  bool _enableLighting = true;
  bool _enableDepth = true;
  bool _enableHUD = false;
  std::vector<std::vector<std::vector<std::shared_ptr<RenderGraph::Buffer>>>> _cameraUBODepth;
  std::vector<std::shared_ptr<RenderGraph::Buffer>> _cameraUBOFull;
  std::shared_ptr<Material> _material;
  std::shared_ptr<MaterialPhong> _defaultMaterialPhong;
  std::shared_ptr<MeshStatic2D> _mesh;
  MaterialType _materialType = MaterialType::PHONG;
  DrawType _drawType = DrawType::FILL;

  void _updateColorDescriptor(const RenderGraph::CommandBuffer& commandBuffer);
  void _updatePhongDescriptor(const RenderGraph::CommandBuffer& commandBuffer);
  void _updatePBRDescriptor(const RenderGraph::CommandBuffer& commandBuffer);
  void _unregisterMaterialDescriptors();

  // we pass base color to shader so alpha part of texture doesn't cast a shadow
  template <class T>
  void _updateShadowDescriptor(std::shared_ptr<T> material, const RenderGraph::CommandBuffer& commandBuffer) {
    int currentFrame = _engineState->getFrameInFlight();
    for (int d = 0; d < _engineState->getSettings()->getMaxDirectionalLights(); d++) {
      material->unregisterUpdate(_descriptorSetCameraDepth[currentFrame][d][0]);
      auto descriptor = std::make_shared<RenderGraph::DescriptorSet>(
          std::vector<RenderGraph::DescriptorSetLayout*>{(_descriptorSetLayoutDepth).get()},
          *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);
      descriptor->add({_cameraUBODepth[currentFrame][d][0].get()});
      descriptor->add({material->getBaseColor()[0].get()});
      descriptor->initialize(commandBuffer);
      material->registerUpdate(descriptor,
                               {.frameInFlight = currentFrame, .materialInfo = {{MaterialTexture::COLOR, 1}}});
      _descriptorSetCameraDepth[currentFrame][d][0] = std::move(descriptor);
    }

    for (int p = 0; p < _engineState->getSettings()->getMaxPointLights(); p++) {
      for (int f = 0; f < 6; f++) {
        const auto lightIndex = _engineState->getSettings()->getMaxDirectionalLights() + p;
        material->unregisterUpdate(_descriptorSetCameraDepth[currentFrame][lightIndex][f]);
        auto descriptor = std::make_shared<RenderGraph::DescriptorSet>(
            std::vector<RenderGraph::DescriptorSetLayout*>{(_descriptorSetLayoutDepth).get()},
            *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);
        descriptor->add({_cameraUBODepth[currentFrame][lightIndex][f].get()});
        descriptor->add({material->getBaseColor()[0].get()});
        descriptor->initialize(commandBuffer);
        material->registerUpdate(descriptor,
                                 {.frameInFlight = currentFrame, .materialInfo = {{MaterialTexture::COLOR, 1}}});
        _descriptorSetCameraDepth[currentFrame][lightIndex][f] = std::move(descriptor);
      }
    }
  }

 public:
  Sprite(const RenderGraph::CommandBuffer* commandBufferTransfer,
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

  void draw(const RenderGraph::CommandBuffer* commandBuffer) override;
  void drawShadow(LightType lightType,
                  int lightIndex,
                  int face,
                  const RenderGraph::CommandBuffer* commandBuffer) override;
};
