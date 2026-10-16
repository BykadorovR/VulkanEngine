#pragma once
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include "Primitive/Cubemap.h"
#include "Primitive/Shape3D.h"
#include "Primitive/Mesh.h"
#include "Graphic/Camera.h"

import Buffer;
import Command;
import DescriptorBuffer;
import Pipeline;
import Texture;

class Equirectangular {
 private:
  std::shared_ptr<EngineState> _engineState;
  std::unique_ptr<RenderGraph::Image> _image;
  std::shared_ptr<RenderGraph::ImageView> _imageView;
  std::shared_ptr<RenderGraph::Texture> _texture;
  std::shared_ptr<Cubemap> _cubemap;
  std::shared_ptr<RenderGraph::Buffer> _stagingBuffer;
  const RenderGraph::CommandBuffer* _commandBufferTransfer;
  std::shared_ptr<RenderGraph::DescriptorSetLayout> _descriptorSetLayout;
  std::vector<std::vector<std::shared_ptr<RenderGraph::Buffer>>> _bufferCubemap;
  std::vector<std::vector<std::shared_ptr<RenderGraph::DescriptorSet>>> _descriptorSetCubemap;
  std::shared_ptr<RenderGraph::Pipeline> _pipelineEquirectangular;
  std::shared_ptr<MaterialColor> _material;
  std::shared_ptr<MeshStatic3D> _mesh3D;
  std::shared_ptr<CameraPerspective> _camera;

  void _convertToCubemap();

 public:
  Equirectangular(std::shared_ptr<ImageCPU<float>> imageCPU,
                  const RenderGraph::CommandBuffer* commandBufferTransfer,
                  std::shared_ptr<EngineState> engineState);

  std::shared_ptr<RenderGraph::Texture> getTexture();
  std::shared_ptr<Cubemap> getCubemap();
};
