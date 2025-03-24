#include "Utility/RenderGraph.h"

GraphPass::GraphPass(GraphPassStage stage, std::shared_ptr<EngineState> engineState) {
  _stage = stage;
  _engineState = engineState;

  switch (stage) {
    case GraphPassStage::GRAPHIC:
      _commandPool = std::make_shared<CommandPool>(vkb::QueueType::graphics, _engineState->getDevice());
      break;
    case GraphPassStage::COMPUTE:
      _commandPool = std::make_shared<CommandPool>(vkb::QueueType::compute, _engineState->getDevice());
      break;
  };
  _commandBuffers.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _commandBuffers[i] = std::make_shared<CommandBuffer>(_commandPool, _engineState->getDevice());
  }
}

void GraphPass::addColorTarget(std::string name, std::vector<std::shared_ptr<Image>> images) {
  _colorTargets[name] = images;
}

GraphPassStage GraphPass::getStage() { return _stage; }

std::map<std::string, std::vector<std::shared_ptr<Image>>> GraphPass::getColorTargets() { return _colorTargets; }

std::map<std::string, std::shared_ptr<Image>> GraphPass::getDepthTarget() { return _depthTarget; }

std::map<std::string, std::vector<std::shared_ptr<Buffer>>> GraphPass::getStorageInputs() { return _storageInputs; }

std::map<std::string, std::vector<std::shared_ptr<Buffer>>> GraphPass::getStorageOutputs() { return _storageOutputs; }

std::map<std::string, std::vector<std::shared_ptr<Buffer>>> GraphPass::getVertexBufferInputs() {
  return _vertexBufferInputs;
}

std::map<std::string, std::vector<std::shared_ptr<Image>>> GraphPass::getTextureInputs() { return _textureInputs; }

void GraphPass::addRenderExecution(std::function<void(std::shared_ptr<CommandBuffer> commandBuffer)> renderExecution) {
  _renderExecution.push_back(renderExecution);
}

void GraphPass::execute() {
  for (auto& render : _renderExecution) render(_commandBuffers[_engineState->getFrameInFlight()]);
}

void GraphPass::addStorageInput(std::string name, std::vector<std::shared_ptr<Buffer>> buffers) {
  _storageInputs[name] = buffers;
}

void GraphPass::addStorageOutput(std::string name, std::vector<std::shared_ptr<Buffer>> buffers) {
  _storageOutputs[name] = buffers;
}

void GraphPass::addVertexBufferInput(std::string name, std::vector<std::shared_ptr<Buffer>> buffers) {
  _vertexBufferInputs[name] = buffers;
}

void GraphPass::addTextureInput(std::string name, std::vector<std::shared_ptr<Image>> images) {
  _textureInputs[name] = images;
}

void GraphPass::setDepthTarget(std::string name, std::shared_ptr<Image> image) { _depthTarget[name] = image; }

void GraphPass::setEnd(bool end) { _end = end; }

bool GraphPass::getEnd() { return _end; }

void GraphPass::addSignalSemaphore(std::vector<std::shared_ptr<Semaphore>> signalSemaphore) {
  _signalSemaphores.push_back(signalSemaphore);
}

void GraphPass::addWaitSemaphore(std::vector<std::shared_ptr<Semaphore>> waitSemaphore) {
  _waitSemaphores.push_back(waitSemaphore);
}

std::vector<std::vector<std::shared_ptr<Semaphore>>> GraphPass::getSignalSemaphores() { return _signalSemaphores; }

std::vector<std::vector<std::shared_ptr<Semaphore>>> GraphPass::getWaitSemaphores() { return _waitSemaphores; }

std::vector<std::shared_ptr<CommandBuffer>> GraphPass::getCommandBuffers() { return _commandBuffers; }

RenderGraph::RenderGraph(std::shared_ptr<Swapchain> swapchain,
                         std::shared_ptr<BS::thread_pool> threadPool,
                         std::shared_ptr<EngineState> engineState) {
  _swapchain = swapchain;
  _threadPool = threadPool;
  _engineState = engineState;

  // create 2 special semaphores
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _semaphoreRenderFinished.push_back(std::make_shared<Semaphore>(_engineState->getDevice()));
    _semaphoreImageAvailable.push_back(std::make_shared<Semaphore>(_engineState->getDevice()));
    _fenceInFlight.push_back(std::make_shared<Fence>(_engineState->getDevice()));
  }
}

std::vector<std::shared_ptr<Semaphore>> RenderGraph::getSemaphoreRenderFinished() { return _semaphoreRenderFinished; }

std::vector<std::shared_ptr<Semaphore>> RenderGraph::getSemaphoreImageAvailable() { return _semaphoreImageAvailable; }

std::vector<std::shared_ptr<Fence>> RenderGraph::getFenceInFlight() { return _fenceInFlight; }

std::shared_ptr<GraphPass> RenderGraph::getPass(std::string name, GraphPassStage stage) {
  if (_passes.find(name) == _passes.end()) {
    _passes[name] = std::make_shared<GraphPass>(stage, _engineState);
  }

  return _passes[name];
}

void RenderGraph::print() {
  for (auto [key, value] : _passes) {
    std::cout << key << ", stage: " << (int)value->getStage() << std::endl;
    for (auto waitSemaphores : value->getWaitSemaphores()) {
      std::cout << " wait semaphores: ";
      for (auto waitSemaphore : waitSemaphores) {
        std::cout << waitSemaphore->getSemaphore() << " ";
      }
      std::cout << std::endl;
    }
    for (auto signalSemaphores : value->getSignalSemaphores()) {
      std::cout << " signal semaphores: ";
      for (auto signalSemaphore : signalSemaphores) {
        std::cout << signalSemaphore->getSemaphore() << " ";
      }
      std::cout << std::endl;
    }
    for (auto [name, resource] : value->getColorTargets()) {
      std::cout << " color target: " << name << std::endl;
    }
    for (auto [name, resource] : value->getDepthTarget()) {
      std::cout << " depth target: " << name << std::endl;
    }
    for (auto [name, resource] : value->getStorageInputs()) {
      std::cout << " storage input: " << name << std::endl;
    }
    for (auto [name, resource] : value->getStorageOutputs()) {
      std::cout << " storage output: " << name << std::endl;
    }
    for (auto [name, resource] : value->getTextureInputs()) {
      std::cout << " texture input: " << name << std::endl;
    }
    for (auto [name, resource] : value->getVertexBufferInputs()) {
      std::cout << " vertex buffer input: " << name << std::endl;
    }
  }
}

void RenderGraph::calculate() {
  auto getDependencies = [&](std::string name) -> std::vector<std::string> {
    std::vector<std::string> dependencies;
    auto value = _passes[name];
    for (auto [name, resource] : value->getStorageInputs()) {
      dependencies.push_back(name);
    }
    for (auto [name, resource] : value->getStorageOutputs()) {
      dependencies.push_back(name);
    }
    for (auto [name, resource] : value->getTextureInputs()) {
      dependencies.push_back(name);
    }
    for (auto [name, resource] : value->getVertexBufferInputs()) {
      dependencies.push_back(name);
    }

    return dependencies;
  };

  auto findColorTarget = [](std::map<std::string, std::shared_ptr<GraphPass>> passes,
                            std::string findName) -> std::string {
    for (auto [key, value] : passes) {
      for (auto [name, resource] : value->getColorTargets()) {
        if (name == findName) return key;
      }
      for (auto [name, resource] : value->getDepthTarget()) {
        if (name == findName) return key;
      }
    }
    return "";
  };

  std::pair<std::string, std::shared_ptr<GraphPass>> root{"", nullptr};
  for (auto [key, value] : _passes) {
    if (value->getEnd()) root = {key, value};
  }
  std::map<std::shared_ptr<GraphPass>, std::vector<std::shared_ptr<GraphPass>>> passes;
  auto passesBackup = _passes;
  // we should for every root pass run traversal process
  std::function<void(std::string name, std::shared_ptr<GraphPass>)> traverse = [&](std::string name,
                                                                                   std::shared_ptr<GraphPass> node) {
    passesBackup.erase(name);
    _passesOrdered.push_front(node);
    auto dependencies = getDependencies(name);
    for (auto dependency : dependencies) {
      // we want to find who writes to this texture, so we are looking for color target or depth target
      auto targetName = findColorTarget(passesBackup, dependency);
      if (targetName.empty() == false) {
        traverse(targetName, _passes[targetName]);
      }
    }
  };

  if (root.second) {
    traverse(root.first, root.second);
  }

  // set semaphores between passes
  bool flagWaitForSwapchain = true;
  bool queueTypeChange = false;
  GraphPassStage passStagePrevious = _passesOrdered[0]->getStage();
  for (int i = 0; i < _passesOrdered.size(); i++) {
    auto node = _passesOrdered[i];
    if (node->getStage() != passStagePrevious) queueTypeChange = true;
    // signal semaphore for the previous pass
    // wait semaphore for the current pass
    if (queueTypeChange) {
      std::vector<std::shared_ptr<Semaphore>> semaphoreQueueType;
      for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
        semaphoreQueueType.push_back(std::make_shared<Semaphore>(_engineState->getDevice()));
      }
      node->addWaitSemaphore(semaphoreQueueType);
      _passesOrdered[i - 1]->addSignalSemaphore(semaphoreQueueType);
      queueTypeChange = false;
    }
    // special case if we read from swapchain
    // who first interact with swapchain that should wait for the semaphore
    if (flagWaitForSwapchain) {
      for (auto [name, target] : node->getColorTargets()) {
        auto swapchainImageViews = _swapchain->getImageViews();
        for (auto swapchainImageView : swapchainImageViews) {
          if (std::find(target.begin(), target.end(), swapchainImageView->getImage()) != target.end()) {
            node->addWaitSemaphore(_semaphoreImageAvailable);
            flagWaitForSwapchain = false;
            break;
          }
        }
        if (flagWaitForSwapchain == false) break;
      }
    }
    // end node should signal end semaphore
    if (node->getEnd()) {
      if (node->getSignalSemaphores().size() == 0) {
        node->addSignalSemaphore(_semaphoreRenderFinished);
      }
    }

    passStagePrevious = node->getStage();
  }
}

void RenderGraph::render() {
  auto frameInFlight = _engineState->getFrameInFlight();
  std::vector<std::pair<std::shared_ptr<GraphPass>, std::future<void>>> renderFutures;
  for (auto& pass : _passesOrdered) {
    renderFutures.push_back({pass, _threadPool->submit(std::bind(&GraphPass::execute, pass))});
  }

  std::vector<VkCommandBuffer> commandBufferSubmit;
  std::vector<VkSemaphore> signalSemaphores;
  std::vector<VkSemaphore> waitSemaphores;
  std::vector<VkPipelineStageFlags> waitStages;
  GraphPassStage previousStage = renderFutures[0].first->getStage();
  for (int i = 0; i < renderFutures.size(); i++) {
    std::shared_ptr<GraphPass> graphPass = renderFutures[i].first;
    std::future<void> renderFuture = std::move(renderFutures[i].second);
    if (renderFuture.valid()) renderFuture.get();
    // stage change or last iteration
    if (previousStage != graphPass->getStage()) {
      // submit
      VkSubmitInfo submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                              .waitSemaphoreCount = (uint32_t)waitSemaphores.size(),
                              .pWaitSemaphores = waitSemaphores.data(),
                              .pWaitDstStageMask = waitStages.data(),
                              .commandBufferCount = (uint32_t)commandBufferSubmit.size(),
                              .pCommandBuffers = commandBufferSubmit.data(),
                              .signalSemaphoreCount = (uint32_t)signalSemaphores.size(),
                              .pSignalSemaphores = signalSemaphores.data()};
      if (previousStage == GraphPassStage::COMPUTE)
        vkQueueSubmit(_engineState->getDevice()->getQueue(vkb::QueueType::compute), 1, &submitInfo, VK_NULL_HANDLE);
      else
        vkQueueSubmit(_engineState->getDevice()->getQueue(vkb::QueueType::graphics), 1, &submitInfo, VK_NULL_HANDLE);
      //
      commandBufferSubmit.clear();
      signalSemaphores.clear();
      waitSemaphores.clear();
      waitStages.clear();
    }

    commandBufferSubmit.push_back(graphPass->getCommandBuffers()[frameInFlight]->getCommandBuffer());
    for (auto& semaphores : graphPass->getSignalSemaphores())
      signalSemaphores.push_back(semaphores[frameInFlight]->getSemaphore());

    for (auto& semaphores : graphPass->getWaitSemaphores()) {
      switch (graphPass->getStage()) {
        case GraphPassStage::COMPUTE:
          waitStages.push_back(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
          break;
        case GraphPassStage::GRAPHIC:
          waitStages.push_back(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
          break;
        case GraphPassStage::TRANSFER:
          waitStages.push_back(VK_PIPELINE_STAGE_TRANSFER_BIT);
          break;
      };
      waitSemaphores.push_back(semaphores[frameInFlight]->getSemaphore());
    }

    previousStage = graphPass->getStage();
  }
  // submit remaining commands
  VkSubmitInfo submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                          .waitSemaphoreCount = (uint32_t)waitSemaphores.size(),
                          .pWaitSemaphores = waitSemaphores.data(),
                          .pWaitDstStageMask = waitStages.data(),
                          .commandBufferCount = (uint32_t)commandBufferSubmit.size(),
                          .pCommandBuffers = commandBufferSubmit.data(),
                          .signalSemaphoreCount = (uint32_t)signalSemaphores.size(),
                          .pSignalSemaphores = signalSemaphores.data()};
  if (previousStage == GraphPassStage::COMPUTE)
    vkQueueSubmit(_engineState->getDevice()->getQueue(vkb::QueueType::compute), 1, &submitInfo,
                  _fenceInFlight[frameInFlight]->getFence());
  else
    vkQueueSubmit(_engineState->getDevice()->getQueue(vkb::QueueType::graphics), 1, &submitInfo,
                  _fenceInFlight[frameInFlight]->getFence());
}