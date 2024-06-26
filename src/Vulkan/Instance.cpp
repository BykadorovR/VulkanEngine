#include "Instance.h"
#ifdef __ANDROID__
#include <android/log.h>
#endif

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                             VkDebugUtilsMessageTypeFlagsEXT messageType,
                                             const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                             void* pUserData) {
#ifdef __ANDROID__
  __android_log_print(ANDROID_LOG_ERROR, "validation layer: ", "%s", pCallbackData->pMessage);
#else
  std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
#endif
  return VK_FALSE;
}

bool Instance::_checkValidationLayersSupport() {
  uint32_t layerCount;
  vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

  std::vector<VkLayerProperties> availableLayers(layerCount);
  vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

  for (auto layerName : _validationLayers) {
    bool layerFound = false;

    for (const auto& layerProperties : availableLayers) {
      if (strcmp(layerName, layerProperties.layerName) == 0) {
        layerFound = true;
        break;
      }
    }

    if (!layerFound) {
      return false;
    }
  }

  return true;
}

Instance::Instance(std::string name, bool validation) {
  _validation = validation;

  if (_validation && _checkValidationLayersSupport() == false) {
    throw std::runtime_error("validation layers requested, but not available!");
  }

  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = name.c_str();
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "No Engine";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_MAKE_API_VERSION(0, 1, 1, 0);

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;

// extensions
#ifdef WIN32
  _extensionsInstance.push_back("VK_KHR_win32_surface");
#elif __ANDROID__
  _extensionsInstance.push_back("VK_KHR_android_surface");
#else
  throw std::runtime_error("Define surface extension for current platform!")
#endif
  if (_validation) _extensionsInstance.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  createInfo.enabledExtensionCount = static_cast<uint32_t>(_extensionsInstance.size());
  createInfo.ppEnabledExtensionNames = _extensionsInstance.data();

  // validation
  createInfo.enabledLayerCount = static_cast<uint32_t>(_validationLayers.size());
  createInfo.ppEnabledLayerNames = _validationLayers.data();
#ifndef __ANDROID__
  if (_validation) {
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugCreateInfo.pfnUserCallback = debugCallback;

    createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
  }
#endif
  if (vkCreateInstance(&createInfo, nullptr, &_instance) != VK_SUCCESS) {
    throw std::runtime_error("failed to create instance!");
  }

  {
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.pNext = nullptr;
    createInfo.flags = 0;
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;

    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(_instance, "vkCreateDebugUtilsMessengerEXT");
    if (func == nullptr) throw std::runtime_error("failed to set up debug messenger!");
    auto result = func(_instance, &createInfo, nullptr, &_debugMessenger);
    if (result != VK_SUCCESS) throw std::runtime_error("failed to set up debug messenger!");

    PFN_vkCreateDebugReportCallbackEXT CreateDebugReportCallback = VK_NULL_HANDLE;
    CreateDebugReportCallback = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(
        _instance, "vkCreateDebugReportCallbackEXT");
  }
}

const VkInstance& Instance::getInstance() { return _instance; }

bool Instance::isValidation() { return _validation; }

const std::vector<const char*>& Instance::getValidationLayers() { return _validationLayers; }

Instance::~Instance() {
  if (_validation) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(_instance,
                                                                           "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
      func(_instance, _debugMessenger, nullptr);
    }
  }
  vkDestroyInstance(_instance, nullptr);
}