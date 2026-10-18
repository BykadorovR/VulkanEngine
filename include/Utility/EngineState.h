#pragma once
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <span>
#include <vector>
#include "Utility/Filesystem.h"
#include "Utility/Settings.h"
#include "Utility/Input.h"
#include "Utility/Logger.h"

import Allocator;
import DescriptorBuffer;
import Device;
import Instance;
import ResourceUploader;
import Surface;
import Window;

class EngineState {
 private:
  std::shared_ptr<Settings> _settings;
  std::shared_ptr<RenderGraph::Instance> _instance;
  std::shared_ptr<RenderGraph::Window> _window;
  std::shared_ptr<RenderGraph::Surface> _surface;
  std::shared_ptr<Input> _input;
  std::shared_ptr<RenderGraph::Device> _device;
  std::shared_ptr<RenderGraph::DescriptorPool> _descriptorPool;
  std::shared_ptr<Filesystem> _filesystem;
  std::shared_ptr<RenderGraph::MemoryAllocator> _memoryAllocator;
  std::shared_ptr<RenderGraph::ResourceUploader> _resourceUploader;
  std::vector<RenderGraph::UploadTicket> _uploadTickets;
  std::shared_ptr<Logger> _logger;
  std::shared_ptr<DebugUtils> _debugUtils;
  int _frameInFlight = 0;
#ifdef __ANDROID__
  AAssetManager* _assetManager;
  ANativeWindow* _nativeWindow;
#endif
 public:
  EngineState(std::shared_ptr<Settings> settings);
#ifdef __ANDROID__
  void setNativeWindow(ANativeWindow* window);
  void setAssetManager(AAssetManager* assetManager);
  AAssetManager* getAssetManager();
#endif
  void initialize();
  std::shared_ptr<Settings> getSettings();
  std::shared_ptr<RenderGraph::Window> getWindow();
  std::shared_ptr<Input> getInput();
  std::shared_ptr<RenderGraph::MemoryAllocator> getMemoryAllocator();
  std::shared_ptr<RenderGraph::Instance> getInstance();
  std::shared_ptr<RenderGraph::Surface> getSurface();
  std::shared_ptr<RenderGraph::Device> getDevice();
  std::shared_ptr<RenderGraph::DescriptorPool> getDescriptorPool();
  std::shared_ptr<Filesystem> getFilesystem();
  std::shared_ptr<RenderGraph::ResourceUploader> getResourceUploader();
  std::vector<RenderGraph::UploadTicket>& getUploadTickets();
  std::shared_ptr<Logger> getLogger();
  std::shared_ptr<DebugUtils> getDebugUtils();
  void setFrameInFlight(int frameInFlight);
  int getFrameInFlight();
};
