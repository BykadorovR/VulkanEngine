#include "Graphic/Blur.h"
#include <numbers>

BlurSeparate::BlurSeparate(bool horizontal,
                           std::vector<std::shared_ptr<Texture>> src,
                           std::vector<std::shared_ptr<Texture>> dst,
                           std::shared_ptr<EngineState> engineState) {
  _horizontal = horizontal;
  _textureSrc = src;
  _textureDst = dst;
  _engineState = engineState;
}

void BlurSeparate::_updateWeights() {
  _blurWeights.clear();
  float expectedValue = 0;
  float sum = 0;
  for (int i = -_kernelSize / 2; i <= _kernelSize / 2; i++) {
    float value = std::exp(-pow(i, 2) / (2 * pow(_sigma, 2)));
    _blurWeights.push_back(value);
    sum += value;
  }

  for (auto& coeff : _blurWeights) coeff /= sum;
}

void BlurSeparate::_updateDescriptors(int currentFrame) {
  _blurWeightsSSBO[currentFrame] = std::make_shared<Buffer>(
      _blurWeights.size() * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, _engineState);
  _blurWeightsSSBO[currentFrame]->setData(_blurWeights.data());
}

std::vector<std::shared_ptr<Texture>> BlurSeparate::getTextureSrc() { return _textureSrc; }

std::vector<std::shared_ptr<Texture>> BlurSeparate::getTextureDst() { return _textureDst; }

bool BlurSeparate::getHorizontal() { return _horizontal; }

void BlurComputeSeparate::_initialize(std::vector<std::shared_ptr<Texture>> src,
                                      std::vector<std::shared_ptr<Texture>> dst) {
  _descriptorSet.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _descriptorSet[i] = std::make_shared<DescriptorSet>(_descriptorSetLayout, _engineState);
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    {
      std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor = {
          {0,
           {VkDescriptorImageInfo{.imageView = src[i]->getImageView()->getImageView(),
                                  .imageLayout = src[i]->getImageView()->getImage()->getImageLayout()}}},
          {1,
           {VkDescriptorImageInfo{.imageView = dst[i]->getImageView()->getImageView(),
                                  .imageLayout = dst[i]->getImageView()->getImage()->getImageLayout()}}}};
      std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfo = {
          {2,
           {VkDescriptorBufferInfo{.buffer = _blurWeightsSSBO[i]->getData(),
                                   .offset = 0,
                                   .range = _blurWeightsSSBO[i]->getSize()}}}};
      _descriptorSet[i]->createCustom(bufferInfo, textureInfoColor);
    }
  }
}

BlurComputeSeparate::BlurComputeSeparate(bool horizontal,
                                         std::vector<std::shared_ptr<Texture>> src,
                                         std::vector<std::shared_ptr<Texture>> dst,
                                         std::shared_ptr<EngineState> engineState)
    : BlurSeparate(horizontal, src, dst, engineState) {
  auto shader = std::make_shared<Shader>(_engineState);
  if (horizontal)
    shader->add("shaders/postprocessing/blurHorizontal_compute.spv", VK_SHADER_STAGE_COMPUTE_BIT);
  else
    shader->add("shaders/postprocessing/blurVertical_compute.spv", VK_SHADER_STAGE_COMPUTE_BIT);

  _descriptorSetLayout = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
  std::vector<VkDescriptorSetLayoutBinding> descriptorSetlayoutBinding{
      {.binding = 0,
       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
       .descriptorCount = 1,
       .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
       .pImmutableSamplers = nullptr},
      {.binding = 1,
       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
       .descriptorCount = 1,
       .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
       .pImmutableSamplers = nullptr},
      {.binding = 2,
       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
       .descriptorCount = 1,
       .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
       .pImmutableSamplers = nullptr}};
  _descriptorSetLayout->createCustom(descriptorSetlayoutBinding);

  _blurWeightsSSBO.resize(_engineState->getSettings()->getMaxFramesInFlight());
  _changed.resize(_engineState->getSettings()->getMaxFramesInFlight());
  _updateWeights();
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _updateDescriptors(i);
    _changed[i] = false;
  }

  _initialize(src, dst);

  _pipeline = std::make_shared<PipelineCompute>(_engineState->getDevice());
  _pipeline->createCustom(shader, {std::pair{std::string("descriptor"), _descriptorSetLayout}}, {});
}

void BlurComputeSeparate::draw(std::shared_ptr<CommandBuffer> commandBuffer) {
  auto currentFrame = _engineState->getFrameInFlight();
  std::shared_ptr<DescriptorSet> descriptorSet = _descriptorSet[currentFrame];
  float groupCountX = 1.f;
  float groupCountY = 128.f;
  if (_horizontal) {
    groupCountX = 128.f;
    groupCountY = 1.f;
  }
  vkCmdBindPipeline(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline->getPipeline());

  if (_changed[currentFrame]) {
    _updateDescriptors(currentFrame);
    _changed[currentFrame] = false;
  }

  auto pipelineLayout = _pipeline->getDescriptorSetLayout();
  auto computeLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                    [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                      return info.first == std::string("descriptor");
                                    });
  if (computeLayout != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE,
                            _pipeline->getPipelineLayout(), 0, 1, &descriptorSet->getDescriptorSets(), 0, nullptr);
  }

  auto [width, height] = _engineState->getSettings()->getResolution();
  vkCmdDispatch(commandBuffer->getCommandBuffer(), std::max(1, (int)std::ceil(width / groupCountX)),
                std::max(1, (int)std::ceil(height / groupCountY)), 1);
}

BlurGraphicSeparate::BlurGraphicSeparate(bool horizontal,
                                         std::vector<std::shared_ptr<Texture>> src,
                                         std::vector<std::shared_ptr<Texture>> dst,
                                         std::shared_ptr<CommandBuffer> commandBufferTransfer,
                                         std::shared_ptr<EngineState> engineState)
    : BlurSeparate(horizontal, src, dst, engineState) {
  _resolution = src[0]->getImageView()->getImage()->getResolution();

  std::shared_ptr<Shader> shader;
  if (horizontal) {
    shader = std::make_shared<Shader>(_engineState);
    shader->add("shaders/postprocessing/blur_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
    shader->add("shaders/postprocessing/blurHorizontal_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
  } else {
    shader = std::make_shared<Shader>(_engineState);
    shader->add("shaders/postprocessing/blur_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
    shader->add("shaders/postprocessing/blurVertical_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
  }
  _blurWeightsSSBO.resize(_engineState->getSettings()->getMaxFramesInFlight());
  _changed.resize(_engineState->getSettings()->getMaxFramesInFlight());
  _mesh = std::make_shared<MeshStatic2D>(engineState);
  // 3   0
  // 2   1
  _mesh->setVertices({Vertex2D{{1.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {1.f, 0.f}, {1.f, 0.f, 0.f, 1.f}},
                      Vertex2D{{1.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {1.f, 1.f}, {1.f, 0.f, 0.f, 1.f}},
                      Vertex2D{{-1.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {0.f, 1.f}, {1.f, 0.f, 0.f, 1.f}},
                      Vertex2D{{-1.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {0.f, 0.f}, {1.f, 0.f, 0.f, 1.f}}},
                     commandBufferTransfer);
  _mesh->setIndexes({0, 3, 2, 2, 1, 0}, commandBufferTransfer);

  _updateWeights();
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _updateDescriptors(i);
    _changed[i] = false;
  }

  _descriptorSetLayout = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
  std::vector<VkDescriptorSetLayoutBinding> descriptorSetlayoutBinding{
      {.binding = 0,
       .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
       .descriptorCount = 1,
       .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
       .pImmutableSamplers = nullptr},
      {.binding = 1,
       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
       .descriptorCount = 1,
       .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
       .pImmutableSamplers = nullptr}};
  _descriptorSetLayout->createCustom(descriptorSetlayoutBinding);

  _initialize(src);

  _pipeline = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(), _engineState->getDevice());
  _pipeline->setDepthTest(true);
  _pipeline->setDepthWrite(true);
  _pipeline->createCustom(
      shader, {std::pair{std::string("blur"), _descriptorSetLayout}}, {}, _mesh->getBindingDescription(),
      _mesh->Mesh2D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, pos)},
                                               {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex2D, texCoord)}}),
      RenderPassScenario::BLUR);
}

void BlurGraphicSeparate::_initialize(std::vector<std::shared_ptr<Texture>> src) {
  _descriptorSet.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _descriptorSet[i] = std::make_shared<DescriptorSet>(_descriptorSetLayout, _engineState);

    std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor = {
        {0,
         {VkDescriptorImageInfo{.sampler = src[i]->getSampler()->getSampler(),
                                .imageView = src[i]->getImageView()->getImageView(),
                                .imageLayout = src[i]->getImageView()->getImage()->getImageLayout()}}}};
    std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor = {
        {1,
         {VkDescriptorBufferInfo{.buffer = _blurWeightsSSBO[i]->getData(),
                                 .offset = 0,
                                 .range = _blurWeightsSSBO[i]->getSize()}}}};
    _descriptorSet[i]->createCustom(bufferInfoColor, textureInfoColor);
  }
}

void BlurGraphicSeparate::draw(std::shared_ptr<CommandBuffer> commandBuffer) {
  int currentFrame = _engineState->getFrameInFlight();
  vkCmdBindPipeline(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline->getPipeline());

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
  vkCmdSetViewport(commandBuffer->getCommandBuffer(), 0, 1, &viewport);

  VkRect2D scissor{.offset = {0, 0}, .extent = VkExtent2D(std::get<0>(resolution), std::get<1>(resolution))};
  vkCmdSetScissor(commandBuffer->getCommandBuffer(), 0, 1, &scissor);

  VkBuffer vertexBuffers[] = {_mesh->getVertexBuffer()->getBuffer()->getData()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);

  vkCmdBindIndexBuffer(commandBuffer->getCommandBuffer(), _mesh->getIndexBuffer()->getBuffer()->getData(), 0,
                       VK_INDEX_TYPE_UINT32);

  auto pipelineLayout = _pipeline->getDescriptorSetLayout();
  vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                          _pipeline->getPipelineLayout(), 0, 1, &_descriptorSet[currentFrame]->getDescriptorSets(), 0,
                          nullptr);

  vkCmdDrawIndexed(commandBuffer->getCommandBuffer(), static_cast<uint32_t>(_mesh->getIndexData().size()), 1, 0, 0, 0);
}