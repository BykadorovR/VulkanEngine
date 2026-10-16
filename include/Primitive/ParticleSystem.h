#pragma once
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include "Primitive/Drawable.h"
#include "Utility/EngineState.h"
#include "Utility/GameState.h"
#include "Graphic/Camera.h"

import Buffer;
import Command;
import DescriptorBuffer;
import Graph;
import Pipeline;
import Texture;

struct Particle {
  glm::vec3 position;
  float radius;
  glm::vec4 color;
  glm::vec4 minColor;
  glm::vec4 maxColor;
  float life;
  float minLife;
  float maxLife;
  int iteration;
  float velocity;
  float minVelocity;
  float maxVelocity;
  alignas(16) glm::vec3 velocityDirection;
};

class ParticleSystemGraphic final : public Transformable, public RenderGraph::GraphElement {
 private:
  std::shared_ptr<EngineState> _engineState;
  std::shared_ptr<GameState> _gameState;

  std::vector<Particle> _particles;
  std::shared_ptr<RenderGraph::Texture> _texture;
  std::vector<std::shared_ptr<RenderGraph::Buffer>> _cameraUniformBuffer;
  std::vector<RenderGraph::Buffer*> _particlesBuffer;
  std::shared_ptr<RenderGraph::DescriptorSetLayout> _descriptorSetLayoutGraphic;
  std::vector<std::shared_ptr<RenderGraph::DescriptorSet>> _descriptorSetGraphic;
  std::shared_ptr<RenderGraph::Pipeline> _graphicPipeline;
  float _frameTimer = 0.f;
  float _pointScale = 60.f;

  void _initializeGraphic(const RenderGraph::CommandBuffer& commandBuffer);

 public:
  ParticleSystemGraphic(std::vector<Particle> particles,
                        std::shared_ptr<RenderGraph::Texture> texture,
                        std::shared_ptr<GameState> gameState,
                        const RenderGraph::CommandBuffer& commandBuffer,
                        std::shared_ptr<EngineState> engineState);
  void setPointScale(float pointScale);
  void updateTimer(float frameTimer);

  void draw(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) override;
  void update(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) override;
  void reset(const std::vector<std::shared_ptr<RenderGraph::ImageView>>& swapchain) override;

  std::vector<std::unique_ptr<RenderGraph::Buffer>> createParticleBuffers() const;
  void attachParticleBuffers(std::vector<RenderGraph::Buffer*> particleBuffers);
  void preserveParticleState(int frame);
  void releaseGraphResources();
  std::size_t getParticleCount() const;
  float getFrameTimer() const;
};

class ParticleSystemCompute final : public RenderGraph::GraphElement {
 private:
  std::shared_ptr<ParticleSystemGraphic> _particleSystemGraphic;
  std::shared_ptr<EngineState> _engineState;
  std::vector<std::shared_ptr<RenderGraph::Buffer>> _deltaUniformBuffer;
  std::vector<RenderGraph::Buffer*> _particlesBuffer;
  std::shared_ptr<RenderGraph::DescriptorSetLayout> _descriptorSetLayout;
  std::vector<std::shared_ptr<RenderGraph::DescriptorSet>> _descriptorSets;
  std::shared_ptr<RenderGraph::Pipeline> _pipeline;

  void _initializeDescriptors(const RenderGraph::CommandBuffer& commandBuffer);

 public:
  ParticleSystemCompute(std::shared_ptr<ParticleSystemGraphic> particleSystemGraphic,
                        std::vector<RenderGraph::Buffer*> particleBuffers,
                        const RenderGraph::CommandBuffer& commandBuffer,
                        std::shared_ptr<EngineState> engineState);
  void draw(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) override;
  void update(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) override;
  void reset(const std::vector<std::shared_ptr<RenderGraph::ImageView>>& swapchain) override;
};
