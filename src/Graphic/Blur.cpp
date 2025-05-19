#include "Graphic/Blur.h"
#include <numbers>

void Blur::_updateWeights() {
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

int Blur::getKernelSize() { return _kernelSize; }

int Blur::getSigma() { return _sigma; }

void Blur::setKernelSize(int kernelSize) {
  kernelSize = std::min(kernelSize, 65);
  _kernelSize = kernelSize;
  _updateWeights();
  for (int i = 0; i < _changed.size(); i++) _changed[i] = true;
}

void Blur::setSigma(int sigma) {
  _sigma = sigma;
  _updateWeights();
  for (int i = 0; i < _changed.size(); i++) _changed[i] = true;
}

void Blur::reset(std::vector<std::shared_ptr<Texture>> src, std::vector<std::shared_ptr<Texture>> dst) {
  _initialize(src, dst);
}

void BlurCompute::_updateDescriptors(int currentFrame) {
  _blurWeightsSSBO[currentFrame] = std::make_shared<Buffer>(
      _blurWeights.size() * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, _engineState);
  _blurWeightsSSBO[currentFrame]->setData(_blurWeights.data());
}

void BlurCompute::_setWeights(int currentFrame) {
  std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfo = {
      {0,
       {VkDescriptorBufferInfo{.buffer = _blurWeightsSSBO[currentFrame]->getData(),
                               .offset = 0,
                               .range = _blurWeightsSSBO[currentFrame]->getSize()}}}};
  _descriptorSetWeights[currentFrame]->createCustom(bufferInfo, {});
}

void BlurCompute::_initialize(std::vector<std::shared_ptr<Texture>> src, std::vector<std::shared_ptr<Texture>> dst) {
  _descriptorSetHorizontal.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _descriptorSetHorizontal[i] = std::make_shared<DescriptorSet>(_textureLayout, _engineState);
  _descriptorSetVertical.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _descriptorSetVertical[i] = std::make_shared<DescriptorSet>(_textureLayout, _engineState);
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    {
      std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor = {
          {0,
           {VkDescriptorImageInfo{.imageView = src[i]->getImageView()->getImageView(),
                                  .imageLayout = src[i]->getImageView()->getImage()->getImageLayout()}}},
          {1,
           {VkDescriptorImageInfo{.imageView = dst[i]->getImageView()->getImageView(),
                                  .imageLayout = dst[i]->getImageView()->getImage()->getImageLayout()}}}};
      _descriptorSetHorizontal[i]->createCustom({}, textureInfoColor);
    }
    {
      std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor = {
          {0,
           {VkDescriptorImageInfo{.imageView = dst[i]->getImageView()->getImageView(),
                                  .imageLayout = dst[i]->getImageView()->getImage()->getImageLayout()}}},
          {1,
           {VkDescriptorImageInfo{.imageView = src[i]->getImageView()->getImageView(),
                                  .imageLayout = src[i]->getImageView()->getImage()->getImageLayout()}}}};
      _descriptorSetVertical[i]->createCustom({}, textureInfoColor);
    }
  }
}

BlurCompute::BlurCompute(std::vector<std::shared_ptr<Texture>> src,
                         std::vector<std::shared_ptr<Texture>> dst,
                         std::shared_ptr<EngineState> engineState) {
  _engineState = engineState;

  auto shaderVertical = std::make_shared<Shader>(_engineState);
  shaderVertical->add("shaders/postprocessing/blurVertical_compute.spv", VK_SHADER_STAGE_COMPUTE_BIT);
  auto shaderHorizontal = std::make_shared<Shader>(_engineState);
  shaderHorizontal->add("shaders/postprocessing/blurHorizontal_compute.spv", VK_SHADER_STAGE_COMPUTE_BIT);

  _textureLayout = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
  std::vector<VkDescriptorSetLayoutBinding> layoutBindingTexture{{.binding = 0,
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
                                                                  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                                  .descriptorCount = 1,
                                                                  .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                                                  .pImmutableSamplers = nullptr}};
  _textureLayout->createCustom(layoutBindingTexture);

  _blurWeightsSSBO.resize(_engineState->getSettings()->getMaxFramesInFlight());
  _changed.resize(_engineState->getSettings()->getMaxFramesInFlight());

  auto layoutWeights = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
  VkDescriptorSetLayoutBinding layoutBindingWeights = {.binding = 0,
                                                       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                       .descriptorCount = 1,
                                                       .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                                       .pImmutableSamplers = nullptr};
  layoutWeights->createCustom({layoutBindingWeights});
  _descriptorSetWeights.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _descriptorSetWeights[i] = std::make_shared<DescriptorSet>(layoutWeights, _engineState);

  _initialize(src, dst);

  _pipelineVertical = std::make_shared<PipelineCompute>(_engineState->getDevice());
  _pipelineVertical->createCustom(
      shaderVertical->getShaderStageInfo(VK_SHADER_STAGE_COMPUTE_BIT),
      {std::pair{std::string("texture"), _textureLayout}, std::pair{std::string("weights"), layoutWeights}}, {});
  _pipelineHorizontal = std::make_shared<PipelineCompute>(_engineState->getDevice());
  _pipelineHorizontal->createCustom(
      shaderHorizontal->getShaderStageInfo(VK_SHADER_STAGE_COMPUTE_BIT),
      {std::pair{std::string("texture"), _textureLayout}, std::pair{std::string("weights"), layoutWeights}}, {});

  _updateWeights();
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _updateDescriptors(i);
    _setWeights(i);
    _changed[i] = false;
  }
}

void BlurCompute::draw(bool horizontal, std::shared_ptr<CommandBuffer> commandBuffer) {
  auto currentFrame = _engineState->getFrameInFlight();
  std::shared_ptr<Pipeline> pipeline = _pipelineVertical;
  std::shared_ptr<DescriptorSet> descriptorSet = _descriptorSetVertical[currentFrame];
  float groupCountX = 1.f;
  float groupCountY = 128.f;
  if (horizontal) {
    pipeline = _pipelineHorizontal;
    descriptorSet = _descriptorSetHorizontal[currentFrame];
    groupCountX = 128.f;
    groupCountY = 1.f;
  }
  vkCmdBindPipeline(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->getPipeline());

  if (_changed[currentFrame]) {
    _updateDescriptors(currentFrame);
    _setWeights(currentFrame);
    _changed[currentFrame] = false;
  }

  auto pipelineLayout = pipeline->getDescriptorSetLayout();
  auto computeLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                    [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                      return info.first == std::string("texture");
                                    });
  if (computeLayout != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline->getPipelineLayout(), 0, 1, &descriptorSet->getDescriptorSets(), 0, nullptr);
  }

  auto weightsLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                    [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                      return info.first == std::string("weights");
                                    });
  if (weightsLayout != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline->getPipelineLayout(), 1, 1,
                            &_descriptorSetWeights[currentFrame]->getDescriptorSets(), 0, nullptr);
  }

  auto [width, height] = _engineState->getSettings()->getResolution();
  vkCmdDispatch(commandBuffer->getCommandBuffer(), std::max(1, (int)std::ceil(width / groupCountX)),
                std::max(1, (int)std::ceil(height / groupCountY)), 1);
}

void BlurGraphic::_initialize(std::vector<std::shared_ptr<Texture>> src, std::vector<std::shared_ptr<Texture>> dst) {
  _descriptorSetHorizontal.resize(_engineState->getSettings()->getMaxFramesInFlight());
  _descriptorSetVertical.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _descriptorSetHorizontal[i] = std::make_shared<DescriptorSet>(_layoutBlur, _engineState);
    _descriptorSetVertical[i] = std::make_shared<DescriptorSet>(_layoutBlur, _engineState);

    std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor = {
        {1,
         {VkDescriptorBufferInfo{.buffer = _blurWeightsSSBO[i]->getData(),
                                 .offset = 0,
                                 .range = _blurWeightsSSBO[i]->getSize()}}}};
    std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor = {
        {0,
         {VkDescriptorImageInfo{.sampler = src[i]->getSampler()->getSampler(),
                                .imageView = src[i]->getImageView()->getImageView(),
                                .imageLayout = src[i]->getImageView()->getImage()->getImageLayout()}}}};
    _descriptorSetHorizontal[i]->createCustom(bufferInfoColor, textureInfoColor);

    textureInfoColor.clear();
    textureInfoColor = {{0,
                         {VkDescriptorImageInfo{.sampler = dst[i]->getSampler()->getSampler(),
                                                .imageView = dst[i]->getImageView()->getImageView(),
                                                .imageLayout = dst[i]->getImageView()->getImage()->getImageLayout()}}}};
    _descriptorSetVertical[i]->createCustom(bufferInfoColor, textureInfoColor);
  }
}

void BlurGraphic::_updateDescriptors(int currentFrame) {
  _blurWeightsSSBO[currentFrame] = std::make_shared<Buffer>(
      _blurWeights.size() * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, _engineState);
  _blurWeightsSSBO[currentFrame]->setData(_blurWeights.data());
}

void BlurGraphic::_setWeights(int currentFrame) {
  std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor = {
      {1,
       {VkDescriptorBufferInfo{.buffer = _blurWeightsSSBO[currentFrame]->getData(),
                               .offset = 0,
                               .range = _blurWeightsSSBO[currentFrame]->getSize()}}}};
  _descriptorSetHorizontal[currentFrame]->createCustom(bufferInfoColor, {});
  _descriptorSetVertical[currentFrame]->createCustom(bufferInfoColor, {});
}

BlurGraphic::BlurGraphic(std::vector<std::shared_ptr<Texture>> src,
                         std::vector<std::shared_ptr<Texture>> dst,
                         std::shared_ptr<CommandBuffer> commandBufferTransfer,
                         std::shared_ptr<EngineState> engineState) {
  _engineState = engineState;
  _resolution = src[0]->getImageView()->getImage()->getResolution();

  auto shaderVertical = std::make_shared<Shader>(_engineState);
  shaderVertical->add("shaders/postprocessing/blur_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
  shaderVertical->add("shaders/postprocessing/blurVertical_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
  auto shaderHorizontal = std::make_shared<Shader>(_engineState);
  shaderHorizontal->add("shaders/postprocessing/blur_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
  shaderHorizontal->add("shaders/postprocessing/blurHorizontal_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
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

  _renderPass = _engineState->getRenderPassManager()->getRenderPass(RenderPassScenario::BLUR);

  _updateWeights();
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _updateDescriptors(i);
    _changed[i] = false;
  }

  _layoutBlur = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
  std::vector<VkDescriptorSetLayoutBinding> layoutBinding{{.binding = 0,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 1,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                           .pImmutableSamplers = nullptr}};
  _layoutBlur->createCustom(layoutBinding);

  _initialize(src, dst);

  _pipelineHorizontal = std::make_shared<PipelineGraphic>(_engineState->getDevice());
  _pipelineHorizontal->setDepthTest(true);
  _pipelineHorizontal->setDepthWrite(true);
  _pipelineHorizontal->createCustom(
      {shaderHorizontal->getShaderStageInfo(VK_SHADER_STAGE_VERTEX_BIT),
       shaderHorizontal->getShaderStageInfo(VK_SHADER_STAGE_FRAGMENT_BIT)},
      {std::pair{std::string("blur"), _layoutBlur}}, {}, _mesh->getBindingDescription(),
      _mesh->Mesh2D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, pos)},
                                               {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex2D, texCoord)}}),
      _renderPass);

  _pipelineVertical = std::make_shared<PipelineGraphic>(_engineState->getDevice());
  _pipelineVertical->setDepthTest(true);
  _pipelineVertical->setDepthWrite(true);
  _pipelineVertical->createCustom(
      {shaderVertical->getShaderStageInfo(VK_SHADER_STAGE_VERTEX_BIT),
       shaderVertical->getShaderStageInfo(VK_SHADER_STAGE_FRAGMENT_BIT)},
      {std::pair{std::string("blur"), _layoutBlur}}, {}, _mesh->getBindingDescription(),
      _mesh->Mesh2D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, pos)},
                                               {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex2D, texCoord)}}),
      _renderPass);
}

void BlurGraphic::draw(bool horizontal, std::shared_ptr<CommandBuffer> commandBuffer) {
  int currentFrame = _engineState->getFrameInFlight();
  std::shared_ptr<Pipeline> pipeline = _pipelineVertical;
  std::shared_ptr<DescriptorSet> descriptorSet = _descriptorSetVertical[currentFrame];
  if (horizontal) {
    pipeline = _pipelineHorizontal;
    descriptorSet = _descriptorSetHorizontal[currentFrame];
  }
  vkCmdBindPipeline(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipeline());

  if (_changed[currentFrame]) {
    _updateDescriptors(currentFrame);
    _setWeights(currentFrame);
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

  auto pipelineLayout = pipeline->getDescriptorSetLayout();
  vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline->getPipelineLayout(), 0, 1, &descriptorSet->getDescriptorSets(), 0, nullptr);

  vkCmdDrawIndexed(commandBuffer->getCommandBuffer(), static_cast<uint32_t>(_mesh->getIndexData().size()), 1, 0, 0, 0);
}

BlurGraphicSeparate::BlurGraphicSeparate(bool horizontal,
                                         std::vector<std::shared_ptr<Texture>> src,
                                         std::vector<std::shared_ptr<Texture>> dst,
                                         std::shared_ptr<CommandBuffer> commandBufferTransfer,
                                         std::shared_ptr<EngineState> engineState) {
  _engineState = engineState;
  _textureSrc = src;
  _textureDst = dst;
  _horizontal = horizontal;

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

  _renderPass = _engineState->getRenderPassManager()->getRenderPass(RenderPassScenario::BLUR);

  _updateWeights();
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _updateDescriptors(i);
    _changed[i] = false;
  }

  _layoutBlur = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
  std::vector<VkDescriptorSetLayoutBinding> layoutBinding{{.binding = 0,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 1,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                           .pImmutableSamplers = nullptr}};
  _layoutBlur->createCustom(layoutBinding);

  _initialize(src);

  if (horizontal) {
    _pipeline = std::make_shared<PipelineGraphic>(_engineState->getDevice());
    _pipeline->setDepthTest(true);
    _pipeline->setDepthWrite(true);
    _pipeline->createCustom(
        {shader->getShaderStageInfo(VK_SHADER_STAGE_VERTEX_BIT),
         shader->getShaderStageInfo(VK_SHADER_STAGE_FRAGMENT_BIT)},
        {std::pair{std::string("blur"), _layoutBlur}}, {}, _mesh->getBindingDescription(),
        _mesh->Mesh2D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, pos)},
                                                 {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex2D, texCoord)}}),
        _renderPass);
  } else {
    _pipeline = std::make_shared<PipelineGraphic>(_engineState->getDevice());
    _pipeline->setDepthTest(true);
    _pipeline->setDepthWrite(true);
    _pipeline->createCustom(
        {shader->getShaderStageInfo(VK_SHADER_STAGE_VERTEX_BIT),
         shader->getShaderStageInfo(VK_SHADER_STAGE_FRAGMENT_BIT)},
        {std::pair{std::string("blur"), _layoutBlur}}, {}, _mesh->getBindingDescription(),
        _mesh->Mesh2D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, pos)},
                                                 {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex2D, texCoord)}}),
        _renderPass);
  }
}

void BlurGraphicSeparate::_updateDescriptors(int currentFrame) {
  _blurWeightsSSBO[currentFrame] = std::make_shared<Buffer>(
      _blurWeights.size() * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, _engineState);
  _blurWeightsSSBO[currentFrame]->setData(_blurWeights.data());
}

void BlurGraphicSeparate::_updateWeights() {
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

void BlurGraphicSeparate::_setWeights(int currentFrame) {
  std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfo = {
      {0,
       {VkDescriptorBufferInfo{.buffer = _blurWeightsSSBO[currentFrame]->getData(),
                               .offset = 0,
                               .range = _blurWeightsSSBO[currentFrame]->getSize()}}}};
  _descriptorSet[currentFrame]->createCustom(bufferInfo, {});
}

void BlurGraphicSeparate::_initialize(std::vector<std::shared_ptr<Texture>> src) {
  _descriptorSet.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _descriptorSet[i] = std::make_shared<DescriptorSet>(_layoutBlur, _engineState);

    std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor = {
        {1,
         {VkDescriptorBufferInfo{.buffer = _blurWeightsSSBO[i]->getData(),
                                 .offset = 0,
                                 .range = _blurWeightsSSBO[i]->getSize()}}}};
    std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor = {
        {0,
         {VkDescriptorImageInfo{.sampler = src[i]->getSampler()->getSampler(),
                                .imageView = src[i]->getImageView()->getImageView(),
                                .imageLayout = src[i]->getImageView()->getImage()->getImageLayout()}}}};
    _descriptorSet[i]->createCustom(bufferInfoColor, textureInfoColor);
  }
}

std::vector<std::shared_ptr<Texture>> BlurGraphicSeparate::getTextureSrc() { return _textureSrc; }

std::vector<std::shared_ptr<Texture>> BlurGraphicSeparate::getTextureDst() { return _textureDst; }

bool BlurGraphicSeparate::getHorizontal() { return _horizontal; }

void BlurGraphicSeparate::draw(std::shared_ptr<CommandBuffer> commandBuffer) {
  int currentFrame = _engineState->getFrameInFlight();
  vkCmdBindPipeline(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline->getPipeline());

  if (_changed[currentFrame]) {
    _updateDescriptors(currentFrame);
    _setWeights(currentFrame);
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