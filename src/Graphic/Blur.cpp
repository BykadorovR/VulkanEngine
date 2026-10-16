#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include "Graphic/Blur.h"

import Shader;

BlurSeparate::BlurSeparate(bool horizontal,
                           std::vector<std::shared_ptr<RenderGraph::Texture>> src,
                           std::vector<std::shared_ptr<RenderGraph::Texture>> dst,
                           std::shared_ptr<EngineState> engineState,
                           int layer) {
  _horizontal = horizontal;
  _textureSrc = src;
  _textureDst = dst;
  _engineState = engineState;
  _layer = layer;
}

void BlurSeparate::_updateWeights() {
  _blurWeights.clear();
  float sum = 0;
  for (int i = -_kernelSize / 2; i <= _kernelSize / 2; i++) {
    const float value = std::exp(-std::pow(static_cast<float>(i), 2.0f) / (2.0f * std::pow(_sigma, 2.0f)));
    _blurWeights.push_back(value);
    sum += value;
  }

  for (auto& coeff : _blurWeights) coeff /= sum;
}

void BlurSeparate::_updateDescriptors(int currentFrame) {
  _blurWeightsSSBO[currentFrame] = std::make_shared<RenderGraph::Buffer>(
      _blurWeights.size() * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      *_engineState->getMemoryAllocator());
  std::memcpy(static_cast<std::byte*>(_blurWeightsSSBO[currentFrame]->getAllocationInfo().pMappedData),
              _blurWeights.data(), static_cast<std::size_t>(_blurWeightsSSBO[currentFrame]->getSize()));
}

std::vector<std::shared_ptr<RenderGraph::Texture>> BlurSeparate::getTextureSrc() { return _textureSrc; }

std::vector<std::shared_ptr<RenderGraph::Texture>> BlurSeparate::getTextureDst() { return _textureDst; }

bool BlurSeparate::getHorizontal() { return _horizontal; }

void BlurSeparate::update(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) {
  if (_layer < 0) return;

  const auto& sampledImage = _textureSrc.at(currentFrame)->getImageView().getImage();
  const VkImageMemoryBarrier2 sampledBarrier{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
      .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
      .newLayout = VK_IMAGE_LAYOUT_GENERAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = sampledImage.getImage(),
      .subresourceRange =
          {
              .aspectMask = sampledImage.getAspectMask(),
              .baseMipLevel = 0,
              .levelCount = static_cast<uint32_t>(sampledImage.getMipMapNumber()),
              .baseArrayLayer = static_cast<uint32_t>(_layer),
              .layerCount = 1,
          },
  };
  const auto& targetImage = _textureDst.at(currentFrame)->getImageView().getImage();
  const VkImageMemoryBarrier2 targetBarrier{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
      .newLayout = VK_IMAGE_LAYOUT_GENERAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = targetImage.getImage(),
      .subresourceRange =
          {
              .aspectMask = targetImage.getAspectMask(),
              .baseMipLevel = 0,
              .levelCount = static_cast<uint32_t>(targetImage.getMipMapNumber()),
              .baseArrayLayer = static_cast<uint32_t>(_layer),
              .layerCount = 1,
          },
  };
  const VkImageMemoryBarrier2 barriers[]{sampledBarrier, targetBarrier};
  const VkDependencyInfo dependencyInfo{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 2,
      .pImageMemoryBarriers = barriers,
  };
  vkCmdPipelineBarrier2(commandBuffer.getCommandBuffer(), &dependencyInfo);
}

void BlurSeparate::reset(const std::vector<std::shared_ptr<RenderGraph::ImageView>>&) {}

BlurComputeSeparate::BlurComputeSeparate(bool horizontal,
                                         std::vector<std::shared_ptr<RenderGraph::Texture>> src,
                                         std::vector<std::shared_ptr<RenderGraph::Texture>> dst,
                                         std::shared_ptr<EngineState> engineState,
                                         int layer)
    : BlurSeparate(horizontal, std::move(src), std::move(dst), std::move(engineState), layer) {
  auto shader = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
  if (_horizontal) {
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/postprocessing/blurHorizontal_compute.spv"));
  } else {
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/postprocessing/blurVertical_compute.spv"));
  }

  const auto frameNumber = _engineState->getSettings()->getMaxFramesInFlight();
  if (_textureSrc.size() != frameNumber || _textureDst.size() != frameNumber) {
    throw std::invalid_argument("compute blur textures must match the number of frames in flight");
  }

  _blurWeightsSSBO.resize(frameNumber);
  _changed.resize(frameNumber, false);
  _descriptorSet.resize(frameNumber);
  _updateWeights();
  for (int i = 0; i < frameNumber; ++i) _updateDescriptors(i);

  _descriptorSetLayout = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());

  _descriptorSetLayout->createCustom(shader->getDescriptorSetLayoutBindings().at(0));
  std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts{_descriptorSetLayout.get()};
  const auto shaderStages = shader->getShaderStageInfo();
  _pipeline = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
  _pipeline->createCompute(shaderStages.front(), descriptorSetLayouts, {});
}

void BlurComputeSeparate::_initialize(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) {
  auto descriptorSet = std::make_shared<RenderGraph::DescriptorSet>(
      std::vector<RenderGraph::DescriptorSetLayout*>{(_descriptorSetLayout).get()}, *_engineState->getDescriptorPool(),
      *_engineState->getDevice(), 0);
  descriptorSet->add({_textureSrc[currentFrame].get()});
  descriptorSet->add({_textureDst[currentFrame].get()});
  descriptorSet->add({_blurWeightsSSBO[currentFrame].get()});
  descriptorSet->initialize(commandBuffer);
  _descriptorSet[currentFrame] = std::move(descriptorSet);
}

void BlurComputeSeparate::draw(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) {
  const auto logger = _engineState->getLogger();
  const std::string target = _layer < 0 ? "directional" : "point";
  const std::string direction = _horizontal ? "horizontal" : "vertical";
  logger->begin("Blur " + target + " " + direction + " " + std::to_string(currentFrame), &commandBuffer);

  if (_descriptorSet[currentFrame] == nullptr) _initialize(currentFrame, commandBuffer);

  vkCmdBindPipeline(commandBuffer.getCommandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline->getPipeline());
  _descriptorSet[currentFrame]->bind(VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline->getPipelineLayout(), commandBuffer);

  const auto [width, height] = std::tuple{(_textureSrc[currentFrame]->getImageView().getImage()).getResolution().x,
                                          (_textureSrc[currentFrame]->getImageView().getImage()).getResolution().y};
  constexpr float groupSize = 128.f;
  const int groupCountX = _horizontal ? std::max(1, static_cast<int>(std::ceil(width / groupSize))) : width;
  const int groupCountY = _horizontal ? height : std::max(1, static_cast<int>(std::ceil(height / groupSize)));
  vkCmdDispatch(commandBuffer.getCommandBuffer(), groupCountX, groupCountY, 1);
  logger->end(&commandBuffer);
}

BlurGraphicSeparate::BlurGraphicSeparate(bool horizontal,
                                         std::vector<std::shared_ptr<RenderGraph::Texture>> src,
                                         std::vector<std::shared_ptr<RenderGraph::Texture>> dst,
                                         const RenderGraph::CommandBuffer* commandBufferTransfer,
                                         std::shared_ptr<EngineState> engineState,
                                         int layer)
    : BlurSeparate(horizontal, src, dst, engineState, layer) {
  _resolution = std::tuple{(src[0]->getImageView().getImage()).getResolution().x,
                           (src[0]->getImageView().getImage()).getResolution().y};

  std::shared_ptr<RenderGraph::Shader> shader;
  if (horizontal) {
    shader = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/postprocessing/blur_vertex.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/postprocessing/blurHorizontal_fragment.spv"));
  } else {
    shader = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/postprocessing/blur_vertex.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/postprocessing/blurVertical_fragment.spv"));
  }
  _blurWeightsSSBO.resize(_engineState->getSettings()->getMaxFramesInFlight());
  _changed.resize(_engineState->getSettings()->getMaxFramesInFlight());
  _mesh = std::make_shared<MeshStatic2D>(engineState);
  // 3   0
  // 2   1
  _mesh->setVertices({Vertex2D{{1.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {1.f, 0.f}, {1.f, 0.f, 0.f, 1.f}},
                      Vertex2D{{1.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {1.f, 1.f}, {1.f, 0.f, 0.f, 1.f}},
                      Vertex2D{{-1.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {0.f, 1.f}, {1.f, 0.f, 0.f, 1.f}},
                      Vertex2D{{-1.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {0.f, 0.f}, {1.f, 0.f, 0.f, 1.f}}});
  _mesh->setIndexes({0, 3, 2, 2, 1, 0});

  _updateWeights();
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _updateDescriptors(i);
    _changed[i] = false;
  }

  _descriptorSetLayout = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());

  _descriptorSetLayout->createCustom(shader->getDescriptorSetLayoutBindings().at(0));

  _initialize(src, *commandBufferTransfer);

  const auto targetFormat = _textureDst.front()->getImageView().getImage().getFormat();
  RenderGraph::PipelineGraphic pipelineGraphic;
  pipelineGraphic.setColorAttachments({targetFormat});
  pipelineGraphic.setDepthAttachment(std::nullopt);
  pipelineGraphic.setDepthTest(false);
  pipelineGraphic.setDepthWrite(false);
  std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts{_descriptorSetLayout.get()};
  const auto vertexBinding = _mesh->getBindingDescription();
  const auto vertexAttributes = _mesh->getAttributeDescriptions(
      {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, pos)}, {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex2D, texCoord)}});
  const VkPipelineVertexInputStateCreateInfo vertexInput{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertexBinding,
      .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size()),
      .pVertexAttributeDescriptions = vertexAttributes.data(),
  };
  _pipeline = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
  _pipeline->createGraphic(pipelineGraphic, shader->getShaderStageInfo(), descriptorSetLayouts, {}, vertexInput);
}

void BlurGraphicSeparate::_initialize(std::vector<std::shared_ptr<RenderGraph::Texture>> src,
                                      const RenderGraph::CommandBuffer& commandBuffer) {
  _descriptorSet.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _descriptorSet[i] = std::make_shared<RenderGraph::DescriptorSet>(
        std::vector<RenderGraph::DescriptorSetLayout*>{(_descriptorSetLayout).get()},
        *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);

    _descriptorSet[i]->add({src[i].get()});
    _descriptorSet[i]->add({_blurWeightsSSBO[i].get()});
    _descriptorSet[i]->initialize(commandBuffer);
  }
}

void BlurGraphicSeparate::draw(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) {
  const auto logger = _engineState->getLogger();
  const std::string target = _layer < 0 ? "directional" : "point";
  const std::string direction = _horizontal ? "horizontal" : "vertical";
  logger->begin("Blur " + target + " " + direction + " " + std::to_string(currentFrame), &commandBuffer);
  vkCmdBindPipeline(commandBuffer.getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline->getPipeline());

  if (_changed[currentFrame]) {
    _updateDescriptors(currentFrame);
    _changed[currentFrame] = false;
  }

  auto resolution = _resolution;
  VkViewport viewport{.x = 0.0f,
                      .y = static_cast<float>(std::get<1>(resolution)),
                      .width = static_cast<float>(std::get<0>(resolution)),
                      .height = static_cast<float>(-std::get<1>(resolution)),
                      .minDepth = 0.0f,
                      .maxDepth = 1.0f};
  vkCmdSetViewport(commandBuffer.getCommandBuffer(), 0, 1, &viewport);

  VkRect2D scissor{.offset = {0, 0}, .extent = VkExtent2D(std::get<0>(resolution), std::get<1>(resolution))};
  vkCmdSetScissor(commandBuffer.getCommandBuffer(), 0, 1, &scissor);

  VkBuffer vertexBuffers[] = {_mesh->getVertexBuffer()->getBuffer()->getBuffer()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer.getCommandBuffer(), 0, 1, vertexBuffers, offsets);

  vkCmdBindIndexBuffer(commandBuffer.getCommandBuffer(), _mesh->getIndexBuffer()->getBuffer()->getBuffer(), 0,
                       VK_INDEX_TYPE_UINT32);

  _descriptorSet[currentFrame]->bind(VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline->getPipelineLayout(), commandBuffer);

  vkCmdDrawIndexed(commandBuffer.getCommandBuffer(), static_cast<uint32_t>(_mesh->getIndexData().size()), 1, 0, 0, 0);
  logger->end(&commandBuffer);
}
