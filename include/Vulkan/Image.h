#pragma once
#include "Utility/EngineState.h"
#include "Vulkan/Buffer.h"
#include <tuple>

class Image {
 private:
  std::shared_ptr<EngineState> _engineState;
  std::tuple<int, int> _resolution;
  VkImage _image;
  VmaAllocation _imageMemory;
  VkFormat _format;
  int _layers;
  bool _external = false;
  VkImageLayout _imageLayout;
  std::shared_ptr<Buffer> _stagingBuffer;

 public:
  Image(VkImage& image, std::tuple<int, int> resolution, VkFormat format, std::shared_ptr<EngineState> engineState);
  Image(std::tuple<int, int> resolution,
        int layers,
        int mipMapLevels,
        VkFormat format,
        VkImageTiling tiling,
        VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties,
        std::shared_ptr<EngineState> engineState);

  void setData(std::shared_ptr<Buffer> buffer);
  // bufferOffsets contains offsets for part of buffer that should be copied to corresponding layers of image
  void copyFrom(std::shared_ptr<Buffer> buffer,
                std::vector<int> bufferOffsets,
                std::shared_ptr<CommandBuffer> commandBufferTransfer);
  void changeLayout(VkImageLayout oldLayout,
                    VkImageLayout newLayout,
                    VkImageAspectFlags aspectMask,
                    int layersNumber,
                    int mipMapLevels,
                    std::shared_ptr<CommandBuffer> commandBufferTransfer);
  void generateMipmaps(int mipMapLevels, int layers, std::shared_ptr<CommandBuffer> commandBuffer);

  void overrideLayout(VkImageLayout layout);
  std::tuple<int, int> getResolution();
  VkImage& getImage();
  VkFormat& getFormat();
  VkImageLayout& getImageLayout();
  int getLayersNumber();

  ~Image();
};

class ImageView {
 private:
  std::shared_ptr<Image> _image;
  VkImageView _imageView;
  std::shared_ptr<EngineState> _engineState;

 public:
  // baseArrayLayer - which face is used
  // arrayLayerNumber - number of faces
  // baseMipMapLevel - which mip map level is used
  // mipMapLevels - number of visible mip maps for current image view
  ImageView(std::shared_ptr<Image> image,
            VkImageViewType type,
            int baseArrayLayer,
            int arrayLayerNumber,
            int baseMipMap,
            int mipMapNumber,
            VkImageAspectFlags aspectFlags,
            std::shared_ptr<EngineState> engineState);
  ImageView(std::shared_ptr<Image> image, VkImageView& imageView, std::shared_ptr<EngineState> engineState);
  VkImageView& getImageView();
  std::shared_ptr<Image> getImage();

  ~ImageView();
};

class Framebuffer {
 private:
  std::shared_ptr<Device> _device;
  std::shared_ptr<RenderPass> _renderPass;
  VkFramebuffer _buffer;
  std::tuple<int, int> _resolution;
  std::vector<std::shared_ptr<ImageView>> _attachments;

 public:
  Framebuffer(std::vector<std::shared_ptr<ImageView>> attachments,
              std::tuple<int, int> renderArea,
              std::shared_ptr<RenderPass> renderPass,
              std::shared_ptr<Device> device);

  std::vector<std::shared_ptr<ImageView>> getAttachments();
  std::tuple<int, int> getResolution();
  VkFramebuffer getBuffer();
  std::shared_ptr<RenderPass> getRenderPass();
  ~Framebuffer();
};