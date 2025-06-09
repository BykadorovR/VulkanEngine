#include "Primitive/Cubemap.h"
#include "Vulkan/Buffer.h"

Cubemap::Cubemap(std::shared_ptr<BufferImage> data,
                 VkFormat format,
                 int mipMapLevels,
                 VkImageAspectFlagBits colorBits,
                 VkImageUsageFlags usage,
                 VkFilter filter,
                 std::shared_ptr<CommandBuffer> commandBufferTransfer,
                 std::shared_ptr<EngineState> engineState) {
  _engineState = engineState;
  // image
  // usage VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
  _image = std::make_shared<Image>(data->getResolution(), data->getNumber(), mipMapLevels, format,
                                   VK_IMAGE_TILING_OPTIMAL, usage, engineState);
  _image->changeLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, colorBits, 6, mipMapLevels,
                       commandBufferTransfer);
  int imageSize = std::get<0>(data->getResolution()) * std::get<1>(data->getResolution()) * data->getChannels();
  _image->copyFrom(data, {0, imageSize, imageSize * 2, imageSize * 3, imageSize * 4, imageSize * 5},
                   commandBufferTransfer);

  _image->generateMipmaps(mipMapLevels, 6, commandBufferTransfer);

  // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
  //_image->changeLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, layout, colorBits, 6, mipMapLevels,
  // commandBufferTransfer);
  _imageView = std::make_shared<ImageView>(_image, VK_IMAGE_VIEW_TYPE_CUBE, 0, 6, 0, mipMapLevels, colorBits,
                                           engineState);
  _texture = std::make_shared<Texture>(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, mipMapLevels, filter, _imageView,
                                       _engineState);

  _imageViewSeparate.resize(6);
  _textureSeparate.resize(6);
  for (int i = 0; i < 6; i++) {
    _imageViewSeparate[i].resize(mipMapLevels);
    _textureSeparate[i].resize(mipMapLevels);
    for (int j = 0; j < mipMapLevels; j++) {
      _imageViewSeparate[i][j] = std::make_shared<ImageView>(_image, VK_IMAGE_VIEW_TYPE_2D, i, 1, j, 1, colorBits,
                                                             engineState);
      _textureSeparate[i][j] = std::make_shared<Texture>(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1, filter,
                                                         _imageViewSeparate[i][j], _engineState);
    }
  }
}

Cubemap::Cubemap(std::tuple<int, int> resolution,
                 VkFormat format,
                 int mipMapLevels,
                 VkImageLayout layout,
                 VkImageAspectFlagBits colorBits,
                 VkImageUsageFlags usage,
                 VkFilter filter,
                 std::shared_ptr<CommandBuffer> commandBufferTransfer,
                 std::shared_ptr<EngineState> engineState) {
  _engineState = engineState;
  // usage VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
  _image = std::make_shared<Image>(resolution, 6, mipMapLevels, format, VK_IMAGE_TILING_OPTIMAL, usage, engineState);
  // VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
  _image->changeLayout(VK_IMAGE_LAYOUT_UNDEFINED, layout, colorBits, 6, mipMapLevels, commandBufferTransfer);
  _imageView = std::make_shared<ImageView>(_image, VK_IMAGE_VIEW_TYPE_CUBE, 0, 6, 0, mipMapLevels, colorBits,
                                           engineState);
  _texture = std::make_shared<Texture>(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, mipMapLevels, filter, _imageView,
                                       _engineState);

  _imageViewSeparate.resize(6);
  _textureSeparate.resize(6);
  for (int i = 0; i < 6; i++) {
    _imageViewSeparate[i].resize(mipMapLevels);
    _textureSeparate[i].resize(mipMapLevels);
    for (int j = 0; j < mipMapLevels; j++) {
      _imageViewSeparate[i][j] = std::make_shared<ImageView>(_image, VK_IMAGE_VIEW_TYPE_2D, i, 1, j, 1, colorBits,
                                                             engineState);
      _textureSeparate[i][j] = std::make_shared<Texture>(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1, filter,
                                                         _imageViewSeparate[i][j], _engineState);
    }
  }
}

std::shared_ptr<Texture> Cubemap::getTexture() { return _texture; }

std::vector<std::vector<std::shared_ptr<Texture>>> Cubemap::getTextureSeparate() { return _textureSeparate; }