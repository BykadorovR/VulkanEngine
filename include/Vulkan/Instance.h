#pragma once
#ifdef __ANDROID__
#include "VulkanWrapper.h"
#else
#include <vulkan/vulkan.h>
#endif
#include <string>
#include <vector>
#include <exception>
#include <stdexcept>
#include <memory>
#include <iostream>

class Instance {
 private:
  VkInstance _instance;
  VkDebugUtilsMessengerEXT _debugMessenger;
  // validation
  bool _validation = false;
  const std::vector<const char*> _validationLayers = {"VK_LAYER_KHRONOS_validation"};
  std::vector<const char*> _extensionsInstance = {"VK_KHR_surface"};
  bool _checkValidationLayersSupport();

 public:
  Instance(std::string name, bool validation);
  const VkInstance& getInstance();
  const std::vector<const char*>& getValidationLayers();
  bool isValidation();
  ~Instance();
};