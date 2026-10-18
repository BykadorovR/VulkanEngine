#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>
#include "Engine/Core.h"
#include "Graphic/Postprocessing.h"
#include "Graphic/Shadow.h"
#include "Primitive/Drawable.h"
#include "Primitive/ParticleSystem.h"
#include "Primitive/TerrainComposition.h"
#include "Utility/GUI.h"

namespace {
class SceneRenderStage final : public RenderGraph::GraphElement {
 private:
  std::map<AlphaType, std::vector<std::shared_ptr<Drawable>>>* _drawables;
  const std::shared_ptr<Skybox>* _skybox;
  std::shared_ptr<GameState> _gameState;
  std::shared_ptr<EngineState> _engineState;

 public:
  SceneRenderStage(std::map<AlphaType, std::vector<std::shared_ptr<Drawable>>>& drawables,
                   const std::shared_ptr<Skybox>& skybox,
                   std::shared_ptr<GameState> gameState,
                   std::shared_ptr<EngineState> engineState)
      : _drawables(&drawables),
        _skybox(&skybox),
        _gameState(std::move(gameState)),
        _engineState(std::move(engineState)) {}

  void draw(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) override {
    const auto logger = _engineState->getLogger();
    _gameState->getLightManager()->draw(currentFrame, commandBuffer);

    if (*_skybox) {
      logger->begin("Render skybox " + std::to_string(currentFrame), &commandBuffer);
      (*_skybox)->draw(&commandBuffer);
      logger->end(&commandBuffer);
    }

    if (const auto opaque = _drawables->find(AlphaType::OPAQUE); opaque != _drawables->end()) {
      for (const auto& drawable : opaque->second) {
        logger->begin("Render " + drawable->getName() + " " + std::to_string(currentFrame), &commandBuffer);
        drawable->draw(&commandBuffer);
        logger->end(&commandBuffer);
      }
    }

    const auto transparent = _drawables->find(AlphaType::TRANSPARENT);
    if (transparent == _drawables->end()) return;

    auto& transparentDrawables = transparent->second;
    const auto camera = _gameState->getCameraManager()->getCurrentCamera();
    std::sort(transparentDrawables.begin(), transparentDrawables.end(),
              [&camera](const std::shared_ptr<Drawable>& left, const std::shared_ptr<Drawable>& right) {
                return glm::distance(glm::vec3(left->getModel()[3]), camera->getEye()) >
                       glm::distance(glm::vec3(right->getModel()[3]), camera->getEye());
              });
    for (const auto& drawable : transparentDrawables) {
      logger->begin("Render " + drawable->getName() + " " + std::to_string(currentFrame), &commandBuffer);
      drawable->draw(&commandBuffer);
      logger->end(&commandBuffer);
    }
  }

  void update(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) override {
    _gameState->getLightManager()->update(currentFrame, commandBuffer);
  }

  void reset(const std::vector<std::shared_ptr<RenderGraph::ImageView>>&) override {}
};

class ParticleRenderStage final : public RenderGraph::GraphElement {
 private:
  const std::vector<std::shared_ptr<ParticleSystemGraphic>>* _particleSystems;
  std::shared_ptr<GameState> _gameState;

 public:
  ParticleRenderStage(const std::vector<std::shared_ptr<ParticleSystemGraphic>>& particleSystems,
                      std::shared_ptr<GameState> gameState)
      : _particleSystems(&particleSystems),
        _gameState(std::move(gameState)) {}

  void draw(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) override {
    auto particleSystems = *_particleSystems;
    const glm::vec3 cameraPosition = _gameState->getCameraManager()->getCurrentCamera()->getEye();
    std::sort(particleSystems.begin(), particleSystems.end(),
              [&cameraPosition](const std::shared_ptr<ParticleSystemGraphic>& left,
                                const std::shared_ptr<ParticleSystemGraphic>& right) {
                const glm::vec3 leftOffset = left->getTranslate() - cameraPosition;
                const glm::vec3 rightOffset = right->getTranslate() - cameraPosition;
                return glm::dot(leftOffset, leftOffset) > glm::dot(rightOffset, rightOffset);
              });
    for (const auto& particleSystem : particleSystems) particleSystem->draw(currentFrame, commandBuffer);
  }

  void update(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) override {
    for (const auto& particleSystem : *_particleSystems) particleSystem->update(currentFrame, commandBuffer);
  }

  void reset(const std::vector<std::shared_ptr<RenderGraph::ImageView>>& swapchain) override {
    for (const auto& particleSystem : *_particleSystems) particleSystem->reset(swapchain);
  }
};
}  // namespace

void Core::_initializeFrameResources() {
  const auto settings = _engineState->getSettings();
  const auto device = _engineState->getDevice();
  const int frameCount = settings->getMaxFramesInFlight();

  // Render graph.
  if (!_tracer) {
    _tracer = std::make_unique<RenderGraph::Tracer>(*device);
  }
  _renderGraph =
      std::make_unique<RenderGraph::Graph>(std::max(1, settings->getThreadsInPool()), frameCount, *device, *_tracer);
  _renderGraph->setSwapchain(*_renderGraphSwapchain);
  _renderGraph->initialize();

  // Application command buffers.
  _applicationCommandBuffers.clear();
  _applicationCommandPool = std::make_unique<RenderGraph::CommandPool>(RenderGraph::QueueType::GRAPHICS, *device);

  _applicationCommandBuffers.reserve(frameCount);
  for (int frame = 0; frame < frameCount; ++frame) {
    _applicationCommandBuffers.emplace_back(
        std::make_unique<RenderGraph::CommandBuffer>(*_applicationCommandPool, *device));
  }

  // Per-frame fences.
  _frameFences.assign(frameCount, VK_NULL_HANDLE);
  const VkFenceCreateInfo createInfo{
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = VK_FENCE_CREATE_SIGNALED_BIT,
  };

  for (auto& fence : _frameFences) {
    if (vkCreateFence(device->getLogicalDevice(), &createInfo, nullptr, &fence) != VK_SUCCESS) {
      for (const VkFence createdFence : _frameFences) {
        if (createdFence != VK_NULL_HANDLE) vkDestroyFence(device->getLogicalDevice(), createdFence, nullptr);
      }
      _frameFences.clear();
      throw std::runtime_error("failed to create frame fence");
    }
  }
}

void Core::_addRenderGraphFlightImages(std::string_view name,
                                       const std::vector<const RenderGraph::Image*>& images,
                                       VkImageAspectFlags aspectMask,
                                       int layer) {
  std::vector<std::shared_ptr<RenderGraph::ImageView>> nativeViews;
  nativeViews.reserve(images.size());
  const auto* commandBuffer = getCommandBufferApplication();
  for (const auto* image : images) {
    if (layer < 0 || layer >= image->getLayerNumber()) {
      throw std::out_of_range("RenderGraph image layer is out of range");
    }
    auto attachmentImage = std::make_unique<RenderGraph::Image>(*_engineState->getMemoryAllocator());
    const auto resolution = image->getResolution();
    attachmentImage->wrapImage(image->getImage(), image->getFormat(), resolution, 1, 1, aspectMask,
                               image->getUsageFlags());
    const auto imageLayout = image->getImageLayout();
    if (imageLayout != VK_IMAGE_LAYOUT_GENERAL) {
      throw std::logic_error("RenderGraph flight image must use VK_IMAGE_LAYOUT_GENERAL");
    }
    const VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image->getImage(),
        .subresourceRange =
            {
                .aspectMask = image->getAspectMask(),
                .baseMipLevel = 0,
                .levelCount = static_cast<uint32_t>(image->getMipMapNumber()),
                .baseArrayLayer = static_cast<uint32_t>(layer),
                .layerCount = 1,
            },
    };
    const VkDependencyInfo dependencyInfo{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(commandBuffer->getCommandBuffer(), &dependencyInfo);
    // wrapImage does not copy the tracked layout, so keep the wrapper state consistent with the wrapped image.
    attachmentImage->changeLayout(VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_NONE, 0,
                                  VK_PIPELINE_STAGE_2_NONE, 0, *commandBuffer);
    auto attachmentView = std::make_shared<RenderGraph::ImageView>(std::move(attachmentImage),
                                                                   *_engineState->getDevice());
    attachmentView->createImageView(VK_IMAGE_VIEW_TYPE_2D, 0, layer);
    nativeViews.push_back(std::move(attachmentView));
  }
  auto holder = std::make_unique<RenderGraph::ImageViewHolder>(std::move(nativeViews),
                                                               [this]() { return _renderGraph->getFrameInFlight(); });
  _renderGraph->getGraphStorage().add(name, std::move(holder));
}

#ifdef __ANDROID__
void Core::setAssetManager(AAssetManager* assetManager) { _assetManager = assetManager; }
void Core::setNativeWindow(ANativeWindow* window) { _nativeWindow = window; }
#endif

Core::Core(std::shared_ptr<Settings> settings) : _engineState(std::make_shared<EngineState>(std::move(settings))) {}

Core::~Core() {
  if (!_frameFences.empty()) {
    const VkDevice device = _engineState->getDevice()->getLogicalDevice();
    vkDeviceWaitIdle(device);
    for (const VkFence fence : _frameFences) {
      if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, nullptr);
    }
    _frameFences.clear();
  }
}

void Core::initialize() {
#ifdef __ANDROID__
  _engineState->setNativeWindow(_nativeWindow);
  _engineState->setAssetManager(_assetManager);
#endif
  _engineState->initialize();
  const auto settings = _engineState->getSettings();
  const auto [width, height] = settings->getResolution();
  _renderGraphSwapchain = std::make_unique<RenderGraph::Swapchain>(
      glm::ivec2{width, height}, *_engineState->getMemoryAllocator(), *_engineState->getDevice(),
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
  _renderGraphSwapchain->initialize();
  settings->setSwapchainColorFormat(_renderGraphSwapchain->getSwapchain().image_format);

  const int frameCount = settings->getMaxFramesInFlight();
  if (_renderGraphSwapchain->getImageCount() < frameCount) {
    throw std::invalid_argument("Frames in flight (" + std::to_string(frameCount) +
                                ") cannot exceed the swapchain image count (" +
                                std::to_string(_renderGraphSwapchain->getImageCount()) + ")");
  }

  _threadPool = std::make_shared<BS::thread_pool>(settings->getThreadsInPool());
  _initializeFrameResources();
  _timer = std::make_shared<Timer>(_engineState);
  _timerFPSReal = std::make_shared<TimerFPS>();
  _timerFPSLimited = std::make_shared<TimerFPS>();

  const auto debugUtils = _engineState->getDebugUtils();
  const auto device = _engineState->getDevice();
  debugUtils->setName("Queue graphic", VkObjectType::VK_OBJECT_TYPE_QUEUE,
                      device->getQueue(RenderGraph::QueueType::GRAPHICS));
  debugUtils->setName("Queue present", VkObjectType::VK_OBJECT_TYPE_QUEUE,
                      device->getQueue(RenderGraph::QueueType::PRESENT));
  debugUtils->setName("Queue compute", VkObjectType::VK_OBJECT_TYPE_QUEUE,
                      device->getQueue(RenderGraph::QueueType::COMPUTE));
  for (const auto& commandBuffer : _applicationCommandBuffers) {
    debugUtils->setName("Command buffer for application", VkObjectType::VK_OBJECT_TYPE_COMMAND_BUFFER,
                        commandBuffer->getCommandBuffer());
  }

  _gameState = std::make_shared<GameState>(getCommandBufferApplication(), _engineState);
}

void Core::_resetRenderGraph() {
  const auto device = _engineState->getDevice();
  const int currentFrame = _engineState->getFrameInFlight();
  auto resolution = _engineState->getWindow()->getResolution();
#ifndef __ANDROID__
  auto* window = static_cast<GLFWwindow*>(_engineState->getWindow()->getWindow());
  while ((resolution.x == 0 || resolution.y == 0) && !glfwWindowShouldClose(window)) {
    glfwWaitEvents();
    resolution = _engineState->getWindow()->getResolution();
  }
  if (glfwWindowShouldClose(window)) return;
#else
  if (resolution.x == 0 || resolution.y == 0) {
    const VkSubmitInfo2 frameSubmitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    if (vkQueueSubmit2(device->getQueue(RenderGraph::QueueType::GRAPHICS), 1, &frameSubmitInfo,
                       _frameFences.at(currentFrame)) != VK_SUCCESS) {
      throw std::runtime_error("failed to submit frame fence");
    }
    return;
  }
#endif

  // Submit pending uploads before destroying their command pool.
  auto& applicationCommandBuffer = *_applicationCommandBuffers.at(currentFrame);
  if (applicationCommandBuffer.getActive()) {
    applicationCommandBuffer.endCommands();
    const VkCommandBufferSubmitInfo commandBufferInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = applicationCommandBuffer.getCommandBuffer(),
    };
    const VkSubmitInfo2 applicationSubmitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandBufferInfo,
    };
    if (vkQueueSubmit2(device->getQueue(RenderGraph::QueueType::GRAPHICS), 1, &applicationSubmitInfo, nullptr) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to submit application commands");
    }
  }

  if (vkDeviceWaitIdle(device->getLogicalDevice()) != VK_SUCCESS) {
    throw std::runtime_error("failed to reset render graph");
  }

  const auto settings = _engineState->getSettings();
  const int frameCount = settings->getMaxFramesInFlight();
  const int latestParticleFrame = (currentFrame + frameCount - 1) % frameCount;
  for (const auto& particleSystem : _particleSystems) {
    particleSystem->preserveParticleState(latestParticleFrame);
    particleSystem->releaseGraphResources();
  }

  for (const VkFence fence : _frameFences) {
    if (fence != VK_NULL_HANDLE) vkDestroyFence(device->getLogicalDevice(), fence, nullptr);
  }
  _frameFences.clear();
  _applicationCommandBuffers.clear();
  _applicationCommandPool.reset();
  _renderGraph.reset();
  _renderGraphSwapchain->reset(resolution);

  const auto& extent = _renderGraphSwapchain->getSwapchain().extent;
  settings->setResolution({extent.width, extent.height});
  settings->setSwapchainColorFormat(_renderGraphSwapchain->getSwapchain().image_format);

  _initializeFrameResources();
  if (_postprocessing) {
    std::vector<std::shared_ptr<RenderGraph::ImageView>> graphicImageViews;
    graphicImageViews.reserve(frameCount);
    for (int frame = 0; frame < frameCount; ++frame) {
      auto image = std::make_unique<RenderGraph::Image>(*_engineState->getMemoryAllocator());
      image->createImage(settings->getGraphicColorFormat(), {extent.width, extent.height}, 1, 1,
                         VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
      auto imageView = std::make_shared<RenderGraph::ImageView>(std::move(image), *device);
      imageView->createImageView(VK_IMAGE_VIEW_TYPE_2D, 0, 0);
      graphicImageViews.push_back(std::move(imageView));
    }
    _graphicImageViews = std::move(graphicImageViews);
  }
  _engineState->setFrameInFlight(_renderGraph->getFrameInFlight());

  // Reset resources which depend on the swapchain.
  if (_postprocessing) {
    _postprocessing->reset(_graphicImageViews, _renderGraphSwapchain->getImageViews(), *getCommandBufferApplication());
  }
  if (_gui) _gui->reset();
  if (_resetCallback) {
    _resetCallback(extent.width, extent.height);
  }
  _buildRenderGraph();
}

void Core::_buildRenderGraph() {
  if (_renderGraph->getPassGraphic("Render") != nullptr) {
    const auto device = _engineState->getDevice();
    if (vkDeviceWaitIdle(device->getLogicalDevice()) != VK_SUCCESS) {
      throw std::runtime_error("failed to rebuild render graph");
    }

    const auto settings = _engineState->getSettings();
    const int frameCount = settings->getMaxFramesInFlight();
    const int latestParticleFrame = (_renderGraph->getFrameInFlight() + frameCount - 1) % frameCount;
    for (const auto& particleSystem : _particleSystems) {
      particleSystem->preserveParticleState(latestParticleFrame);
      particleSystem->releaseGraphResources();
    }

    _renderGraph =
        std::make_unique<RenderGraph::Graph>(std::max(1, settings->getThreadsInPool()), frameCount, *device, *_tracer);
    _renderGraph->setSwapchain(*_renderGraphSwapchain);
    _renderGraph->initialize();
    _engineState->setFrameInFlight(_renderGraph->getFrameInFlight());
  }

  constexpr int cubemapFaceCount = 6;
  const auto collectTextureImages = [](const auto& textures) {
    std::vector<const RenderGraph::Image*> images;
    images.reserve(textures.size());
    for (const auto& texture : textures) {
      images.push_back(&texture->getImageView().getImage());
    }
    return images;
  };

  // Directional shadows.
  const auto& directionalShadows = _gameState->getLightManager()->getDirectionalShadows();
  for (int shadowIndex = 0; shadowIndex < static_cast<int>(directionalShadows.size()); ++shadowIndex) {
    const auto& shadow = directionalShadows[shadowIndex];
    if (!shadow) continue;

    const std::string shadowName = "Directional shadow " + std::to_string(shadowIndex);
    auto& shadowPass = _renderGraph->createPassGraphic(shadowName);
    shadowPass.registerGraphElement(
        std::make_shared<DirectionalShadowRenderer>(shadowIndex, _shadowables, _engineState));

    const auto shadowImages = collectTextureImages(shadow->getShadowMapTexture());
    _addRenderGraphFlightImages(shadowName, shadowImages);
    shadowPass.addColorTarget(shadowName, RenderGraph::RenderTargetLoadAction::CLEAR);

    const auto blurIterator = _directionalShadowBlurPasses.find(shadow);
    if (blurIterator == _directionalShadowBlurPasses.end()) continue;

    const auto& blurPasses = blurIterator->second;
    for (int blurIndex = 0; blurIndex < static_cast<int>(blurPasses.size()); ++blurIndex) {
      const auto& [horizontalBlur, verticalBlur] = blurPasses[blurIndex];
      const auto sourceImages = collectTextureImages(horizontalBlur->getTextureSrc());
      const auto horizontalImages = collectTextureImages(horizontalBlur->getTextureDst());
      const std::string horizontalInputName =
          blurIndex == 0 ? shadowName : shadowName + " blur vertical output " + std::to_string(blurIndex - 1);
      const std::string horizontalOutputName = shadowName + " blur horizontal output " + std::to_string(blurIndex);

      auto& horizontalPass = _renderGraph->createPassGraphic(shadowName + " blur horizontal " +
                                                             std::to_string(blurIndex));
      horizontalPass.registerGraphElement(horizontalBlur);
      _addRenderGraphFlightImages(horizontalInputName, sourceImages);
      _addRenderGraphFlightImages(horizontalOutputName, horizontalImages);
      horizontalPass.addTextureInput(horizontalInputName);
      horizontalPass.addColorTarget(horizontalOutputName, RenderGraph::RenderTargetLoadAction::CLEAR);

      const auto verticalImages = collectTextureImages(verticalBlur->getTextureDst());
      const std::string verticalOutputName = shadowName + " blur vertical output " + std::to_string(blurIndex);
      auto& verticalPass = _renderGraph->createPassGraphic(shadowName + " blur vertical " + std::to_string(blurIndex));
      verticalPass.registerGraphElement(verticalBlur);
      _addRenderGraphFlightImages(verticalOutputName, verticalImages);
      verticalPass.addTextureInput(horizontalOutputName);
      verticalPass.addColorTarget(verticalOutputName, RenderGraph::RenderTargetLoadAction::CLEAR);
    }
  }
  // Point shadows.
  const auto& pointShadows = _gameState->getLightManager()->getPointShadows();
  for (int shadowIndex = 0; shadowIndex < static_cast<int>(pointShadows.size()); ++shadowIndex) {
    const auto& shadow = pointShadows[shadowIndex];
    if (!shadow) continue;

    const std::string shadowName = "Point shadow " + std::to_string(shadowIndex);
    std::vector<const RenderGraph::Image*> shadowImages;
    shadowImages.reserve(shadow->getShadowMapCubemap().size());
    for (const auto& cubemap : shadow->getShadowMapCubemap()) {
      shadowImages.push_back(&cubemap->getTexture()->getImageView().getImage());
    }

    for (int face = 0; face < cubemapFaceCount; ++face) {
      const std::string faceName = shadowName + " face " + std::to_string(face);
      auto& shadowPass = _renderGraph->createPassGraphic(faceName);
      shadowPass.registerGraphElement(
          std::make_shared<PointShadowRenderer>(shadow, shadowIndex, face, _shadowables, _engineState));
      _addRenderGraphFlightImages(faceName, shadowImages, VK_IMAGE_ASPECT_COLOR_BIT, face);
      shadowPass.addColorTarget(faceName, RenderGraph::RenderTargetLoadAction::CLEAR);
    }

    const auto blurIterator = _pointShadowBlurPasses.find(shadow);
    if (blurIterator == _pointShadowBlurPasses.end()) continue;

    const auto textureIterator = _pointShadowBlurTextures.find(shadow);
    if (textureIterator == _pointShadowBlurTextures.end() ||
        textureIterator->second.size() != blurIterator->second.size()) {
      throw std::logic_error("Point shadow blur resources are inconsistent");
    }

    const auto& blurPasses = blurIterator->second;
    const auto& blurTextures = textureIterator->second;
    for (int blurIndex = 0; blurIndex < static_cast<int>(blurPasses.size()); ++blurIndex) {
      const auto& [horizontalBlurs, verticalBlurs] = blurPasses[blurIndex];
      const auto& [sourceTextures, targetTextures] = blurTextures[blurIndex];
      const auto sourceImages = collectTextureImages(sourceTextures);
      const auto targetImages = collectTextureImages(targetTextures);

      for (int face = 0; face < cubemapFaceCount; ++face) {
        const std::string faceSuffix = " face " + std::to_string(face);
        const std::string horizontalInputName = blurIndex == 0 ? shadowName + faceSuffix
                                                               : shadowName + " blur vertical output " +
                                                                     std::to_string(blurIndex - 1) + faceSuffix;
        const std::string horizontalOutputName = shadowName + " blur horizontal output " + std::to_string(blurIndex) +
                                                 faceSuffix;
        auto& horizontalPass = _renderGraph->createPassGraphic(shadowName + " blur horizontal " +
                                                               std::to_string(blurIndex) + faceSuffix);
        horizontalPass.registerGraphElement(horizontalBlurs.at(face));
        _addRenderGraphFlightImages(horizontalOutputName, targetImages, VK_IMAGE_ASPECT_COLOR_BIT, face);
        horizontalPass.addTextureInput(horizontalInputName);
        horizontalPass.addColorTarget(horizontalOutputName, RenderGraph::RenderTargetLoadAction::CLEAR);

        const std::string verticalOutputName = shadowName + " blur vertical output " + std::to_string(blurIndex) +
                                               faceSuffix;
        auto& verticalPass = _renderGraph->createPassGraphic(shadowName + " blur vertical " +
                                                             std::to_string(blurIndex) + faceSuffix);
        verticalPass.registerGraphElement(verticalBlurs.at(face));
        _addRenderGraphFlightImages(verticalOutputName, sourceImages, VK_IMAGE_ASPECT_COLOR_BIT, face);
        verticalPass.addTextureInput(horizontalOutputName);
        verticalPass.addColorTarget(verticalOutputName, RenderGraph::RenderTargetLoadAction::CLEAR);
      }
    }
  }
  std::vector<std::string> particleBufferNames;
  particleBufferNames.reserve(_particleSystems.size());
  auto& graphStorage = _renderGraph->getGraphStorage();
  for (int particleIndex = 0; particleIndex < static_cast<int>(_particleSystems.size()); ++particleIndex) {
    const auto& particleSystem = _particleSystems[particleIndex];
    const std::string bufferName = "Particles " + std::to_string(particleIndex);
    auto particleBuffers = particleSystem->createParticleBuffers();
    graphStorage.add(bufferName, particleBuffers);
    particleSystem->attachParticleBuffers(graphStorage.getBuffer(bufferName));
    particleBufferNames.push_back(bufferName);
  }

  auto swapchainHolder = std::make_unique<RenderGraph::ImageViewHolder>(
      _renderGraphSwapchain->getImageViews(),
      [this]() { return static_cast<int>(_renderGraphSwapchain->getSwapchainIndex()); });
  graphStorage.add("Swapchain", std::move(swapchainHolder));
  if (_postprocessing) {
    auto graphicHolder = std::make_unique<RenderGraph::ImageViewHolder>(
        _graphicImageViews, [this]() { return _renderGraph->getFrameInFlight(); });
    graphStorage.add("Graphic", std::move(graphicHolder));
  }

  const auto settings = _engineState->getSettings();
  const auto [width, height] = settings->getResolution();
  auto depthImage = std::make_unique<RenderGraph::Image>(*_engineState->getMemoryAllocator());
  depthImage->createImage(settings->getDepthFormat(), {width, height}, 1, 1, VK_IMAGE_ASPECT_DEPTH_BIT,
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
  auto depthImageView = std::make_shared<RenderGraph::ImageView>(std::move(depthImage), *_engineState->getDevice());
  depthImageView->createImageView(VK_IMAGE_VIEW_TYPE_2D, 0, 0);
  std::vector<std::shared_ptr<RenderGraph::ImageView>> depthImageViews{std::move(depthImageView)};
  auto depthHolder = std::make_unique<RenderGraph::ImageViewHolder>(std::move(depthImageViews), []() { return 0; });
  graphStorage.add("Depth", std::move(depthHolder));

  const std::string renderTarget = _postprocessing ? "Graphic" : "Swapchain";
  auto& renderPass = _renderGraph->createPassGraphic("Render");
  renderPass.registerGraphElement(std::make_shared<SceneRenderStage>(_drawables, _skybox, _gameState, _engineState));

  for (int shadowIndex = 0; shadowIndex < static_cast<int>(directionalShadows.size()); ++shadowIndex) {
    const auto& shadow = directionalShadows[shadowIndex];
    if (!shadow) continue;

    const auto blurIterator = _directionalShadowBlurPasses.find(shadow);
    const std::string shadowName = "Directional shadow " + std::to_string(shadowIndex);
    if (blurIterator != _directionalShadowBlurPasses.end() && !blurIterator->second.empty()) {
      renderPass.addTextureInput(shadowName + " blur vertical output " +
                                 std::to_string(blurIterator->second.size() - 1));
    } else {
      renderPass.addTextureInput(shadowName);
    }
  }

  for (int shadowIndex = 0; shadowIndex < static_cast<int>(pointShadows.size()); ++shadowIndex) {
    const auto& shadow = pointShadows[shadowIndex];
    if (!shadow) continue;

    const auto blurIterator = _pointShadowBlurPasses.find(shadow);
    const bool hasBlur = blurIterator != _pointShadowBlurPasses.end() && !blurIterator->second.empty();
    const std::string shadowName = "Point shadow " + std::to_string(shadowIndex);
    for (int face = 0; face < cubemapFaceCount; ++face) {
      const std::string faceSuffix = " face " + std::to_string(face);
      if (hasBlur) {
        renderPass.addTextureInput(shadowName + " blur vertical output " +
                                   std::to_string(blurIterator->second.size() - 1) + faceSuffix);
      } else {
        renderPass.addTextureInput(shadowName + faceSuffix);
      }
    }
  }
  renderPass.addColorTarget(renderTarget, RenderGraph::RenderTargetLoadAction::CLEAR);
  renderPass.setDepthTarget("Depth", RenderGraph::RenderTargetLoadAction::CLEAR);

  if (!_particleSystems.empty()) {
    auto& particleComputePass = _renderGraph->createPassCompute("Particle compute", false);
    auto& particleDrawPass = _renderGraph->createPassGraphic("Particles");
    for (std::size_t particleIndex = 0; particleIndex < _particleSystems.size(); ++particleIndex) {
      const auto& particleSystem = _particleSystems[particleIndex];
      const auto& bufferName = particleBufferNames[particleIndex];
      particleComputePass.registerGraphElement(std::make_shared<ParticleSystemCompute>(
          particleSystem, graphStorage.getBuffer(bufferName), *getCommandBufferApplication(), _engineState));
      particleComputePass.addStorageBufferInput(bufferName);
      particleComputePass.addStorageBufferOutput(bufferName);
      particleDrawPass.addVertexBufferInput(bufferName);
    }
    particleDrawPass.registerGraphElement(std::make_shared<ParticleRenderStage>(_particleSystems, _gameState));
    particleDrawPass.addColorTarget(renderTarget, RenderGraph::RenderTargetLoadAction::KEEP);
    particleDrawPass.setDepthTarget("Depth", RenderGraph::RenderTargetLoadAction::KEEP);
  }

  if (_postprocessing) {
    auto& postprocessingPass = _renderGraph->createPassCompute("Postprocessing", false);
    postprocessingPass.registerGraphElement(_postprocessing);
    postprocessingPass.addStorageTextureInput("Graphic");
    postprocessingPass.addStorageTextureOutput("Swapchain");
  }

  if (_gui) {
    auto& guiPass = _renderGraph->createPassGraphic("GUI");
    guiPass.registerGraphElement(_gui);
    guiPass.addColorTarget("Swapchain", RenderGraph::RenderTargetLoadAction::KEEP);
  }

  _renderGraph->calculate();
  _renderGraph->print();
  _renderGraphDirty = false;
}

void Core::draw() {
#ifdef __ANDROID__
  {
#else
  auto* window = static_cast<GLFWwindow*>(_engineState->getWindow()->getWindow());
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
#endif
    if (_renderGraphDirty) _buildRenderGraph();

    _timer->tick();
    _timerFPSReal->tick();
    _timerFPSLimited->tick();

    const int frameInFlight = _renderGraph->getFrameInFlight();
    const auto device = _engineState->getDevice();
    const VkDevice logicalDevice = device->getLogicalDevice();
    const VkFence frameFence = _frameFences.at(frameInFlight);
    if (vkWaitForFences(logicalDevice, 1, &frameFence, VK_TRUE, std::numeric_limits<uint64_t>::max()) != VK_SUCCESS) {
      throw std::runtime_error("failed to wait for frame fence");
    }
    if (vkResetFences(logicalDevice, 1, &frameFence) != VK_SUCCESS) {
      throw std::runtime_error("failed to reset frame fence");
    }
    _engineState->setFrameInFlight(frameInFlight);
    _engineState->getResourceUploader()->reclaim();

    const auto* commandBufferApplication = getCommandBufferApplication();
    // Release resources deferred until this frame in flight becomes available again.
    _deferredShadowables[frameInFlight].clear();
    _deferredDrawables[frameInFlight].clear();
    if (_updateCallback) _updateCallback(commandBufferApplication);
    for (const auto& material : _materials) {
      material->update(frameInFlight, *commandBufferApplication);
    }
    _gameState->getCameraManager()->update();
    for (const auto& animation : _animations) {
      auto& future = _futureAnimationUpdate[animation];
      if (future.valid()) future.get();
      animation->updateBuffers(frameInFlight);
    }

    auto& applicationCommandBuffer = *_applicationCommandBuffers.at(frameInFlight);
    applicationCommandBuffer.endCommands();
    const VkCommandBufferSubmitInfo commandBufferInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = applicationCommandBuffer.getCommandBuffer(),
    };
    const VkSubmitInfo2 applicationSubmitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandBufferInfo,
    };
    if (vkQueueSubmit2(device->getQueue(RenderGraph::QueueType::GRAPHICS), 1, &applicationSubmitInfo, nullptr) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to submit application commands");
    }

    if (_renderGraph->render()) {
      _resetRenderGraph();
#ifdef __ANDROID__
      return;
#else
      continue;
#endif
    }
    for (const auto& particleSystem : _particleSystems) {
      particleSystem->updateTimer(_timer->getElapsedCurrent());
    }

    const auto logger = _engineState->getLogger();
    const auto globalFrame = _timer->getFrameCounter();
    for (const auto& animation : _animations) {
      _futureAnimationUpdate[animation] = _threadPool->submit([this, animation, logger, frame = globalFrame]() {
        logger->begin("Calculate animation joints " + std::to_string(frame));
        // Calculate the next frame while the GPU is using the current one.
        animation->calculateJoints(_timer->getElapsedCurrent());
        logger->end();
      });
    }
    const VkSubmitInfo2 frameSubmitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    if (vkQueueSubmit2(device->getQueue(RenderGraph::QueueType::GRAPHICS), 1, &frameSubmitInfo,
                       _frameFences.at(frameInFlight)) != VK_SUCCESS) {
      throw std::runtime_error("failed to submit frame fence");
    }

    _timerFPSReal->tock();
    _timer->sleep(_engineState->getSettings()->getDesiredFPS());
    _timer->tock();
    _timerFPSLimited->tock();
  }
#ifndef __ANDROID__
  if (vkDeviceWaitIdle(_engineState->getDevice()->getLogicalDevice()) != VK_SUCCESS) {
    throw std::runtime_error("failed to wait for the device after rendering");
  }
#endif
}

void Core::registerUpdate(std::function<void(const RenderGraph::CommandBuffer*)> update) {
  _updateCallback = std::move(update);
}

void Core::registerReset(std::function<void(int, int)> reset) { _resetCallback = std::move(reset); }

void Core::setCamera(std::shared_ptr<Camera> camera) {
  _gameState->getCameraManager()->setCurrentCamera(std::move(camera));
}

void Core::addDrawable(std::shared_ptr<Drawable> drawable, AlphaType type) {
  if (!drawable) return;

  auto& drawables = _drawables[type];
  if (std::find(drawables.begin(), drawables.end(), drawable) == drawables.end()) {
    drawables.push_back(std::move(drawable));
  }
}

void Core::addShadowable(std::shared_ptr<Shadowable> shadowable) {
  if (shadowable && std::find(_shadowables.begin(), _shadowables.end(), shadowable) == _shadowables.end()) {
    _shadowables.push_back(std::move(shadowable));
  }
}

void Core::addSkybox(std::shared_ptr<Skybox> skybox) {
  if (_skybox == skybox) return;
  _skybox = std::move(skybox);
}

void Core::removeDrawable(std::shared_ptr<Drawable> drawable) {
  if (!drawable) return;

  for (auto& drawableEntry : _drawables) {
    auto& drawables = drawableEntry.second;
    const auto position = std::find(drawables.begin(), drawables.end(), drawable);
    if (position == drawables.end()) continue;

    // Keep the object alive until the GPU finishes the current frame.
    const int deferredFrame = (_engineState->getFrameInFlight() + 1) %
                              _engineState->getSettings()->getMaxFramesInFlight();
    _deferredDrawables[deferredFrame].push_back(*position);
    drawables.erase(position);
    return;
  }
}

void Core::removeShadowable(std::shared_ptr<Shadowable> shadowable) {
  if (!shadowable) return;

  const auto position = std::find(_shadowables.begin(), _shadowables.end(), shadowable);
  if (position == _shadowables.end()) return;

  // Keep the object alive until the GPU finishes the current frame.
  const int deferredFrame = (_engineState->getFrameInFlight() + 1) %
                            _engineState->getSettings()->getMaxFramesInFlight();
  _deferredShadowables[deferredFrame].push_back(*position);
  _shadowables.erase(position);
}

std::shared_ptr<ImageCPU<uint8_t>> Core::loadImageCPU(std::string path) {
  return _gameState->getResourceManager()->getImageManager().loadCPU<uint8_t>(std::move(path));
}

std::shared_ptr<ImageDataBuffer> Core::loadImageGPU(std::shared_ptr<ImageCPU<uint8_t>> imageCPU) {
  return _gameState->getResourceManager()->getImageManager().loadGPU<uint8_t>({imageCPU});
}

std::shared_ptr<RenderGraph::Texture> Core::createTexture(std::string path, VkFormat format, int mipMapLevels) {
  return _gameState->getResourceManager()->getImageManager().createTexture(loadImageGPU(loadImageCPU(std::move(path))),
                                                                           format, VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                                                           mipMapLevels, VK_FILTER_LINEAR);
}

std::shared_ptr<Cubemap> Core::createCubemap(std::vector<std::string> paths, VkFormat format, int mipMapLevels) {
  std::vector<std::shared_ptr<ImageCPU<uint8_t>>> images;
  images.reserve(paths.size());
  for (auto& path : paths) {
    images.push_back(loadImageCPU(std::move(path)));
  }
  return std::make_shared<Cubemap>(
      _gameState->getResourceManager()->getImageManager().loadGPU<uint8_t>(images), format, mipMapLevels,
      VK_IMAGE_ASPECT_COLOR_BIT,
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_FILTER_LINEAR,
      _engineState);
}

std::shared_ptr<ModelGLTF> Core::createModelGLTF(std::string path) {
  auto model = _gameState->getResourceManager()->loadModel(std::move(path));
  _materials.insert(model->getMaterialsColor().begin(), model->getMaterialsColor().end());
  _materials.insert(model->getMaterialsPhong().begin(), model->getMaterialsPhong().end());
  _materials.insert(model->getMaterialsPBR().begin(), model->getMaterialsPBR().end());
  return model;
}

std::shared_ptr<Animation> Core::createAnimation(std::shared_ptr<ModelGLTF> modelGLTF) {
  auto animation = std::make_shared<Animation>(modelGLTF->getNodes(), modelGLTF->getSkins(), modelGLTF->getAnimations(),
                                               _engineState);
  _animations.push_back(animation);
  return animation;
}

std::shared_ptr<Equirectangular> Core::createEquirectangular(std::string path) {
  return std::make_shared<Equirectangular>(
      _gameState->getResourceManager()->getImageManager().loadCPU<float>(std::move(path)),
      getCommandBufferApplication(), _engineState);
}

std::shared_ptr<MaterialColor> Core::createMaterialColor(MaterialTarget target) {
  auto material = std::make_shared<MaterialColor>(target, _engineState);
  _materials.insert(material);
  return material;
}

std::shared_ptr<MaterialPhong> Core::createMaterialPhong(MaterialTarget target) {
  auto material = std::make_shared<MaterialPhong>(target, _engineState);
  _materials.insert(material);
  return material;
}

std::shared_ptr<MaterialPBR> Core::createMaterialPBR(MaterialTarget target) {
  auto material = std::make_shared<MaterialPBR>(target, _engineState);
  _materials.insert(material);
  return material;
}

std::shared_ptr<Shape3D> Core::createShape3D(ShapeType shapeType,
                                             std::shared_ptr<Mesh3D> mesh,
                                             VkCullModeFlagBits cullMode) {
  return std::make_shared<Shape3D>(shapeType, std::move(mesh), cullMode, getCommandBufferApplication(), _gameState,
                                   _engineState);
}

std::shared_ptr<Model3D> Core::createModel3D(std::shared_ptr<ModelGLTF> modelGLTF) {
  return std::make_shared<Model3D>(modelGLTF->getNodes(), modelGLTF->getMeshes(), getCommandBufferApplication(),
                                   _gameState, _engineState);
}

std::shared_ptr<Sprite> Core::createSprite() {
  return std::make_shared<Sprite>(getCommandBufferApplication(), _gameState, _engineState);
}

std::shared_ptr<TerrainGPU> Core::createTerrainComposition(std::shared_ptr<ImageCPU<uint8_t>> heightmap) {
  return std::make_shared<TerrainComposition>(std::move(heightmap), _gameState, _engineState);
}

std::shared_ptr<TerrainCPU> Core::createTerrainCPU(std::shared_ptr<ImageCPU<uint8_t>> heightmap) {
  return std::make_shared<TerrainCPU>(std::move(heightmap), _gameState, _engineState);
}

std::shared_ptr<TerrainCPU> Core::createTerrainCPU(std::vector<float> heights, std::tuple<int, int> resolution) {
  return std::make_shared<TerrainCPU>(std::move(heights), resolution, _gameState, _engineState);
}

std::shared_ptr<Line> Core::createLine() {
  return std::make_shared<Line>(getCommandBufferApplication(), _gameState, _engineState);
}

std::shared_ptr<IBL> Core::createIBL() {
  return std::make_shared<IBL>(getCommandBufferApplication(), _gameState, _engineState);
}

std::shared_ptr<ParticleSystemGraphic> Core::createParticleSystem(
    std::vector<Particle> particles,
    std::shared_ptr<RenderGraph::Texture> particleTexture) {
  auto particleSystem = std::make_shared<ParticleSystemGraphic>(
      std::move(particles), std::move(particleTexture), _gameState, *getCommandBufferApplication(), _engineState);
  _particleSystems.push_back(particleSystem);
  _renderGraphDirty = true;

  return particleSystem;
}

std::shared_ptr<Skybox> Core::createSkybox() { return std::make_shared<Skybox>(_gameState, _engineState); }

std::shared_ptr<PhysicsManager> Core::createPhysicsManager() {
  return std::make_shared<PhysicsManager>(_threadPool, _engineState);
}

std::shared_ptr<GUI> Core::createGUI() {
  _gui = std::make_shared<GUI>(_engineState);
  _gui->initialize(getCommandBufferApplication());
  _engineState->getInput()->subscribe(std::dynamic_pointer_cast<InputSubscriberExclusive>(_gui));
  _renderGraphDirty = true;

  return _gui;
}

std::shared_ptr<Postprocessing> Core::createPostprocessing() {
  const auto settings = _engineState->getSettings();
  const auto [width, height] = settings->getResolution();
  if (width <= 0 || height <= 0) {
    throw std::invalid_argument("Graphic image resolution must be positive");
  }

  std::vector<std::shared_ptr<RenderGraph::ImageView>> graphicImageViews;
  graphicImageViews.reserve(settings->getMaxFramesInFlight());
  for (int frame = 0; frame < settings->getMaxFramesInFlight(); ++frame) {
    auto image = std::make_unique<RenderGraph::Image>(*_engineState->getMemoryAllocator());
    image->createImage(settings->getGraphicColorFormat(), {width, height}, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
    auto imageView = std::make_shared<RenderGraph::ImageView>(std::move(image), *_engineState->getDevice());
    imageView->createImageView(VK_IMAGE_VIEW_TYPE_2D, 0, 0);
    graphicImageViews.push_back(std::move(imageView));
  }
  _graphicImageViews = std::move(graphicImageViews);

  _postprocessing = std::make_shared<Postprocessing>(_graphicImageViews, _renderGraphSwapchain->getImageViews(),
                                                     *getCommandBufferApplication(), _engineState,
                                                     *_renderGraphSwapchain);
  _renderGraphDirty = true;
  return _postprocessing;
}

std::shared_ptr<PointShadowResources> Core::createPointShadow(std::shared_ptr<PointLight> pointLight, bool blur) {
  auto shadow = _gameState->getLightManager()->createPointShadow(std::move(pointLight), getCommandBufferApplication());
  _renderGraphDirty = true;
  if (!blur) return shadow;

  constexpr int cubemapFaceCount = 6;
  const auto settings = _engineState->getSettings();
  const auto device = _engineState->getDevice();
  const auto* commandBuffer = getCommandBufferApplication();
  const int frameCount = settings->getMaxFramesInFlight();
  const VkFormat shadowFormat = settings->getShadowMapFormat();
  const auto& shadowCubemaps = shadow->getShadowMapCubemap();
  const auto imageResolution = shadowCubemaps.at(0)->getTexture()->getImageView().getImage().getResolution();
  const auto resolution = std::tuple{imageResolution.x, imageResolution.y};
  const VkFilter filter = device->isFormatFeatureSupported(shadowFormat, VK_IMAGE_TILING_OPTIMAL,
                                                           VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
                              ? VK_FILTER_LINEAR
                              : VK_FILTER_NEAREST;

  std::vector<std::shared_ptr<Cubemap>> blurCubemaps;
  blurCubemaps.reserve(frameCount);
  for (int frame = 0; frame < frameCount; ++frame) {
    blurCubemaps.push_back(std::make_shared<Cubemap>(
        resolution, shadowFormat, 1, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, filter, commandBuffer, _engineState));
  }

  std::vector<std::shared_ptr<BlurSeparate>> horizontalBlurs(cubemapFaceCount);
  std::vector<std::shared_ptr<BlurSeparate>> verticalBlurs(cubemapFaceCount);
  for (int face = 0; face < cubemapFaceCount; ++face) {
    std::vector<std::shared_ptr<RenderGraph::Texture>> sourceTextures;
    std::vector<std::shared_ptr<RenderGraph::Texture>> targetTextures;
    sourceTextures.reserve(frameCount);
    targetTextures.reserve(frameCount);
    for (int frame = 0; frame < frameCount; ++frame) {
      sourceTextures.push_back(shadowCubemaps.at(frame)->getTextureSeparate().at(face).at(0));
      targetTextures.push_back(blurCubemaps.at(frame)->getTextureSeparate().at(face).at(0));
    }

    auto horizontalBlur = std::make_shared<BlurGraphicSeparate>(true, sourceTextures, targetTextures, commandBuffer,
                                                                _engineState, face);
    auto verticalBlur = std::make_shared<BlurGraphicSeparate>(
        false, horizontalBlur->getTextureDst(), horizontalBlur->getTextureSrc(), commandBuffer, _engineState, face);
    horizontalBlurs[face] = std::move(horizontalBlur);
    verticalBlurs[face] = std::move(verticalBlur);
  }
  _pointShadowBlurPasses[shadow] = {{std::move(horizontalBlurs), std::move(verticalBlurs)}};

  std::vector<std::shared_ptr<RenderGraph::Texture>> sourceCubemapTextures;
  std::vector<std::shared_ptr<RenderGraph::Texture>> targetCubemapTextures;
  sourceCubemapTextures.reserve(frameCount);
  targetCubemapTextures.reserve(frameCount);
  for (int frame = 0; frame < frameCount; ++frame) {
    sourceCubemapTextures.push_back(shadowCubemaps.at(frame)->getTexture());
    targetCubemapTextures.push_back(blurCubemaps.at(frame)->getTexture());
  }
  _pointShadowBlurTextures[shadow] = {{std::move(sourceCubemapTextures), std::move(targetCubemapTextures)}};

  return shadow;
}

std::shared_ptr<DirectionalShadowResources> Core::createDirectionalShadow(
    std::shared_ptr<DirectionalLight> directionalLight,
    bool blur) {
  auto shadow = _gameState->getLightManager()->createDirectionalShadow(std::move(directionalLight),
                                                                       getCommandBufferApplication());
  _renderGraphDirty = true;
  if (!blur) return shadow;

  const auto settings = _engineState->getSettings();
  const auto device = _engineState->getDevice();
  const auto* commandBuffer = getCommandBufferApplication();
  const int frameCount = settings->getMaxFramesInFlight();
  const VkFormat shadowFormat = settings->getShadowMapFormat();
  const auto& shadowTextures = shadow->getShadowMapTexture();
  const auto resolution = shadowTextures.at(0)->getImageView().getImage().getResolution();
  const VkFilter filter = device->isFormatFeatureSupported(shadowFormat, VK_IMAGE_TILING_OPTIMAL,
                                                           VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
                              ? VK_FILTER_LINEAR
                              : VK_FILTER_NEAREST;

  std::vector<std::shared_ptr<RenderGraph::Texture>> blurTextures;
  blurTextures.reserve(frameCount);
  for (int frame = 0; frame < frameCount; ++frame) {
    auto blurImage = std::make_unique<RenderGraph::Image>(*_engineState->getMemoryAllocator());
    blurImage->createImage(shadowFormat, resolution, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    blurImage->changeLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                            VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                            VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, *commandBuffer);

    auto blurImageView = std::make_shared<RenderGraph::ImageView>(std::move(blurImage), *device);
    blurImageView->createImageView(VK_IMAGE_VIEW_TYPE_2D, 0, 0);
    auto blurSampler = std::make_shared<RenderGraph::Sampler>(*device);
    blurSampler->createSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, 1, settings->getAnisotropicSamples(), filter);
    blurTextures.push_back(std::make_shared<RenderGraph::Texture>(std::move(blurImageView), std::move(blurSampler)));
  }

  auto horizontalBlur = std::make_shared<BlurGraphicSeparate>(true, shadowTextures, blurTextures, commandBuffer,
                                                              _engineState);
  auto verticalBlur = std::make_shared<BlurGraphicSeparate>(false, horizontalBlur->getTextureDst(), shadowTextures,
                                                            commandBuffer, _engineState);
  _directionalShadowBlurPasses[shadow] = {{std::move(horizontalBlur), std::move(verticalBlur)}};

  return shadow;
}

std::shared_ptr<PointLight> Core::createPointLight() { return _gameState->getLightManager()->createPointLight(); }

std::shared_ptr<DirectionalLight> Core::createDirectionalLight() {
  return _gameState->getLightManager()->createDirectionalLight();
}

std::shared_ptr<AmbientLight> Core::createAmbientLight() { return _gameState->getLightManager()->createAmbientLight(); }

const RenderGraph::CommandBuffer* Core::getCommandBufferApplication() {
  const int frame = _engineState->getFrameInFlight();
  auto& commandBuffer = _applicationCommandBuffers.at(frame);
  if (!commandBuffer->getActive()) commandBuffer->beginCommands();
  return commandBuffer.get();
}

std::shared_ptr<ResourceManager> Core::getResourceManager() { return _gameState->getResourceManager(); }

const std::vector<std::shared_ptr<Drawable>>& Core::getDrawables(AlphaType type) { return _drawables[type]; }

const std::vector<std::shared_ptr<PointLight>>& Core::getPointLights() {
  return _gameState->getLightManager()->getPointLights();
}

const std::vector<std::shared_ptr<DirectionalLight>>& Core::getDirectionalLights() {
  return _gameState->getLightManager()->getDirectionalLights();
}

const std::vector<std::shared_ptr<PointShadowResources>>& Core::getPointShadows() {
  return _gameState->getLightManager()->getPointShadows();
}

const std::vector<std::shared_ptr<DirectionalShadowResources>>& Core::getDirectionalShadows() {
  return _gameState->getLightManager()->getDirectionalShadows();
}

std::shared_ptr<Postprocessing> Core::getPostprocessing() { return _postprocessing; }

std::shared_ptr<GUI> Core::getGUI() { return _gui; }

std::shared_ptr<EngineState> Core::getEngineState() { return _engineState; }

std::shared_ptr<GameState> Core::getGameState() { return _gameState; }

std::shared_ptr<Camera> Core::getCamera() { return _gameState->getCameraManager()->getCurrentCamera(); }

std::tuple<int, int> Core::getFPS() { return {_timerFPSLimited->getFPS(), _timerFPSReal->getFPS()}; }
