#pragma once
#include "Window.h"
#include "Instance.h"
#include "Surface.h"
#include "Device.h"
#include "Settings.h"
#include "Input.h"
#include "Pool.h"
#include "Filesystem.h"

class State {
 private:
  std::shared_ptr<Settings> _settings;
  std::shared_ptr<Window> _window;
  std::shared_ptr<Input> _input;
  std::shared_ptr<Instance> _instance;
  std::shared_ptr<Surface> _surface;
  std::shared_ptr<Device> _device;
  std::shared_ptr<DescriptorPool> _descriptorPool;
  std::shared_ptr<Filesystem> _filesystem;
  int _frameInFlight = 0;
#ifdef __ANDROID__
  AAssetManager* _assetManager;
  ANativeWindow* _nativeWindow;
#endif
 public:
  State(std::shared_ptr<Settings> settings);
#ifdef __ANDROID__
  void setNativeWindow(ANativeWindow* window);
  void setAssetManager(AAssetManager* assetManager);
#endif
  void initialize();
  std::shared_ptr<Settings> getSettings();
  std::shared_ptr<Window> getWindow();
  std::shared_ptr<Input> getInput();
  std::shared_ptr<Instance> getInstance();
  std::shared_ptr<Surface> getSurface();
  std::shared_ptr<Device> getDevice();
  std::shared_ptr<DescriptorPool> getDescriptorPool();
  std::shared_ptr<Filesystem> getFilesystem();

  void setFrameInFlight(int frameInFlight);
  int getFrameInFlight();
};