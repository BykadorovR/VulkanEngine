#include "Utility/RenderGraph.h"

void GraphStorage::add(std::string name, std::shared_ptr<ImageHolder> imageHolder) {
  _imageHolders[name] = imageHolder;
}

void GraphStorage::add(std::string name, std::shared_ptr<Image> image) { _images[name] = image; }

void GraphStorage::add(std::string name, std::vector<std::shared_ptr<Buffer>> buffers) { _buffers[name] = buffers; }

std::shared_ptr<ImageHolder> GraphStorage::getImageHolder(std::string name) { return _imageHolders[name]; }

std::shared_ptr<Image> GraphStorage::getImage(std::string name) { return _images[name]; }

std::vector<std::shared_ptr<Buffer>> GraphStorage::getBuffer(std::string name) { return _buffers[name]; }

GraphPass::GraphPass(std::string name,
                     GraphPassStage stage,
                     std::shared_ptr<GraphStorage> graphStorage,
                     std::shared_ptr<EngineState> engineState) {
  _name = name;
  _stage = stage;
  _graphStorage = graphStorage;
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

GraphPassStage GraphPass::getStage() { return _stage; }

std::vector<std::string> GraphPass::getColorTargets() { return _colorTargets; }

std::optional<std::string> GraphPass::getDepthTarget() { return _depthTarget; }

std::vector<std::string> GraphPass::getStorageInputs() { return _storageInputs; }

std::vector<std::string> GraphPass::getStorageOutputs() { return _storageOutputs; }

std::vector<std::string> GraphPass::getVertexBufferInputs() { return _vertexBufferInputs; }

std::vector<std::string> GraphPass::getTextureInputs() { return _textureInputs; }

void GraphPass::addRenderExecution(
    std::function<void(std::vector<std::shared_ptr<Framebuffer>>, std::shared_ptr<CommandBuffer>)> renderExecution) {
  _renderExecutions.push_back(renderExecution);
}

void GraphPass::addComputeExecution(std::function<void(std::shared_ptr<CommandBuffer>)> computeExecution) {
  _computeExecutions.push_back(computeExecution);
}

void GraphPass::execute() {
  for (auto& render : _renderExecutions) {
    auto commandBuffer = _commandBuffers[_engineState->getFrameInFlight()];
    if (commandBuffer->getActive() == false) commandBuffer->beginCommands();

    std::vector<int> indices;
    for (auto& key : getColorTargets()) {
      indices.push_back(_graphStorage->getImageHolder(key)->getIndex());
    }

    render(_frameBuffers[indices], commandBuffer);
  }
  for (auto& compute : _computeExecutions) {
    auto commandBuffer = _commandBuffers[_engineState->getFrameInFlight()];
    if (commandBuffer->getActive() == false) commandBuffer->beginCommands();

    compute(commandBuffer);
  }
}

void GraphPass::addColorTarget(std::string name) { _colorTargets.push_back(name); }

void GraphPass::addStorageInput(std::string name) { _storageInputs.push_back(name); }

void GraphPass::addStorageOutput(std::string name) { _storageOutputs.push_back(name); }

void GraphPass::addVertexBufferInput(std::string name) { _vertexBufferInputs.push_back(name); }

void GraphPass::addTextureInput(std::string name) { _textureInputs.push_back(name); }

void GraphPass::setDepthTarget(std::string name) { _depthTarget = name; }

void GraphPass::setEnd(bool end) { _end = end; }

bool GraphPass::getEnd() { return _end; }

std::string GraphPass::getName() { return _name; }

void GraphPass::addSignalSemaphore(std::vector<std::shared_ptr<Semaphore>> signalSemaphore) {
  _signalSemaphores.push_back(signalSemaphore);
}

void GraphPass::addWaitSemaphore(std::vector<std::shared_ptr<Semaphore>> waitSemaphore) {
  _waitSemaphores.push_back(waitSemaphore);
}

std::vector<std::vector<std::shared_ptr<Semaphore>>> GraphPass::getSignalSemaphores() { return _signalSemaphores; }

std::vector<std::vector<std::shared_ptr<Semaphore>>> GraphPass::getWaitSemaphores() { return _waitSemaphores; }

void GraphPass::setCommandBuffers(std::vector<std::shared_ptr<CommandBuffer>> commandBuffers) {
  _commandBuffers = commandBuffers;
}

void GraphPass::setFrameBuffers(std::map<std::vector<int>, std::vector<std::shared_ptr<Framebuffer>>> frameBuffers) {
  _frameBuffers = frameBuffers;
}

std::map<std::vector<int>, std::vector<std::shared_ptr<Framebuffer>>> GraphPass::getFrameBuffers() {
  return _frameBuffers;
}

void GraphPass::setRenderPass(std::shared_ptr<RenderPass> renderPass) { _renderPass = renderPass; }

void GraphPass::setRenderPassScenario(RenderPassScenario renderPassScenario) {
  _renderPassScenario = renderPassScenario;
}

RenderPassScenario GraphPass::getRenderPassScenario() { return _renderPassScenario; }

std::shared_ptr<RenderPass> GraphPass::getRenderPass() { return _renderPass; }

std::vector<std::shared_ptr<CommandBuffer>> GraphPass::getCommandBuffers() { return _commandBuffers; }

RenderGraph::RenderGraph(std::shared_ptr<Swapchain> swapchain,
                         std::shared_ptr<BS::thread_pool> threadPool,
                         std::shared_ptr<EngineState> engineState) {
  _swapchain = swapchain;
  _engineState = engineState;
  _threadPool = threadPool;
  _graphStorage = std::make_shared<GraphStorage>();

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

std::shared_ptr<GraphStorage> RenderGraph::getGraphStorage() { return _graphStorage; }

std::shared_ptr<GraphPass> RenderGraph::getPass(std::string name, GraphPassStage stage) {
  auto passIt = std::find_if(_passes.begin(), _passes.end(), [name = name](std::shared_ptr<GraphPass> graphPass) {
    return graphPass->getName() == name;
  });
  if (passIt == _passes.end()) {
    auto pass = std::make_shared<GraphPass>(name, stage, _graphStorage, _engineState);
    _passes.push_back(pass);
    return pass;
  }

  return *passIt;
}

std::shared_ptr<GraphPass> RenderGraph::getPassApplication() {
  if (_passApplication == nullptr) {
    _passApplication = std::make_shared<GraphPass>("Application", GraphPassStage::GRAPHIC, _graphStorage, _engineState);
    std::vector<std::shared_ptr<Semaphore>> semaphoreApplicationReady;
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
      semaphoreApplicationReady.push_back(std::make_shared<Semaphore>(_engineState->getDevice()));
    }
    _passApplication->addSignalSemaphore(semaphoreApplicationReady);
  }
  return _passApplication;
}

void RenderGraph::print() {
  if (_passesOrdered.size() == 0) return;
  for (auto value : _passesOrdered) {
    std::cout << "Name: " << value->getName() << ", Stage : " << (int)value->getStage() << std::endl;
    if (value->getStage() == GraphPassStage::GRAPHIC) {
      auto renderPass = value->getRenderPass();
      // for application stage is empty
      if (renderPass) {
        std::cout << "Render pass: " << renderPass->getRenderPass()
                  << ", color: " << renderPass->getColorAttachmentNumber()
                  << ", depth: " << renderPass->getDepthAttachmentNumber() << std::endl;
      }
      if (value->getFrameBuffers().empty() == false) {
        std::cout << "Framebuffers:" << std::endl;
        for (auto [indices, framebuffers] : value->getFrameBuffers()) {
          std::cout << "[";
          for (auto i = indices.begin(); i != indices.end(); i++) {
            std::cout << *i;
            if (i != indices.end() - 1) std::cout << " ,";
          }
          std::cout << "] ";
          for (auto framebuffer : framebuffers) {
            std::cout << framebuffer->getBuffer() << " ";
          }
          std::cout << std::endl;
        }
      }
    }
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
    for (auto& name : value->getColorTargets()) {
      std::cout << " color target: " << name << "; ";
      for (auto image : _graphStorage->getImageHolder(name)->getImages()) {
        std::cout << image->getImage() << " ";
      }
      std::cout << std::endl;
    }
    {
      auto name = value->getDepthTarget();
      if (name)
        std::cout << " depth target: " << name.value() << "; " << _graphStorage->getImage(name.value())->getImage()
                  << std::endl;
    }
    for (auto& name : value->getStorageInputs()) {
      std::cout << " storage input: " << name << "; ";
      for (auto resource : _graphStorage->getBuffer(name)) {
        std::cout << resource->getData() << " ";
      }
      std::cout << std::endl;
    }
    for (auto name : value->getStorageOutputs()) {
      std::cout << " storage output: " << name << "; ";
      for (auto resource : _graphStorage->getBuffer(name)) {
        std::cout << resource->getData() << " ";
      }
      std::cout << std::endl;
    }
    for (auto name : value->getTextureInputs()) {
      std::cout << " texture input: " << name << "; ";
      for (auto image : _graphStorage->getImageHolder(name)->getImages()) {
        std::cout << image->getImage() << " ";
      }
      std::cout << std::endl;
    }
    for (auto name : value->getVertexBufferInputs()) {
      std::cout << " vertex buffer input: " << name << "; ";
      for (auto resource : _graphStorage->getBuffer(name)) {
        std::cout << resource->getData() << " ";
      }
      std::cout << std::endl;
    }
  }
}

void RenderGraph::calculate() {
  auto getInputs = [&](std::string name) -> std::vector<std::string> {
    std::vector<std::string> dependencies;
    auto value = *std::find_if(_passes.rbegin(), _passes.rend(), [name = name](std::shared_ptr<GraphPass> graphPass) {
      return graphPass->getName() == name;
    });
    for (auto name : value->getStorageInputs()) {
      dependencies.push_back(name);
    }
    for (auto name : value->getTextureInputs()) {
      dependencies.push_back(name);
    }
    for (auto name : value->getVertexBufferInputs()) {
      dependencies.push_back(name);
    }
    return dependencies;
  };

  auto findTarget = [](std::vector<std::shared_ptr<GraphPass>> passes,
                       std::string findName) -> std::shared_ptr<GraphPass> {
    for (auto it = passes.rbegin(); it != passes.rend(); ++it) {
      auto value = *it;
      for (auto name : value->getColorTargets()) {
        if (name == findName) return value;
      }
      {
        auto name = value->getDepthTarget();
        if (name == findName) return value;
      }
      for (auto name : value->getStorageOutputs()) {
        if (name == findName) return value;
      }
    }
    return nullptr;
  };

  std::shared_ptr<GraphPass> root{nullptr};
  for (auto& value : _passes) {
    if (value->getEnd()) root = {value};
  }
  auto passesBackup = _passes;
  // we should for every root pass run traversal process
  std::function<void(std::shared_ptr<GraphPass>)> traverse = [&](std::shared_ptr<GraphPass> node) {
    if (passesBackup.empty()) return;

    passesBackup.erase(std::remove(passesBackup.begin(), passesBackup.end(), node), passesBackup.end());
    _passesOrdered.push_front(node);

    auto inputs = getInputs(node->getName());
    std::vector<std::shared_ptr<GraphPass>> passNext;
    for (auto input : inputs) {
      auto targetPass = findTarget(passesBackup, input);
      if (targetPass) passNext.push_back(targetPass);
    }
    // this means that stage depends only on it's frame buffer attachment
    if (inputs.empty()) {
      for (auto input : node->getColorTargets()) {
        auto targetPass = findTarget(passesBackup, input);
        if (targetPass) passNext.push_back(targetPass);
      }
    }

    // need to find the pass with the matching name and minimum targets number
    std::sort(passNext.begin(), passNext.end(), [](std::shared_ptr<GraphPass> a, std::shared_ptr<GraphPass> b) {
      int numberA = a->getColorTargets().size();
      if (a->getDepthTarget()) ++numberA;
      int numberB = b->getColorTargets().size();
      if (b->getDepthTarget()) ++numberB;
      return numberA < numberB;
    });

    for (auto& pass : passNext) {
      traverse(pass);
    }
  };

  if (root) {
    traverse(root);
  }

  // set semaphores between passes
  bool flagWaitForSwapchain = true;
  bool queueTypeChange = false;
  std::optional<GraphPassStage> passStagePrevious;
  for (int i = 0; i < _passesOrdered.size(); i++) {
    auto pass = _passesOrdered[i];
    if (passStagePrevious.has_value() && pass->getStage() != passStagePrevious) queueTypeChange = true;
    // signal semaphore for the previous pass
    // wait semaphore for the current pass
    if (queueTypeChange) {
      std::vector<std::shared_ptr<Semaphore>> semaphoreQueueType;
      for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
        semaphoreQueueType.push_back(std::make_shared<Semaphore>(_engineState->getDevice()));
      }
      pass->addWaitSemaphore(semaphoreQueueType);
      _passesOrdered[i - 1]->addSignalSemaphore(semaphoreQueueType);
      queueTypeChange = false;
    }
    // special case if we read from swapchain
    // who first interact with swapchain that should wait for the semaphore
    if (flagWaitForSwapchain) {
      for (auto name : pass->getColorTargets()) {
        if (std::dynamic_pointer_cast<ImageHolderSwapchain>(_graphStorage->getImageHolder(name))) {
          pass->addWaitSemaphore(_semaphoreImageAvailable);
          flagWaitForSwapchain = false;
          break;
        }
      }
    }
    // end node should signal end semaphore
    if (pass->getEnd()) {
      if (pass->getSignalSemaphores().size() == 0) {
        pass->addSignalSemaphore(_semaphoreRenderFinished);
      }
    }

    passStagePrevious = pass->getStage();

    // calculate render pass and framebuffer
    if (pass->getStage() == GraphPassStage::GRAPHIC) {
      std::shared_ptr<RenderPass> renderPass = _engineState->getRenderPassManager()->getRenderPass(
          pass->getRenderPassScenario());
      if (renderPass == nullptr) {
        std::vector<VkAttachmentDescription> colorDescriptions;
        std::vector<VkAttachmentReference> colorReferences;
        uint32_t index = 0;
        for (auto& key : pass->getColorTargets()) {
          VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
          if (i > 0) {
            auto passPrevious = _passesOrdered[i - 1];
            for (auto keyPrevious : passPrevious->getColorTargets()) {
              if (key == keyPrevious) {
                loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                break;
              }
            }
          }
          auto target = _graphStorage->getImageHolder(key);
          VkImageLayout finalLayout = target->getImage()->getImageLayout();
          if (pass->getEnd()) {
            finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
          }
          VkAttachmentDescription colorDescription{.format = target->getImage()->getFormat(),
                                                   .samples = VK_SAMPLE_COUNT_1_BIT,
                                                   .loadOp = loadOp,
                                                   .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                                   .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                                   .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                                   .initialLayout = target->getImage()->getImageLayout(),
                                                   .finalLayout = finalLayout};
          colorDescriptions.push_back(colorDescription);
          colorReferences.push_back(
              VkAttachmentReference{.attachment = index, .layout = target->getImage()->getImageLayout()});
          index++;
        }
        bool depth = false;
        {
          auto key = pass->getDepthTarget();
          if (key) {
            auto target = _graphStorage->getImage(key.value());
            VkAttachmentDescription depthDescription{.format = target->getFormat(),
                                                     .samples = VK_SAMPLE_COUNT_1_BIT,
                                                     .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                     .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                                     .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                                     .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                                     // comes from light baking to depth image
                                                     .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                                     // goes to GUI for visualization
                                                     .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
            colorDescriptions.push_back(depthDescription);
            depth = true;
          }
        }

        renderPass = std::make_shared<RenderPass>(_engineState->getDevice());
        if (depth) {
          VkAttachmentReference depthReference{.attachment = (uint32_t)colorReferences.size(),
                                               // we want read depth in shader
                                               .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
          renderPass->initializeCustom(colorDescriptions, colorReferences, depthReference);
        } else {
          renderPass->initializeCustom(colorDescriptions, colorReferences, std::nullopt);
        }

        _engineState->getRenderPassManager()->setRenderPass(pass->getRenderPassScenario(), renderPass);
      }
      pass->setRenderPass(renderPass);

      _calculateFrameBuffers(pass);
    }
  }
}

void RenderGraph::_calculateFrameBuffers(std::shared_ptr<GraphPass> pass) {
  auto depth = pass->getDepthTarget();
  // fill framebuffers
  std::vector<int> attachments;
  for (auto& key : pass->getColorTargets()) {
    auto target = _graphStorage->getImageHolder(key);
    attachments.push_back(target->getImages().size());
  }

  std::map<std::vector<int>, std::vector<std::shared_ptr<Framebuffer>>> frameBuffers;
  std::vector<int> indices(attachments.size(), 0);
  while (true) {
    std::vector<std::shared_ptr<Image>> images(attachments.size());
    int layersMax = 1;
    for (int i = 0; i < attachments.size(); i++) {
      auto target = _graphStorage->getImageHolder(pass->getColorTargets()[i]);
      images[i] = target->getImages()[indices[i]];
      layersMax = std::max(layersMax, images[i]->getLayersNumber());
    }

    if (depth) images.push_back(_graphStorage->getImage(depth.value()));

    std::vector<std::vector<std::shared_ptr<ImageView>>> imageViews(
        layersMax, std::vector<std::shared_ptr<ImageView>>(images.size()));
    for (int l = 0; l < layersMax; l++) {
      for (int i = 0; i < images.size(); i++) {
        auto image = images[i];
        auto aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        if (depth && image == _graphStorage->getImage(depth.value())) aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

        if (image->getLayersNumber() == 1) {
          // baseArrayLayer - which face is used
          // arrayLayerNumber - number of faces
          // baseMipMapLevel - which mip map level is used
          // mipMapLevels - number of visible mip maps for current image view
          if (l > 0)
            imageViews[l][i] = imageViews[l - 1][i];
          else {
            imageViews[l][i] = std::make_shared<ImageView>(image, VK_IMAGE_VIEW_TYPE_2D, 0, 1, 0, 1, aspectMask,
                                                           _engineState);
          }
        } else {
          // always render to mip map level = 0
          imageViews[l][i] = std::make_shared<ImageView>(image, VK_IMAGE_VIEW_TYPE_2D, l, 1, 0, 1, aspectMask,
                                                         _engineState);
        }
      }
    }

    frameBuffers[indices].resize(layersMax);
    for (int l = 0; l < layersMax; l++) {
      auto target = _graphStorage->getImageHolder(pass->getColorTargets()[0]);
      auto imageParameters = target->getImage()->getResolution();
      frameBuffers[indices][l] = std::make_shared<Framebuffer>(imageViews[l], imageParameters, pass->getRenderPass(),
                                                               _engineState->getDevice());
    }
    int counter = static_cast<int>(attachments.size()) - 1;
    while (counter >= 0) {
      indices[counter]++;
      if (indices[counter] < attachments[counter]) {
        break;
      } else {
        indices[counter] = 0;
        counter--;
      }
    }

    if (counter < 0) break;
  }

  pass->setFrameBuffers(frameBuffers);
}

void RenderGraph::render() {
  auto frameInFlight = _engineState->getFrameInFlight();
  // application update should be called in main thread because of ImGUI
  _passApplication->execute();

  std::vector<std::pair<std::shared_ptr<GraphPass>, std::future<void>>> renderFutures;
  for (auto& pass : _passesOrdered) {
    renderFutures.push_back({pass, _threadPool->submit(std::bind(&GraphPass::execute, pass))});
  }

  std::vector<std::shared_ptr<CommandBuffer>> commandBufferSubmit;
  std::vector<VkSemaphore> signalSemaphores;
  std::vector<VkSemaphore> waitSemaphores;
  // first submit application
  {
    for (auto& semaphores : _passApplication->getSignalSemaphores()) {
      signalSemaphores.push_back(semaphores[frameInFlight]->getSemaphore());
      waitSemaphores.push_back(semaphores[frameInFlight]->getSemaphore());
    }
    // need to change the layout from SRC_KHR to GENERAL
    auto swapchainImageViews = _swapchain->getImageViews();
    auto textureInput = _swapchain->getImageViews()[_swapchain->getSwapchainIndex()];
    textureInput->getImage()->changeLayout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_GENERAL,
                                           VK_IMAGE_ASPECT_COLOR_BIT, 1, 1,
                                           _passApplication->getCommandBuffers()[frameInFlight]);
    // need to end command buffers before submit
    _passApplication->getCommandBuffers()[frameInFlight]->endCommands();
    VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &_passApplication->getCommandBuffers()[frameInFlight]->getCommandBuffer(),
        .signalSemaphoreCount = (uint32_t)signalSemaphores.size(),
        .pSignalSemaphores = signalSemaphores.data()};
    vkQueueSubmit(_engineState->getDevice()->getQueue(vkb::QueueType::graphics), 1, &submitInfo, VK_NULL_HANDLE);
    signalSemaphores.clear();
  }
  // process rest of the stages
  std::optional<GraphPassStage> previousStage;
  for (int i = 0; i < renderFutures.size(); i++) {
    std::shared_ptr<GraphPass> graphPass = renderFutures[i].first;
    std::future<void> renderFuture = std::move(renderFutures[i].second);
    if (renderFuture.valid()) renderFuture.get();
    // stage change or last iteration
    if (previousStage.has_value()) {
      if (previousStage != graphPass->getStage()) {
        // need to end command buffers before submit
        std::vector<VkCommandBuffer> commandBufferRawSubmit;
        for (auto& commandBuffer : commandBufferSubmit) {
          commandBuffer->endCommands();
          commandBufferRawSubmit.push_back(commandBuffer->getCommandBuffer());
        }

        std::vector<VkPipelineStageFlags> waitStages(waitSemaphores.size());
        if (previousStage == GraphPassStage::COMPUTE)
          for (auto& waitStage : waitStages) waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        else
          for (auto& waitStage : waitStages) waitStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        // submit + semaphores
        VkSubmitInfo submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                .waitSemaphoreCount = (uint32_t)waitSemaphores.size(),
                                .pWaitSemaphores = waitSemaphores.data(),
                                .pWaitDstStageMask = waitStages.data(),
                                .commandBufferCount = (uint32_t)commandBufferRawSubmit.size(),
                                .pCommandBuffers = commandBufferRawSubmit.data(),
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
      } else {
        // put EXECUTION AND MEMORY barriers if needed (not layout transition ones)
        // IMPORTANT: we should add any barrier to the previous stage because potentially all command buffer are already
        // recorded. So we need to add barrier to the end of the previous command buffer.
        {
          switch (graphPass->getStage()) {
            case GraphPassStage::GRAPHIC: {
              std::set<std::shared_ptr<Image>> images;
              for (auto& key : graphPass->getTextureInputs()) {
                images.insert(_graphStorage->getImageHolder(key)->getImage());
              }

              std::vector<VkImageMemoryBarrier> executionBarriers;
              for (auto& image : images) {
                executionBarriers.push_back(
                    VkImageMemoryBarrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                         .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                         .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                         .oldLayout = image->getImageLayout(),
                                         .newLayout = image->getImageLayout(),
                                         .image = image->getImage(),
                                         .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}});
              }
              vkCmdPipelineBarrier(commandBufferSubmit.back()->getCommandBuffer(),
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                   0, 0, nullptr, 0, nullptr, executionBarriers.size(), executionBarriers.data());
              break;
            }
            case GraphPassStage::COMPUTE: {
              std::vector<VkImageMemoryBarrier> imageBarriers;
              for (auto& key : graphPass->getTextureInputs()) {
                std::shared_ptr<Image> image = _graphStorage->getImageHolder(key)->getImage();
                imageBarriers.push_back(
                    VkImageMemoryBarrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                         .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                         .oldLayout = image->getImageLayout(),
                                         .newLayout = image->getImageLayout(),
                                         .image = image->getImage(),
                                         .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}});
              }
              std::vector<VkBufferMemoryBarrier> bufferBarriers;
              for (auto& key : graphPass->getStorageInputs()) {
                bufferBarriers.push_back(
                    VkBufferMemoryBarrier{.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                          .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                          .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                          .buffer = _graphStorage->getBuffer(key)[frameInFlight]->getData(),
                                          .size = VK_WHOLE_SIZE});
              }
              vkCmdPipelineBarrier(renderFutures[i - 1].first->getCommandBuffers()[frameInFlight]->getCommandBuffer(),
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                                   nullptr, bufferBarriers.size(), bufferBarriers.data(), imageBarriers.size(),
                                   imageBarriers.data());
              break;
            }
          };
        }
      }
    }

    commandBufferSubmit.push_back(graphPass->getCommandBuffers()[frameInFlight]);
    for (auto& semaphores : graphPass->getSignalSemaphores())
      signalSemaphores.push_back(semaphores[frameInFlight]->getSemaphore());

    for (auto& semaphores : graphPass->getWaitSemaphores()) {
      waitSemaphores.push_back(semaphores[frameInFlight]->getSemaphore());
    }

    previousStage = graphPass->getStage();
  }
  // need to end command buffers before submit
  std::vector<VkCommandBuffer> commandBufferRawSubmit;
  for (auto& commandBuffer : commandBufferSubmit) {
    commandBuffer->endCommands();
    commandBufferRawSubmit.push_back(commandBuffer->getCommandBuffer());
  }

  std::vector<VkPipelineStageFlags> waitStages(waitSemaphores.size());
  if (previousStage == GraphPassStage::COMPUTE)
    for (auto& waitStage : waitStages) waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  else
    for (auto& waitStage : waitStages) waitStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  // submit remaining commands
  VkSubmitInfo submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                          .waitSemaphoreCount = (uint32_t)waitSemaphores.size(),
                          .pWaitSemaphores = waitSemaphores.data(),
                          .pWaitDstStageMask = waitStages.data(),
                          .commandBufferCount = (uint32_t)commandBufferRawSubmit.size(),
                          .pCommandBuffers = commandBufferRawSubmit.data(),
                          .signalSemaphoreCount = (uint32_t)signalSemaphores.size(),
                          .pSignalSemaphores = signalSemaphores.data()};
  if (previousStage == GraphPassStage::COMPUTE)
    vkQueueSubmit(_engineState->getDevice()->getQueue(vkb::QueueType::compute), 1, &submitInfo,
                  _fenceInFlight[frameInFlight]->getFence());
  else
    vkQueueSubmit(_engineState->getDevice()->getQueue(vkb::QueueType::graphics), 1, &submitInfo,
                  _fenceInFlight[frameInFlight]->getFence());
}

void RenderGraph::reset(std::shared_ptr<CommandBuffer> commandBuffer) {
  std::vector<std::shared_ptr<Image>> images;
  for (auto& imageView : _swapchain->getImageViews()) images.push_back(imageView->getImage());
  auto swapchainHolder = std::make_shared<ImageHolderSwapchain>(images, _swapchain);
  for (auto& pass : _passesOrdered) {
    bool changed = false;
    // change only images themselfs, no need to change names
    for (auto key : pass->getColorTargets()) {
      if (std::dynamic_pointer_cast<ImageHolderSwapchain>(_graphStorage->getImageHolder(key))) {
        _graphStorage->add(key, swapchainHolder);
        changed = true;
      }
    }

    if (changed && pass->getStage() == GraphPassStage::GRAPHIC) {
      //// recalculate rest of the attachments because the size of images should be the same as framebuffer
      // for (auto key : pass->getTextureInputs()) {
      //   if (std::dynamic_pointer_cast<ImageHolderFlight>(_graphStorage->getImageHolder(key))) {
      //     std::vector<std::shared_ptr<Image>> imagesReset;
      //     for (auto& image : _graphStorage->getImageHolder(key)->getImages()) {
      //       auto imageReset = std::make_shared<Image>(
      //           swapchainHolder->getImage()->getResolution(), image->getLayersNumber(), image->getMipMapLevels(),
      //           image->getFormat(), image->getTiling(), image->getUsage(), _engineState);
      //       imageReset->generateMipmaps(image->getMipMapLevels(), image->getLayersNumber(), commandBuffer);
      //       imagesReset.push_back(imageReset);
      //     }
      //     auto imageHolder = std::make_shared<ImageHolderSwapchain>(imagesReset, _swapchain);
      //     _graphStorage->add(key, imageHolder);
      //   }
      // }

      _calculateFrameBuffers(pass);
    }
  }
}

ImageHolder::ImageHolder(std::vector<std::shared_ptr<Image>> images) { _images = images; }

std::vector<std::shared_ptr<Image>> ImageHolder::getImages() { return _images; }

ImageHolderSwapchain::ImageHolderSwapchain(std::vector<std::shared_ptr<Image>> images,
                                           std::shared_ptr<Swapchain> swapchain)
    : ImageHolder(images) {
  _swapchain = swapchain;
}

int ImageHolderSwapchain::getIndex() { return _swapchain->getSwapchainIndex(); }

std::shared_ptr<Image> ImageHolderSwapchain::getImage() { return _images[_swapchain->getSwapchainIndex()]; }

ImageHolderFlight::ImageHolderFlight(std::vector<std::shared_ptr<Image>> images,
                                     std::shared_ptr<EngineState> engineState)
    : ImageHolder(images) {
  _engineState = engineState;
}

int ImageHolderFlight::getIndex() { return _engineState->getFrameInFlight(); }

std::shared_ptr<Image> ImageHolderFlight::getImage() { return _images[_engineState->getFrameInFlight()]; }