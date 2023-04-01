#pragma once
#include "Device.h"
#include "Shader.h"
#include "Buffer.h"
#include "Render.h"
#include "Descriptor.h"

class Pipeline {
 private:
  std::shared_ptr<Device> _device;
  std::vector<std::shared_ptr<DescriptorSetLayout>> _descriptorSetLayout;
  VkPipeline _pipeline;
  VkPipelineLayout _pipelineLayout;
  std::shared_ptr<Shader> _shader;

 public:
  Pipeline(std::shared_ptr<Shader> shader,
           std::vector<std::shared_ptr<DescriptorSetLayout>> descriptorSetLayout,
           std::shared_ptr<Device> device);
  void createGraphic2D(VkVertexInputBindingDescription bindingDescription,
                       std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions,
                       std::shared_ptr<RenderPass> renderPass);
  void createGraphic3D(VkCullModeFlags cullMode,
                       VkVertexInputBindingDescription bindingDescription,
                       std::array<VkVertexInputAttributeDescription, 7> attributeDescriptions,
                       VkPushConstantRange pushConstants,
                       std::shared_ptr<RenderPass> renderPass);
  void createHUD(VkVertexInputBindingDescription bindingDescription,
                 std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions,
                 std::shared_ptr<RenderPass> renderPass);
  void createCompute();

  VkPipeline& getPipeline();
  VkPipelineLayout& getPipelineLayout();
  ~Pipeline();
};