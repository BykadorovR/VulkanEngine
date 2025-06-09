#define STB_IMAGE_IMPLEMENTATION
#include "Graphic/Texture.h"
#include "Vulkan/Buffer.h"

Texture::Texture(std::shared_ptr<BufferImage> data,
                 VkFormat format,
                 VkSamplerAddressMode mode,
                 int mipMapLevels,
                 VkFilter filter,
                 std::shared_ptr<CommandBuffer> commandBufferTransfer,
                 std::shared_ptr<EngineState> engineState) {
  _mipMapLevels = mipMapLevels;
  // image
  auto image = std::make_shared<Image>(
      data->getResolution(), 1, mipMapLevels, format, VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, engineState);
  image->changeLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 1,
                      mipMapLevels, commandBufferTransfer);
  image->copyFrom(data, {0}, commandBufferTransfer);
  // TODO: generate mipmaps here
  // TODO: use commandBuffer with graphic support for Blit (!)
  image->generateMipmaps(mipMapLevels, 1, commandBufferTransfer);

  // image view
  _imageView = std::make_shared<ImageView>(image, VK_IMAGE_VIEW_TYPE_2D, 0, 1, 0, mipMapLevels,
                                           VK_IMAGE_ASPECT_COLOR_BIT, engineState);
  _sampler = std::make_shared<Sampler>(mode, mipMapLevels, engineState->getSettings()->getAnisotropicSamples(), filter,
                                       engineState);
}

Texture::Texture(VkSamplerAddressMode mode,
                 int mipMapLevels,
                 VkFilter filter,
                 std::shared_ptr<ImageView> imageView,
                 std::shared_ptr<EngineState> engineState) {
  _mipMapLevels = mipMapLevels;
  _imageView = imageView;
  _sampler = std::make_shared<Sampler>(mode, mipMapLevels, engineState->getSettings()->getAnisotropicSamples(), filter,
                                       engineState);
}

void Texture::copyFrom(std::shared_ptr<BufferImage> data, std::shared_ptr<CommandBuffer> commandBuffer) {
  auto image = getImageView()->getImage();
  image->changeLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_ASPECT_COLOR_BIT, 1, _mipMapLevels, commandBuffer);
  image->copyFrom(data, {0}, commandBuffer);
  image->generateMipmaps(_mipMapLevels, 1, commandBuffer);
}

std::shared_ptr<ImageView> Texture::getImageView() { return _imageView; }

std::shared_ptr<Sampler> Texture::getSampler() { return _sampler; }