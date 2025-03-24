#pragma once
#include <string>
#include <map>
#include <vector>
#include <memory>
#include "Vulkan/Image.h"
#include "Vulkan/Swapchain.h"
#include "BS_thread_pool.hpp"
#include "Vulkan/Sync.h"

enum class GraphPassStage { GRAPHIC = 0, COMPUTE = 1, TRANSFER = 2 };

class GraphPass {
 private:
  GraphPassStage _stage;
  std::shared_ptr<EngineState> _engineState;
  // TODO: can contain either frameInFlight or number in swapchain targets, need to handle appropriately
  std::map<std::string, std::vector<std::shared_ptr<Image>>> _colorTargets;
  std::map<std::string, std::shared_ptr<Image>> _depthTarget;
  std::map<std::string, std::vector<std::shared_ptr<Buffer>>> _storageInputs;
  std::map<std::string, std::vector<std::shared_ptr<Buffer>>> _storageOutputs;
  std::map<std::string, std::vector<std::shared_ptr<Buffer>>> _vertexBufferInputs;
  std::map<std::string, std::vector<std::shared_ptr<Image>>> _textureInputs;
  std::shared_ptr<CommandPool> _commandPool;
  std::vector<std::shared_ptr<CommandBuffer>> _commandBuffers;
  std::vector<std::vector<std::shared_ptr<Semaphore>>> _signalSemaphores, _waitSemaphores;
  std::vector<std::function<void(std::shared_ptr<CommandBuffer> commandBuffer)>> _renderExecution;
  bool _end = false;

 public:
  GraphPass(GraphPassStage stage, std::shared_ptr<EngineState> engineState);
  // handle attachments
  void addColorTarget(std::string name, std::vector<std::shared_ptr<Image>> images);
  void setDepthTarget(std::string name, std::shared_ptr<Image> image);
  // handle input to shaders
  void addTextureInput(std::string name, std::vector<std::shared_ptr<Image>> images);
  void addUniformInput();
  void addStorageInput(std::string name, std::vector<std::shared_ptr<Buffer>> buffers);
  // handle output from shaders
  void addStorageOutput(std::string name, std::vector<std::shared_ptr<Buffer>> buffers);
  // handle vertices
  void addVertexBufferInput(std::string name, std::vector<std::shared_ptr<Buffer>> buffers);
  void addIndexBufferInput();
  // mark the last vertex in render graph
  void setEnd(bool end);

  void addSignalSemaphore(std::vector<std::shared_ptr<Semaphore>> signalSemaphore);
  void addWaitSemaphore(std::vector<std::shared_ptr<Semaphore>> waitSemaphore);
  std::vector<std::vector<std::shared_ptr<Semaphore>>> getSignalSemaphores();
  std::vector<std::vector<std::shared_ptr<Semaphore>>> getWaitSemaphores();
  std::vector<std::shared_ptr<CommandBuffer>> getCommandBuffers();

  GraphPassStage getStage();
  std::map<std::string, std::vector<std::shared_ptr<Image>>> getColorTargets();
  std::map<std::string, std::shared_ptr<Image>> getDepthTarget();
  std::map<std::string, std::vector<std::shared_ptr<Buffer>>> getStorageInputs();
  std::map<std::string, std::vector<std::shared_ptr<Buffer>>> getStorageOutputs();
  std::map<std::string, std::vector<std::shared_ptr<Buffer>>> getVertexBufferInputs();
  std::map<std::string, std::vector<std::shared_ptr<Image>>> getTextureInputs();
  bool getEnd();
  // set function that does render pass work
  void addRenderExecution(std::function<void(std::shared_ptr<CommandBuffer> commandBuffer)>);
  void execute();
};

class RenderGraph {
 private:
  std::map<std::string, std::shared_ptr<GraphPass>> _passes;
  std::deque<std::shared_ptr<GraphPass>> _passesOrdered;
  std ::shared_ptr<Swapchain> _swapchain;
  std::shared_ptr<EngineState> _engineState;
  std::shared_ptr<BS::thread_pool> _threadPool;
  // special semaphores
  std::vector<std::shared_ptr<Semaphore>> _semaphoreRenderFinished, _semaphoreImageAvailable;
  std::vector<std::shared_ptr<Fence>> _fenceInFlight;

 public:
  RenderGraph(std::shared_ptr<Swapchain> swapchain,
              std::shared_ptr<BS::thread_pool> threadPool,
              std::shared_ptr<EngineState> engineState);
  std::shared_ptr<GraphPass> getPass(std::string name, GraphPassStage stage);
  std::vector<std::shared_ptr<Semaphore>> getSemaphoreRenderFinished();
  std::vector<std::shared_ptr<Semaphore>> getSemaphoreImageAvailable();
  std::vector<std::shared_ptr<Fence>> getFenceInFlight();
  void calculate();
  void print();
  void render();
};