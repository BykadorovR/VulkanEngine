#include <array>
#include "Primitive/Cubemap.h"

Cubemap::Cubemap(std::shared_ptr<ImageDataBuffer> data,
                 VkFormat format,
                 int mipMapLevels,
                 VkImageAspectFlagBits colorBits,
                 VkImageUsageFlags usage,
                 VkFilter filter,
                 std::shared_ptr<EngineState> engineState) {
  _engineState = engineState;
  // image
  // usage VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
  auto image = std::make_unique<RenderGraph::Image>(*engineState->getMemoryAllocator());
  image->createImage(format, {std::get<0>(data->getResolution()), std::get<1>(data->getResolution())}, mipMapLevels,
                     data->getNumber(), colorBits, usage, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);
  const auto* bytes = static_cast<const std::byte*>(data->buffer->getAllocationInfo().pMappedData);
  const std::span imageBytes(bytes, static_cast<std::size_t>(data->buffer->getSize()));
  const VkDeviceSize imageSize = std::get<0>(data->getResolution()) * std::get<1>(data->getResolution()) *
                                 data->getChannels();
  const std::array<VkDeviceSize, 6> offsets{0, imageSize, imageSize * 2, imageSize * 3, imageSize * 4, imageSize * 5};
  _engineState->getResourceUploader()->upload(*image, imageBytes, offsets);
  _engineState->getResourceUploader()->submit();

  _imageViewSeparate.resize(6);
  _textureSeparate.resize(6);
  for (int i = 0; i < 6; i++) {
    _imageViewSeparate[i].resize(mipMapLevels);
    _textureSeparate[i].resize(mipMapLevels);
    for (int j = 0; j < mipMapLevels; j++) {
      auto faceImage = std::make_unique<RenderGraph::Image>(*engineState->getMemoryAllocator());
      faceImage->wrapImage(image->getImage(), format,
                           {std::get<0>(data->getResolution()), std::get<1>(data->getResolution())}, 1, 1, colorBits,
                           usage);
      _imageViewSeparate[i][j] = std::make_shared<RenderGraph::ImageView>(std::move(faceImage),
                                                                          *engineState->getDevice());
      _imageViewSeparate[i][j]->createImageView(VK_IMAGE_VIEW_TYPE_2D, j, i);
      auto faceSampler = std::make_shared<RenderGraph::Sampler>(*engineState->getDevice());
      faceSampler->createSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1,
                                 engineState->getSettings()->getAnisotropicSamples(), filter);
      _textureSeparate[i][j] = std::make_shared<RenderGraph::Texture>(_imageViewSeparate[i][j], std::move(faceSampler));
    }
  }
  _imageView = std::make_shared<RenderGraph::ImageView>(std::move(image), *engineState->getDevice());
  _imageView->createImageView(VK_IMAGE_VIEW_TYPE_CUBE, 0, 0);
  auto cubemapSampler = std::make_shared<RenderGraph::Sampler>(*engineState->getDevice());
  cubemapSampler->createSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, mipMapLevels,
                                engineState->getSettings()->getAnisotropicSamples(), filter);
  _texture = std::make_shared<RenderGraph::Texture>(_imageView, std::move(cubemapSampler));
}

Cubemap::Cubemap(std::tuple<int, int> resolution,
                 VkFormat format,
                 int mipMapLevels,
                 VkImageLayout layout,
                 VkImageAspectFlagBits colorBits,
                 VkImageUsageFlags usage,
                 VkFilter filter,
                 const RenderGraph::CommandBuffer* commandBufferTransfer,
                 std::shared_ptr<EngineState> engineState) {
  _engineState = engineState;
  // usage VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
  auto image = std::make_unique<RenderGraph::Image>(*engineState->getMemoryAllocator());
  image->createImage(format, {std::get<0>(resolution), std::get<1>(resolution)}, mipMapLevels, 6, colorBits, usage,
                     VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);
  // VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
  image->changeLayout(VK_IMAGE_LAYOUT_UNDEFINED, layout, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                      VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, *commandBufferTransfer);

  _imageViewSeparate.resize(6);
  _textureSeparate.resize(6);
  for (int i = 0; i < 6; i++) {
    _imageViewSeparate[i].resize(mipMapLevels);
    _textureSeparate[i].resize(mipMapLevels);
    for (int j = 0; j < mipMapLevels; j++) {
      auto faceImage = std::make_unique<RenderGraph::Image>(*engineState->getMemoryAllocator());
      faceImage->wrapImage(image->getImage(), format, {std::get<0>(resolution), std::get<1>(resolution)}, 1, 1,
                           colorBits, usage);
      _imageViewSeparate[i][j] = std::make_shared<RenderGraph::ImageView>(std::move(faceImage),
                                                                          *engineState->getDevice());
      _imageViewSeparate[i][j]->createImageView(VK_IMAGE_VIEW_TYPE_2D, j, i);
      auto faceSampler = std::make_shared<RenderGraph::Sampler>(*engineState->getDevice());
      faceSampler->createSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1,
                                 engineState->getSettings()->getAnisotropicSamples(), filter);
      _textureSeparate[i][j] = std::make_shared<RenderGraph::Texture>(_imageViewSeparate[i][j], std::move(faceSampler));
    }
  }
  _imageView = std::make_shared<RenderGraph::ImageView>(std::move(image), *engineState->getDevice());
  _imageView->createImageView(VK_IMAGE_VIEW_TYPE_CUBE, 0, 0);
  auto cubemapSampler = std::make_shared<RenderGraph::Sampler>(*engineState->getDevice());
  cubemapSampler->createSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, mipMapLevels,
                                engineState->getSettings()->getAnisotropicSamples(), filter);
  _texture = std::make_shared<RenderGraph::Texture>(_imageView, std::move(cubemapSampler));
}

std::shared_ptr<RenderGraph::Texture> Cubemap::getTexture() { return _texture; }

std::vector<std::vector<std::shared_ptr<RenderGraph::Texture>>> Cubemap::getTextureSeparate() {
  return _textureSeparate;
}
