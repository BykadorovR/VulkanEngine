#include "Vulkan/Image.h"

Image::Image(VkImage& image,
             std::tuple<int, int> resolution,
             VkFormat format,
             std::shared_ptr<EngineState> engineState) {
  _image = image;
  _engineState = engineState;
  _resolution = resolution;
  _format = format;

  _external = true;
}

Image::Image(std::tuple<int, int> resolution,
             int layers,
             int mipMapLevels,
             VkFormat format,
             VkImageTiling tiling,
             VkImageUsageFlags usage,
             std::shared_ptr<EngineState> engineState) {
  _engineState = engineState;
  _resolution = resolution;
  _layers = layers;
  _mipMapLevels = mipMapLevels;
  _format = format;
  _layers = layers;
  _usage = usage;

  _imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  auto imageInfo = VkImageCreateInfo{
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = format,
      .extent = {.width = static_cast<uint32_t>(std::get<0>(resolution)),
                 .height = static_cast<uint32_t>(std::get<1>(resolution)),
                 .depth = 1},
      .mipLevels = static_cast<uint32_t>(mipMapLevels),
      .arrayLayers = static_cast<uint32_t>(layers),
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = tiling,
      .usage = usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = _imageLayout,
  };
  // for cubemap need to set flag
  if (layers == 6) imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

  VmaAllocationCreateInfo allocCreateInfo = {};
  allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

  vmaCreateImage(_engineState->getMemoryAllocator()->getAllocator(), &imageInfo, &allocCreateInfo, &_image,
                 &_imageMemory, nullptr);
}
int Image::getLayersNumber() { return _layers; }

int Image::getMipMapLevels() { return _mipMapLevels; }

VkImageTiling Image::getTiling() { return _tiling; }

VkImageUsageFlags Image::getUsage() { return _usage; }

VkFormat& Image::getFormat() { return _format; }

void Image::overrideLayout(VkImageLayout layout) { _imageLayout = layout; }

std::tuple<int, int> Image::getResolution() { return _resolution; }

void Image::generateMipmaps(int mipMapLevels, int layers, std::shared_ptr<CommandBuffer> commandBuffer) {
  VkImageMemoryBarrier barrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                               .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                               .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                               .image = getImage(),
                               .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                    .levelCount = 1,
                                                    .baseArrayLayer = 0,
                                                    .layerCount = static_cast<uint32_t>(layers)}};
  auto [mipWidth, mipHeight] = getResolution();
  for (uint32_t i = 1; i < mipMapLevels; i++) {
    // change layout of source image to SRC so we can resize it and generate i-th mip map level
    barrier.subresourceRange.baseMipLevel = i - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer->getCommandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkImageBlit blit{};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {mipWidth, mipHeight, 1};  // previous image size
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel = i - 1;  // previous mip map image used for resize
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = layers;
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1};
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.mipLevel = i;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = layers;

    // i = 0 has SRC layout (we changed it above), i = 1 has DST layout (we changed from undefined to dst in
    // constructor)
    vkCmdBlitImage(commandBuffer->getCommandBuffer(), getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, getImage(),
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

    // change i = 0 to READ OPTIMAL, we won't use this level anymore, next resizes will use next i
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer->getCommandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    if (mipWidth > 1) mipWidth /= 2;
    if (mipHeight > 1) mipHeight /= 2;
  }

  // we don't generate mip map from last level so we need explicitly change dst to read only
  barrier.subresourceRange.baseMipLevel = mipMapLevels - 1;
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  vkCmdPipelineBarrier(commandBuffer->getCommandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

  // we changed real image layout above, need to override imageLayout internal field
  overrideLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void Image::changeLayout(VkImageLayout oldLayout,
                         VkImageLayout newLayout,
                         VkImageAspectFlags aspectMask,
                         int layersNumber,
                         int mipMapLevels,
                         std::shared_ptr<CommandBuffer> commandBufferTransfer) {
  _imageLayout = newLayout;
  VkImageMemoryBarrier barrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                               .oldLayout = oldLayout,
                               .newLayout = newLayout,
                               .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                               .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                               .image = _image,
                               .subresourceRange = {.aspectMask = aspectMask,
                                                    .baseMipLevel = 0,
                                                    .levelCount = static_cast<uint32_t>(mipMapLevels),
                                                    .baseArrayLayer = 0,
                                                    .layerCount = static_cast<uint32_t>(layersNumber)}};

  switch (oldLayout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
      // Image layout is undefined (or does not matter)
      // Only valid as initial layout
      // No flags required, listed only for completeness
      barrier.srcAccessMask = 0;
      break;

    case VK_IMAGE_LAYOUT_PREINITIALIZED:
      // Image is preinitialized
      // Only valid as initial layout for linear images, preserves memory contents
      // Make sure host writes have been finished
      barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
      break;

    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      // Image is a color attachment
      // Make sure any writes to the color buffer have been finished
      barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      break;

    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      // Image is a depth/stencil attachment
      // Make sure any writes to the depth/stencil buffer have been finished
      barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      break;

    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      // Image is a transfer source
      // Make sure any reads from the image have been finished
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      break;

    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      // Image is a transfer destination
      // Make sure any writes to the image have been finished
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      break;

    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      // Image is read by a shader
      // Make sure any shader reads from the image have been finished
      barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
      break;
    default:
      // Other source layouts aren't handled (yet)
      break;
  }

  // Target layouts (new)
  // Destination access mask controls the dependency for the new image layout
  switch (newLayout) {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      // Image will be used as a transfer destination
      // Make sure any writes to the image have been finished
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      break;

    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      // Image will be used as a transfer source
      // Make sure any reads from the image have been finished
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      break;

    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      // Image will be used as a color attachment
      // Make sure any writes to the color buffer have been finished
      barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      break;

    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
      // Image layout will be used as a depth/stencil attachment
      // Make sure any writes to depth/stencil buffer have been finished
      barrier.dstAccessMask = barrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      // Image will be read in a shader (sampler, input attachment)
      // Make sure any writes to the image have been finished
      if (barrier.srcAccessMask == 0) {
        barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
      }
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      break;
    default:
      // Other source layouts aren't handled (yet)
      break;
  }

  vkCmdPipelineBarrier(commandBufferTransfer->getCommandBuffer(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void Image::setData(std::shared_ptr<Buffer> buffer) {}

void Image::copyFrom(std::shared_ptr<Buffer> buffer,
                     std::vector<int> bufferOffsets,
                     std::shared_ptr<CommandBuffer> commandBufferTransfer) {
  _stagingBuffer = buffer;

  std::vector<VkBufferImageCopy> bufferCopyRegions;
  for (int i = 0; i < bufferOffsets.size(); i++) {
    VkBufferImageCopy region{
        .bufferOffset = static_cast<VkDeviceSize>(bufferOffsets[i]),
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .mipLevel = 0,
                             .baseArrayLayer = static_cast<uint32_t>(i),
                             .layerCount = 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = {(uint32_t)std::get<0>(_resolution), (uint32_t)std::get<1>(_resolution), 1}};

    bufferCopyRegions.push_back(region);
  }

  int currentFrame = _engineState->getFrameInFlight();
  vkCmdCopyBufferToImage(commandBufferTransfer->getCommandBuffer(), buffer->getData(), _image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, bufferCopyRegions.size(), bufferCopyRegions.data());
  // need to insert memory barrier so read in fragment shader waits for copy
  VkMemoryBarrier memoryBarrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                   .pNext = nullptr,
                                   .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                   .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
  vkCmdPipelineBarrier(commandBufferTransfer->getCommandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);
}

VkImageLayout& Image::getImageLayout() { return _imageLayout; }

VkImage& Image::getImage() { return _image; }

Image::~Image() {
  if (_external == false) {
    vmaDestroyImage(_engineState->getMemoryAllocator()->getAllocator(), _image, _imageMemory);
  }
}

ImageView::ImageView(std::shared_ptr<Image> image,
                     VkImageViewType type,
                     int baseArrayLayer,
                     int arrayLayerNumber,
                     int baseMipMap,
                     int mipMapNumber,
                     VkImageAspectFlags aspectFlags,
                     std::shared_ptr<EngineState> engineState) {
  _engineState = engineState;
  _image = image;

  VkImageViewCreateInfo viewInfo{.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                 .image = image->getImage(),
                                 .viewType = type,
                                 .format = image->getFormat(),
                                 .components = {.r = VK_COMPONENT_SWIZZLE_IDENTITY,
                                                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                                                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                                                .a = VK_COMPONENT_SWIZZLE_IDENTITY},
                                 .subresourceRange = {
                                     .aspectMask = aspectFlags,
                                     .baseMipLevel = static_cast<uint32_t>(baseMipMap),
                                     .levelCount = static_cast<uint32_t>(mipMapNumber),
                                     .baseArrayLayer = static_cast<uint32_t>(baseArrayLayer),
                                     .layerCount = static_cast<uint32_t>(arrayLayerNumber),
                                 }};

  if (vkCreateImageView(_engineState->getDevice()->getLogicalDevice(), &viewInfo, nullptr, &_imageView) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture image view!");
  }
}

ImageView::ImageView(std::shared_ptr<Image> image, VkImageView& imageView, std::shared_ptr<EngineState> engineState) {
  _image = image;
  _imageView = imageView;
  _engineState = engineState;
}

VkImageView& ImageView::getImageView() { return _imageView; }

std::shared_ptr<Image> ImageView::getImage() { return _image; }

ImageView::~ImageView() { vkDestroyImageView(_engineState->getDevice()->getLogicalDevice(), _imageView, nullptr); }

Framebuffer::Framebuffer(std::vector<std::shared_ptr<ImageView>> attachments,
                         std::tuple<int, int> renderArea,
                         std::shared_ptr<RenderPass> renderPass,
                         std::shared_ptr<Device> device) {
  _device = device;
  _resolution = renderArea;
  _renderPass = renderPass;
  _attachments = attachments;

  std::vector<VkImageView> input;
  for (int i = 0; i < attachments.size(); i++) {
    input.push_back(attachments[i]->getImageView());
  }
  // depth and image must have equal resolution
  VkFramebufferCreateInfo framebufferInfo{.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                                          .renderPass = renderPass->getRenderPass(),
                                          .attachmentCount = static_cast<uint32_t>(input.size()),
                                          .pAttachments = input.data(),
                                          .width = static_cast<uint32_t>(std::get<0>(renderArea)),
                                          .height = static_cast<uint32_t>(std::get<1>(renderArea)),
                                          .layers = 1};

  if (vkCreateFramebuffer(_device->getLogicalDevice(), &framebufferInfo, nullptr, &_buffer) != VK_SUCCESS) {
    throw std::runtime_error("failed to create framebuffer!");
  }
}

std::vector<std::shared_ptr<ImageView>> Framebuffer::getAttachments() { return _attachments; }

std::tuple<int, int> Framebuffer::getResolution() { return _resolution; }

VkFramebuffer Framebuffer::getBuffer() { return _buffer; }

std::shared_ptr<RenderPass> Framebuffer::getRenderPass() { return _renderPass; }

Framebuffer::~Framebuffer() { vkDestroyFramebuffer(_device->getLogicalDevice(), _buffer, nullptr); }