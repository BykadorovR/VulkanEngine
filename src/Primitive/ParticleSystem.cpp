#include <cstring>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vk_mem_alloc.h>
#include "Primitive/ParticleSystem.h"

import Shader;

struct VertexPush {
  float pointScale;  // nominator of gl_PointSize
};

ParticleSystemGraphic::ParticleSystemGraphic(std::vector<Particle> particles,
                                             std::shared_ptr<RenderGraph::Texture> texture,
                                             std::shared_ptr<GameState> gameState,
                                             const RenderGraph::CommandBuffer& commandBuffer,
                                             std::shared_ptr<EngineState> engineState) {
  setName("Particle system");
  _particles = particles;
  _engineState = engineState;
  _gameState = gameState;
  _texture = texture;

  _initializeGraphic(commandBuffer);
}

void ParticleSystemGraphic::_initializeGraphic(const RenderGraph::CommandBuffer& commandBuffer) {
  auto shader = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
  shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                            "shaders/particles/particle_vertex.spv"));
  shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                            "shaders/particles/particle_fragment.spv"));

  _cameraUniformBuffer.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _cameraUniformBuffer[i] = std::make_shared<RenderGraph::Buffer>(
        sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *_engineState->getMemoryAllocator());

  _descriptorSetLayoutGraphic = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());

  _descriptorSetLayoutGraphic->createCustom(shader->getDescriptorSetLayoutBindings().at(0));

  _descriptorSetGraphic.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _descriptorSetGraphic[i] = std::make_shared<RenderGraph::DescriptorSet>(
        std::vector<RenderGraph::DescriptorSetLayout*>{_descriptorSetLayoutGraphic.get()},
        *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);
    _descriptorSetGraphic[i]->add({_cameraUniformBuffer[i].get()});
    _descriptorSetGraphic[i]->add({_texture.get()});
    _descriptorSetGraphic[i]->initialize(commandBuffer);
  }

  // there is no mesh, so store separately
  std::vector<VkVertexInputAttributeDescription> attributeDescriptions{
      {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Particle, position)},
      {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Particle, color)},
      {.location = 2, .binding = 0, .format = VK_FORMAT_R32_SFLOAT, .offset = offsetof(Particle, maxLife)},
      {.location = 3, .binding = 0, .format = VK_FORMAT_R32_SFLOAT, .offset = offsetof(Particle, maxVelocity)},
      {.location = 4,
       .binding = 0,
       .format = VK_FORMAT_R32G32B32_SFLOAT,
       .offset = offsetof(Particle, velocityDirection)}};

  VkVertexInputBindingDescription bindingDescriptions = {.binding = 0,
                                                         .stride = sizeof(Particle),
                                                         .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};

  const VkPipelineVertexInputStateCreateInfo vertexInput{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &bindingDescriptions,
      .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
      .pVertexAttributeDescriptions = attributeDescriptions.data()};
  const auto settings = _engineState->getSettings();
  RenderGraph::PipelineGraphic pipelineGraphic;
  pipelineGraphic.setColorAttachments({settings->getGraphicColorFormat()});
  pipelineGraphic.setDepthAttachment(settings->getDepthFormat());
  pipelineGraphic.setCullMode(VK_CULL_MODE_BACK_BIT);
  pipelineGraphic.setDepthTest(true);
  pipelineGraphic.setDepthWrite(false);
  pipelineGraphic.setAlphaBlending(true);
  pipelineGraphic.setTopology(VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
  std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts{_descriptorSetLayoutGraphic.get()};
  const std::unordered_map<std::string, VkPushConstantRange> pushConstants{
      {"vertex", {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .offset = 0, .size = sizeof(VertexPush)}}};
  _graphicPipeline = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
  _graphicPipeline->createGraphic(pipelineGraphic, shader->getShaderStageInfo(), descriptorSetLayouts, pushConstants,
                                  vertexInput);
}

std::vector<std::unique_ptr<RenderGraph::Buffer>> ParticleSystemGraphic::createParticleBuffers() const {
  std::vector<std::unique_ptr<RenderGraph::Buffer>> particleBuffers;
  const int frameCount = _engineState->getSettings()->getMaxFramesInFlight();
  particleBuffers.reserve(frameCount);
  for (int frame = 0; frame < frameCount; ++frame) {
    auto buffer = std::make_unique<RenderGraph::Buffer>(
        _particles.size() * sizeof(Particle), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *_engineState->getMemoryAllocator());
    std::memcpy(buffer->getAllocationInfo().pMappedData, _particles.data(),
                static_cast<std::size_t>(buffer->getSize()));
    if (vmaFlushAllocation(_engineState->getMemoryAllocator()->getAllocator(), buffer->getAllocation(), 0,
                           VK_WHOLE_SIZE) != VK_SUCCESS) {
      throw std::runtime_error("Failed to flush particle buffer");
    }
    particleBuffers.push_back(std::move(buffer));
  }
  return particleBuffers;
}

void ParticleSystemGraphic::attachParticleBuffers(std::vector<RenderGraph::Buffer*> particleBuffers) {
  _particlesBuffer = std::move(particleBuffers);
}

void ParticleSystemGraphic::preserveParticleState(int frame) {
  if (_particlesBuffer.empty()) return;
  if (frame < 0 || static_cast<size_t>(frame) >= _particlesBuffer.size()) {
    throw std::out_of_range("Particle frame is out of range");
  }

  auto* buffer = _particlesBuffer[frame];
  if (vmaInvalidateAllocation(_engineState->getMemoryAllocator()->getAllocator(), buffer->getAllocation(), 0,
                              VK_WHOLE_SIZE) != VK_SUCCESS) {
    throw std::runtime_error("Failed to invalidate particle buffer");
  }
  std::memcpy(_particles.data(), buffer->getAllocationInfo().pMappedData, static_cast<std::size_t>(buffer->getSize()));
}

void ParticleSystemGraphic::releaseGraphResources() { _particlesBuffer.clear(); }

void ParticleSystemGraphic::setPointScale(float pointScale) { _pointScale = pointScale; }

void ParticleSystemGraphic::updateTimer(float frameTimer) { _frameTimer = frameTimer; }

std::size_t ParticleSystemGraphic::getParticleCount() const { return _particles.size(); }

float ParticleSystemGraphic::getFrameTimer() const { return _frameTimer; }

ParticleSystemCompute::ParticleSystemCompute(
    std::shared_ptr<ParticleSystemGraphic> particleSystemGraphic,
    std::vector<RenderGraph::Buffer*> particleBuffers,
    const RenderGraph::CommandBuffer& commandBuffer,
    std::shared_ptr<EngineState> engineState)
    : _particleSystemGraphic(std::move(particleSystemGraphic)),
      _engineState(std::move(engineState)),
      _particlesBuffer(std::move(particleBuffers)) {
  const int frameCount = _engineState->getSettings()->getMaxFramesInFlight();
  if (_particlesBuffer.size() != static_cast<std::size_t>(frameCount)) {
    throw std::logic_error("Particle buffer count must match the number of frames in flight");
  }

  auto shader = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
  shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                            "shaders/particles/particle_compute.spv"));

  _deltaUniformBuffer.resize(frameCount);
  for (int frame = 0; frame < frameCount; ++frame) {
    _deltaUniformBuffer[frame] = std::make_shared<RenderGraph::Buffer>(
        sizeof(float), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *_engineState->getMemoryAllocator());
  }

  _descriptorSetLayout = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
  _descriptorSetLayout->createCustom(shader->getDescriptorSetLayoutBindings().at(0));

  _pipeline = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
  std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts{_descriptorSetLayout.get()};
  _pipeline->createCompute(shader->getShaderStageInfo().front(), descriptorSetLayouts, {});
  _initializeDescriptors(commandBuffer);
}

void ParticleSystemCompute::_initializeDescriptors(const RenderGraph::CommandBuffer& commandBuffer) {
  const int frameCount = _engineState->getSettings()->getMaxFramesInFlight();
  _descriptorSets.resize(frameCount);
  for (int frame = 0; frame < frameCount; ++frame) {
    _descriptorSets[frame] = std::make_shared<RenderGraph::DescriptorSet>(
        std::vector<RenderGraph::DescriptorSetLayout*>{_descriptorSetLayout.get()},
        *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);
    _descriptorSets[frame]->add({_deltaUniformBuffer[frame].get()});
    _descriptorSets[frame]->add({_particlesBuffer[(frame + frameCount - 1) % frameCount]});
    _descriptorSets[frame]->add({_particlesBuffer[frame]});
    _descriptorSets[frame]->initialize(commandBuffer);
  }
}

void ParticleSystemCompute::draw(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) {
  const auto logger = _engineState->getLogger();
  logger->begin("Particle system compute " + _particleSystemGraphic->getName() + ", frame: " +
                    std::to_string(currentFrame),
                &commandBuffer);

  const auto frameCount = static_cast<int>(_particlesBuffer.size());
  const auto previousFrame = (currentFrame + frameCount - 1) % frameCount;

  std::vector<VkBufferMemoryBarrier2> barriersBefore;
  if (previousFrame == currentFrame) {
    barriersBefore.push_back({
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = _particlesBuffer[currentFrame]->getBuffer(),
        .offset = 0,
        .size = _particlesBuffer[currentFrame]->getSize(),
    });
  } else {
    barriersBefore.push_back({
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = _particlesBuffer[previousFrame]->getBuffer(),
        .offset = 0,
        .size = _particlesBuffer[previousFrame]->getSize(),
    });
    barriersBefore.push_back({
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = _particlesBuffer[currentFrame]->getBuffer(),
        .offset = 0,
        .size = _particlesBuffer[currentFrame]->getSize(),
    });
  }
  const VkDependencyInfo dependencyBefore{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .bufferMemoryBarrierCount = static_cast<uint32_t>(barriersBefore.size()),
      .pBufferMemoryBarriers = barriersBefore.data(),
  };
  vkCmdPipelineBarrier2(commandBuffer.getCommandBuffer(), &dependencyBefore);

  const float timeDelta = _particleSystemGraphic->getFrameTimer() * 2.f;
  std::memcpy(static_cast<std::byte*>(_deltaUniformBuffer[currentFrame]->getAllocationInfo().pMappedData),
              &timeDelta, static_cast<std::size_t>(_deltaUniformBuffer[currentFrame]->getSize()));

  vkCmdBindPipeline(commandBuffer.getCommandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline->getPipeline());

  _descriptorSets[currentFrame]->bind(VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline->getPipelineLayout(), commandBuffer);

  vkCmdDispatch(commandBuffer.getCommandBuffer(),
                std::max(1, static_cast<int>(std::ceil(_particleSystemGraphic->getParticleCount() / 16.f))), 1, 1);
  logger->end(&commandBuffer);
}

void ParticleSystemCompute::update(int, const RenderGraph::CommandBuffer&) {}

void ParticleSystemCompute::reset(const std::vector<std::shared_ptr<RenderGraph::ImageView>>&) {}

void ParticleSystemGraphic::draw(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) {
  const auto logger = _engineState->getLogger();
  logger->begin("Render " + getName() + " " + std::to_string(currentFrame), &commandBuffer);

  if (_particlesBuffer.empty()) throw std::logic_error("Particle buffers are not attached to RenderGraph");
  vkCmdBindPipeline(commandBuffer.getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, _graphicPipeline->getPipeline());
  auto resolution = _engineState->getSettings()->getResolution();
  VkViewport viewport{.x = 0.0f,
                      .y = static_cast<float>(std::get<1>(resolution)),
                      .width = static_cast<float>(std::get<0>(resolution)),
                      .height = static_cast<float>(-std::get<1>(resolution)),
                      .minDepth = 0.f,
                      .maxDepth = 1.f};

  vkCmdSetViewport(commandBuffer.getCommandBuffer(), 0, 1, &viewport);

  VkRect2D scissor{.offset = {0, 0}, .extent = VkExtent2D(std::get<0>(resolution), std::get<1>(resolution))};
  vkCmdSetScissor(commandBuffer.getCommandBuffer(), 0, 1, &scissor);

  if (_graphicPipeline->getPushConstants().find("vertex") != _graphicPipeline->getPushConstants().end()) {
    VertexPush pushConstants{.pointScale = _pointScale};
    auto info = _graphicPipeline->getPushConstants().at("vertex");
    vkCmdPushConstants(commandBuffer.getCommandBuffer(), _graphicPipeline->getPipelineLayout(), info.stageFlags,
                       info.offset, info.size, &pushConstants);
  }

  BufferMVP cameraUBO{.model = getModel(),
                      .view = _gameState->getCameraManager()->getCurrentCamera()->getView(),
                      .projection = _gameState->getCameraManager()->getCurrentCamera()->getProjection()};
  std::memcpy(static_cast<std::byte*>(_cameraUniformBuffer[currentFrame]->getAllocationInfo().pMappedData), &cameraUBO,
              static_cast<std::size_t>(_cameraUniformBuffer[currentFrame]->getSize()));

  VkBuffer vertexBuffers[] = {_particlesBuffer[currentFrame]->getBuffer()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer.getCommandBuffer(), 0, 1, vertexBuffers, offsets);

  _descriptorSetGraphic[currentFrame]->bind(VK_PIPELINE_BIND_POINT_GRAPHICS, _graphicPipeline->getPipelineLayout(),
                                            commandBuffer);

  vkCmdDraw(commandBuffer.getCommandBuffer(), static_cast<uint32_t>(_particles.size()), 1, 0, 0);
  logger->end(&commandBuffer);
}

void ParticleSystemGraphic::update(int, const RenderGraph::CommandBuffer&) {}

void ParticleSystemGraphic::reset(const std::vector<std::shared_ptr<RenderGraph::ImageView>>&) {}
