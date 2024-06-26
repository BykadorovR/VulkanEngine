#include "Descriptor.h"

DescriptorSetLayout::DescriptorSetLayout(std::shared_ptr<Device> device) { _device = device; }

void DescriptorSetLayout::createCustom(std::vector<VkDescriptorSetLayoutBinding> info) {
  _info = info;

  auto layoutInfo = VkDescriptorSetLayoutCreateInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = _info.size();
  layoutInfo.pBindings = _info.data();

  if (vkCreateDescriptorSetLayout(_device->getLogicalDevice(), &layoutInfo, nullptr, &_descriptorSetLayout) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor set layout!");
  }
}

void DescriptorSetLayout::createPostprocessing() {
  _info.resize(3);

  _info[0].binding = 0;
  _info[0].descriptorCount = 1;
  _info[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  _info[0].pImmutableSamplers = nullptr;
  _info[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

  _info[1].binding = 1;
  _info[1].descriptorCount = 1;
  _info[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  _info[1].pImmutableSamplers = nullptr;
  _info[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

  _info[2].binding = 2;
  _info[2].descriptorCount = 1;
  _info[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  _info[2].pImmutableSamplers = nullptr;
  _info[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

  auto layoutInfo = VkDescriptorSetLayoutCreateInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = _info.size();
  layoutInfo.pBindings = _info.data();

  if (vkCreateDescriptorSetLayout(_device->getLogicalDevice(), &layoutInfo, nullptr, &_descriptorSetLayout) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor set layout!");
  }
}

void DescriptorSetLayout::createShaderStorageBuffer(std::vector<int> bindings, std::vector<VkShaderStageFlags> stage) {
  _info.resize(bindings.size());
  for (int i = 0; i < _info.size(); i++) {
    _info[i].binding = i;
    _info[i].descriptorCount = 1;
    _info[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    _info[i].pImmutableSamplers = nullptr;
    _info[i].stageFlags = stage[i];
  }

  auto layoutInfo = VkDescriptorSetLayoutCreateInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = _info.size();
  layoutInfo.pBindings = _info.data();

  if (vkCreateDescriptorSetLayout(_device->getLogicalDevice(), &layoutInfo, nullptr, &_descriptorSetLayout) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor set layout!");
  }
}

void DescriptorSetLayout::createParticleComputeBuffer() {
  _info.resize(3);
  _info[0].binding = 0;
  _info[0].descriptorCount = 1;
  _info[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  _info[0].pImmutableSamplers = nullptr;
  _info[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

  _info[1].binding = 1;
  _info[1].descriptorCount = 1;
  _info[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  _info[1].pImmutableSamplers = nullptr;
  _info[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

  _info[2].binding = 2;
  _info[2].descriptorCount = 1;
  _info[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  _info[2].pImmutableSamplers = nullptr;
  _info[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

  auto layoutInfo = VkDescriptorSetLayoutCreateInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = _info.size();
  layoutInfo.pBindings = _info.data();

  if (vkCreateDescriptorSetLayout(_device->getLogicalDevice(), &layoutInfo, nullptr, &_descriptorSetLayout) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor set layout!");
  }
}

void DescriptorSetLayout::createUniformBuffer(VkShaderStageFlags stage) {
  _info.resize(1);
  _info[0].binding = 0;
  _info[0].descriptorCount = 1;
  _info[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  _info[0].pImmutableSamplers = nullptr;
  _info[0].stageFlags = stage;

  auto layoutInfo = VkDescriptorSetLayoutCreateInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = _info.size();
  layoutInfo.pBindings = _info.data();

  if (vkCreateDescriptorSetLayout(_device->getLogicalDevice(), &layoutInfo, nullptr, &_descriptorSetLayout) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor set layout!");
  }
}

void DescriptorSetLayout::createGUI() {
  _info.resize(2);
  _info[0].binding = 0;
  _info[0].descriptorCount = 1;
  _info[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  _info[0].pImmutableSamplers = nullptr;
  _info[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  _info[1].binding = 1;
  _info[1].descriptorCount = 1;
  _info[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  _info[1].pImmutableSamplers = nullptr;
  _info[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  auto layoutInfo = VkDescriptorSetLayoutCreateInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = _info.size();
  layoutInfo.pBindings = _info.data();

  if (vkCreateDescriptorSetLayout(_device->getLogicalDevice(), &layoutInfo, nullptr, &_descriptorSetLayout) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor set layout!");
  }
}

std::vector<VkDescriptorSetLayoutBinding> DescriptorSetLayout::getLayoutInfo() { return _info; }

VkDescriptorSetLayout& DescriptorSetLayout::getDescriptorSetLayout() { return _descriptorSetLayout; }

DescriptorSetLayout::~DescriptorSetLayout() {
  vkDestroyDescriptorSetLayout(_device->getLogicalDevice(), _descriptorSetLayout, nullptr);
}

DescriptorSet::DescriptorSet(int number,
                             std::shared_ptr<DescriptorSetLayout> layout,
                             std::shared_ptr<DescriptorPool> pool,
                             std::shared_ptr<Device> device) {
  _descriptorSets.resize(number);
  _device = device;
  _layout = layout;

  std::vector<VkDescriptorSetLayout> layouts(_descriptorSets.size(), layout->getDescriptorSetLayout());
  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = pool->getDescriptorPool();
  allocInfo.descriptorSetCount = static_cast<uint32_t>(_descriptorSets.size());
  allocInfo.pSetLayouts = layouts.data();

  auto sts = vkAllocateDescriptorSets(_device->getLogicalDevice(), &allocInfo, _descriptorSets.data());
  if (sts != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate descriptor sets!");
  }
}

void DescriptorSet::createPostprocessing(std::shared_ptr<ImageView> src,
                                         std::shared_ptr<ImageView> blur,
                                         std::shared_ptr<ImageView> dst) {
  VkDescriptorImageInfo imageInfoSrc{};
  imageInfoSrc.imageLayout = src->getImage()->getImageLayout();
  imageInfoSrc.imageView = src->getImageView();

  VkDescriptorImageInfo imageInfoBlur{};
  imageInfoBlur.imageLayout = blur->getImage()->getImageLayout();
  imageInfoBlur.imageView = blur->getImageView();

  VkDescriptorImageInfo imageInfoDst{};
  imageInfoDst.imageLayout = dst->getImage()->getImageLayout();
  imageInfoDst.imageView = dst->getImageView();

  std::array<VkWriteDescriptorSet, 3> descriptorWrites{};
  descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[0].dstSet = _descriptorSets[0];
  descriptorWrites[0].dstBinding = 0;
  descriptorWrites[0].dstArrayElement = 0;
  descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  descriptorWrites[0].descriptorCount = 1;
  descriptorWrites[0].pImageInfo = &imageInfoSrc;

  descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[1].dstSet = _descriptorSets[0];
  descriptorWrites[1].dstBinding = 1;
  descriptorWrites[1].dstArrayElement = 0;
  descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  descriptorWrites[1].descriptorCount = 1;
  descriptorWrites[1].pImageInfo = &imageInfoBlur;

  descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[2].dstSet = _descriptorSets[0];
  descriptorWrites[2].dstBinding = 2;
  descriptorWrites[2].dstArrayElement = 0;
  descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  descriptorWrites[2].descriptorCount = 1;
  descriptorWrites[2].pImageInfo = &imageInfoDst;

  vkUpdateDescriptorSets(_device->getLogicalDevice(), descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
}

void DescriptorSet::createBlur(std::vector<std::shared_ptr<Texture>> src, std::vector<std::shared_ptr<Texture>> dst) {
  for (size_t i = 0; i < _descriptorSets.size(); i++) {
    VkDescriptorImageInfo imageInfoSrc{};
    imageInfoSrc.imageLayout = src[i]->getImageView()->getImage()->getImageLayout();

    imageInfoSrc.imageView = src[i]->getImageView()->getImageView();

    VkDescriptorImageInfo imageInfoDst{};
    imageInfoDst.imageLayout = dst[i]->getImageView()->getImage()->getImageLayout();

    imageInfoDst.imageView = dst[i]->getImageView()->getImageView();

    std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = _descriptorSets[i];
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pImageInfo = &imageInfoSrc;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = _descriptorSets[i];
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &imageInfoDst;

    vkUpdateDescriptorSets(_device->getLogicalDevice(), descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
  }
}

void DescriptorSet::createParticleComputeBuffer(std::shared_ptr<UniformBuffer> uniformBuffer,
                                                std::shared_ptr<Buffer> bufferIn,
                                                std::shared_ptr<Buffer> bufferOut) {
  for (size_t i = 0; i < _descriptorSets.size(); i++) {
    VkDescriptorBufferInfo bufferUniformInfo{};
    bufferUniformInfo.buffer = uniformBuffer->getBuffer()[i]->getData();
    bufferUniformInfo.offset = 0;
    bufferUniformInfo.range = uniformBuffer->getBuffer()[i]->getSize();

    VkDescriptorBufferInfo bufferInInfo{};
    bufferInInfo.buffer = bufferIn->getData();
    bufferInInfo.offset = 0;
    bufferInInfo.range = bufferIn->getSize();

    VkDescriptorBufferInfo bufferOutInfo{};
    bufferOutInfo.buffer = bufferOut->getData();
    bufferOutInfo.offset = 0;
    bufferOutInfo.range = bufferOut->getSize();

    std::array<VkWriteDescriptorSet, 3> descriptorWrites{};
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = _descriptorSets[i];
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &bufferUniformInfo;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = _descriptorSets[i];
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pBufferInfo = &bufferInInfo;

    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = _descriptorSets[i];
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].dstArrayElement = 0;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].pBufferInfo = &bufferOutInfo;

    vkUpdateDescriptorSets(_device->getLogicalDevice(), descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
  }
}

void DescriptorSet::createShaderStorageBuffer(int currentFrame,
                                              std::vector<int> bindings,
                                              std::vector<std::vector<std::shared_ptr<Buffer>>> buffer) {
  std::vector<VkDescriptorBufferInfo> bufferInfo(buffer.size());
  std::vector<VkWriteDescriptorSet> descriptorWrites(buffer.size());
  for (int j = 0; j < buffer.size(); j++) {
    bufferInfo[j].buffer = buffer[j][currentFrame]->getData();
    bufferInfo[j].offset = 0;
    bufferInfo[j].range = buffer[j][currentFrame]->getSize();

    descriptorWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[j].dstSet = _descriptorSets[currentFrame];
    descriptorWrites[j].dstBinding = bindings[j];
    descriptorWrites[j].dstArrayElement = 0;
    descriptorWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[j].descriptorCount = 1;
    descriptorWrites[j].pBufferInfo = &bufferInfo[j];
  }

  vkUpdateDescriptorSets(_device->getLogicalDevice(), descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
}

void DescriptorSet::createCustom(int currentFrame,
                                 std::map<int, std::vector<VkDescriptorBufferInfo>> buffers,
                                 std::map<int, std::vector<VkDescriptorImageInfo>> images) {
  auto info = _layout->getLayoutInfo();
  std::vector<VkWriteDescriptorSet> descriptorWrites(info.size());
  for (int i = 0; i < info.size(); i++) {
    descriptorWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[i].dstSet = _descriptorSets[currentFrame];
    descriptorWrites[i].dstBinding = info[i].binding;
    descriptorWrites[i].dstArrayElement = 0;
    descriptorWrites[i].descriptorType = info[i].descriptorType;
    descriptorWrites[i].descriptorCount = info[i].descriptorCount;
    switch (descriptorWrites[i].descriptorType) {
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        descriptorWrites[i].pBufferInfo = buffers[descriptorWrites[i].dstBinding].data();
        break;
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        descriptorWrites[i].pImageInfo = images[descriptorWrites[i].dstBinding].data();
    }
  }

  vkUpdateDescriptorSets(_device->getLogicalDevice(), descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
}

void DescriptorSet::updateImages(int currentFrame, std::map<int, std::vector<VkDescriptorImageInfo>> images) {
  auto info = _layout->getLayoutInfo();
  std::vector<VkWriteDescriptorSet> descriptorWrites;
  for (auto& [key, value] : images) {
    VkWriteDescriptorSet descriptor{};
    descriptor.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor.dstSet = _descriptorSets[currentFrame];
    descriptor.dstBinding = key;
    descriptor.dstArrayElement = 0;
    descriptor.descriptorType = info[key].descriptorType;
    descriptor.descriptorCount = info[key].descriptorCount;
    descriptor.pImageInfo = value.data();
    descriptorWrites.push_back(descriptor);
  }

  vkUpdateDescriptorSets(_device->getLogicalDevice(), descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
}

void DescriptorSet::createUniformBuffer(std::shared_ptr<UniformBuffer> uniformBuffer) {
  for (size_t i = 0; i < _descriptorSets.size(); i++) {
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBuffer->getBuffer()[i]->getData();
    bufferInfo.offset = 0;
    bufferInfo.range = uniformBuffer->getBuffer()[i]->getSize();

    VkWriteDescriptorSet descriptorWrites{};
    descriptorWrites.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites.dstSet = _descriptorSets[i];
    descriptorWrites.dstBinding = 0;
    descriptorWrites.dstArrayElement = 0;
    descriptorWrites.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites.descriptorCount = 1;
    descriptorWrites.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(_device->getLogicalDevice(), 1, &descriptorWrites, 0, nullptr);
  }
}

void DescriptorSet::createGUI(std::shared_ptr<Texture> texture, std::shared_ptr<UniformBuffer> uniformBuffer) {
  for (size_t i = 0; i < _descriptorSets.size(); i++) {
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBuffer->getBuffer()[i]->getData();
    bufferInfo.offset = 0;
    bufferInfo.range = uniformBuffer->getBuffer()[i]->getSize();

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = texture->getImageView()->getImage()->getImageLayout();

    imageInfo.imageView = texture->getImageView()->getImageView();
    imageInfo.sampler = texture->getSampler()->getSampler();

    std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = _descriptorSets[i];
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &bufferInfo;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = _descriptorSets[i];
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(_device->getLogicalDevice(), static_cast<uint32_t>(descriptorWrites.size()),
                           descriptorWrites.data(), 0, nullptr);
  }
}

std::vector<VkDescriptorSet>& DescriptorSet::getDescriptorSets() { return _descriptorSets; }