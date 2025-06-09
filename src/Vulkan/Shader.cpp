#include "Vulkan/Shader.h"

VkShaderModule Shader::_createShaderModule(const std::vector<char>& code) {
  VkShaderModuleCreateInfo createInfo{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                      .codeSize = code.size(),
                                      .pCode = reinterpret_cast<const uint32_t*>(code.data())};

  VkShaderModule shaderModule;
  if (vkCreateShaderModule(_engineState->getDevice()->getLogicalDevice(), &createInfo, nullptr, &shaderModule) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create shader module!");
  }

  return shaderModule;
}

Shader::Shader(std::shared_ptr<EngineState> engineState) { _engineState = engineState; }

void Shader::add(std::string path, VkShaderStageFlagBits type) {
  auto globalPath = _engineState->getSettings()->getShadersPath();
  auto shaderCode = _engineState->getFilesystem()->readFile<char>(globalPath + path);
  VkShaderModule shaderModule = _createShaderModule(shaderCode);
  _shaders[type] = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = type,
                    .module = shaderModule,
                    .pName = "main"};
}

void Shader::setSpecializationInfo(VkSpecializationInfo info, VkShaderStageFlagBits type) {
  _specializationInfo = info;
  _shaders[type].pSpecializationInfo = &_specializationInfo;
}

std::vector<VkPipelineShaderStageCreateInfo> Shader::getShaderStageInfos() {
  std::vector<VkPipelineShaderStageCreateInfo> shaderStageInfos;
  for (auto [key, value] : _shaders) {
    shaderStageInfos.push_back(value);
  }
  return shaderStageInfos;
}

VkPipelineShaderStageCreateInfo Shader::getShaderStageInfo(VkShaderStageFlagBits type) { return _shaders[type]; }

Shader::~Shader() {
  for (auto& [type, shader] : _shaders)
    vkDestroyShaderModule(_engineState->getDevice()->getLogicalDevice(), shader.module, nullptr);
}