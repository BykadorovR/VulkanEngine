#pragma once
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include "Primitive/Drawable.h"
#include "Utility/EngineState.h"
#include "Utility/GameState.h"
#include "Primitive/Cubemap.h"
#include "Primitive/Mesh.h"
#include "Graphic/Camera.h"
#include "Graphic/Material.h"

import Buffer;
import Command;
import DescriptorBuffer;
import Pipeline;
import Texture;

class IBL {
 private:
  std::shared_ptr<EngineState> _engineState;
  std::shared_ptr<GameState> _gameState;
  std::shared_ptr<MeshStatic3D> _mesh3D;
  std::shared_ptr<MeshStatic2D> _mesh2D;
  std::vector<std::vector<std::shared_ptr<RenderGraph::Buffer>>> _cameraBufferCubemap;
  const RenderGraph::CommandBuffer* _commandBufferTransfer;
  std::vector<std::shared_ptr<RenderGraph::Buffer>> _cameraBuffer;
  std::shared_ptr<RenderGraph::DescriptorSetLayout> _descriptorSetLayoutColor;
  std::vector<std::vector<std::shared_ptr<RenderGraph::DescriptorSet>>> _descriptorSetColor;
  std::shared_ptr<RenderGraph::DescriptorSetLayout> _descriptorSetLayoutBRDF;
  std::vector<std::shared_ptr<RenderGraph::DescriptorSet>> _descriptorSetBRDF;
  std::shared_ptr<RenderGraph::Pipeline> _pipelineDiffuse, _pipelineSpecular, _pipelineSpecularBRDF;
  std::shared_ptr<MaterialColor> _material;
  glm::mat4 _model = glm::mat4(1.f);
  std::shared_ptr<Cubemap> _cubemapDiffuse, _cubemapSpecular;
  std::shared_ptr<RenderGraph::Texture> _textureSpecularBRDF;
  std::shared_ptr<CameraOrtho> _cameraSpecularBRDF;
  std::shared_ptr<CameraPerspective> _camera;

  void _updateColorDescriptor();
  void _draw(int face,
             std::shared_ptr<Camera> camera,
             const RenderGraph::CommandBuffer* commandBuffer,
             std::shared_ptr<RenderGraph::Pipeline> pipeline);

 public:
  IBL(const RenderGraph::CommandBuffer* commandBufferTransfer,
      std::shared_ptr<GameState> gameState,
      std::shared_ptr<EngineState> engineState);
  void setMaterial(std::shared_ptr<MaterialColor> material);
  void setPosition(glm::vec3 position);
  std::shared_ptr<Cubemap> getCubemapDiffuse();
  std::shared_ptr<Cubemap> getCubemapSpecular();
  std::shared_ptr<RenderGraph::Texture> getTextureSpecularBRDF();
  void drawSpecularBRDF();
  void drawDiffuse();
  void drawSpecular();
};
