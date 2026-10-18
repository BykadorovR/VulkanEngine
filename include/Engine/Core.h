#pragma once
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>
#include "BS_thread_pool.hpp"
#include "Utility/EngineState.h"
#include "Utility/GameState.h"
#include "Utility/Settings.h"
#include "Utility/Logger.h"
#include "Utility/GUI.h"
#include "Utility/Timer.h"
#include "Utility/ResourceManager.h"
#include "Utility/Animation.h"
#include "Graphic/Postprocessing.h"
#include "Graphic/Camera.h"
#include "Graphic/LightManager.h"
#include "Graphic/IBL.h"
#include "Graphic/Blur.h"
#include "Primitive/ParticleSystem.h"
#include "Primitive/Terrain.h"
#include "Primitive/Skybox.h"
#include "Primitive/Shape3D.h"
#include "Primitive/Sprite.h"
#include "Primitive/Line.h"
#include "Primitive/Model.h"
#include "Primitive/Equirectangular.h"

import Buffer;
import Command;
import CommandPool;
import Graph;
import Instrumentation;
import Swapchain;
import Texture;

class Core {
 private:
#ifdef __ANDROID__
  AAssetManager* _assetManager = nullptr;
  ANativeWindow* _nativeWindow = nullptr;
#endif
  std::shared_ptr<EngineState> _engineState;
  std::shared_ptr<GameState> _gameState;
  std::unique_ptr<RenderGraph::Swapchain> _renderGraphSwapchain;
  std::unique_ptr<RenderGraph::Tracer> _tracer;
  std::unique_ptr<RenderGraph::Graph> _renderGraph;
  std::unique_ptr<RenderGraph::CommandPool> _applicationCommandPool;
  std::vector<std::unique_ptr<RenderGraph::CommandBuffer>> _applicationCommandBuffers;
  std::vector<VkFence> _frameFences;
  std::vector<std::shared_ptr<RenderGraph::ImageView>> _graphicImageViews;

  std::set<std::shared_ptr<Material>> _materials;
  std::shared_ptr<GUI> _gui;

  std::shared_ptr<Timer> _timer;
  std::shared_ptr<TimerFPS> _timerFPSReal;
  std::shared_ptr<TimerFPS> _timerFPSLimited;

  std::map<AlphaType, std::vector<std::shared_ptr<Drawable>>> _drawables;
  std::map<int, std::vector<std::shared_ptr<Drawable>>> _deferredDrawables;
  std::map<int, std::vector<std::shared_ptr<Shadowable>>> _deferredShadowables;
  std::vector<std::shared_ptr<Shadowable>> _shadowables;
  std::vector<std::shared_ptr<Animation>> _animations;
  std::map<std::shared_ptr<Animation>, std::future<void>> _futureAnimationUpdate;

  std::vector<std::shared_ptr<ParticleSystemGraphic>> _particleSystems;
  std::shared_ptr<Postprocessing> _postprocessing;
  std::shared_ptr<Skybox> _skybox;
  std::map<std::shared_ptr<DirectionalShadowResources>,
           std::vector<std::pair<std::shared_ptr<BlurSeparate>, std::shared_ptr<BlurSeparate>>>>
      _directionalShadowBlurPasses;
  std::map<
      std::shared_ptr<PointShadowResources>,
      std::vector<std::pair<std::vector<std::shared_ptr<BlurSeparate>>, std::vector<std::shared_ptr<BlurSeparate>>>>>
      _pointShadowBlurPasses;
  std::map<std::shared_ptr<PointShadowResources>,
           std::vector<std::pair<std::vector<std::shared_ptr<RenderGraph::Texture>>,
                                 std::vector<std::shared_ptr<RenderGraph::Texture>>>>>
      _pointShadowBlurTextures;

  std::shared_ptr<BS::thread_pool> _threadPool;
  std::function<void(const RenderGraph::CommandBuffer* commandBuffer)> _updateCallback;
  std::function<void(int width, int height)> _resetCallback;
  bool _renderGraphDirty = true;

  void _buildRenderGraph();
  void _resetRenderGraph();
  void _initializeFrameResources();
  void _addRenderGraphFlightImages(std::string_view name,
                                   const std::vector<const RenderGraph::Image*>& images,
                                   VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                   int layer = 0);

 public:
  Core(std::shared_ptr<Settings> settings);
  ~Core();
#ifdef __ANDROID__
  void setAssetManager(AAssetManager* assetManager);
  void setNativeWindow(ANativeWindow* window);
#endif
  void initialize();
  void draw();
  void registerUpdate(std::function<void(const RenderGraph::CommandBuffer*)> update);
  void registerReset(std::function<void(int, int)> reset);

  void setCamera(std::shared_ptr<Camera> camera);
  void addDrawable(std::shared_ptr<Drawable> drawable, AlphaType type = AlphaType::TRANSPARENT);
  void addShadowable(std::shared_ptr<Shadowable> shadowable);
  void addSkybox(std::shared_ptr<Skybox> skybox);
  void removeDrawable(std::shared_ptr<Drawable> drawable);
  void removeShadowable(std::shared_ptr<Shadowable> shadowable);

  std::shared_ptr<ImageCPU<uint8_t>> loadImageCPU(std::string path);
  std::shared_ptr<ImageDataBuffer> loadImageGPU(std::shared_ptr<ImageCPU<uint8_t>> imageCPU);
  std::shared_ptr<RenderGraph::Texture> createTexture(std::string path, VkFormat format, int mipMapLevels);
  std::shared_ptr<Cubemap> createCubemap(std::vector<std::string> paths, VkFormat format, int mipMapLevels);
  std::shared_ptr<ModelGLTF> createModelGLTF(std::string path);
  std::shared_ptr<Animation> createAnimation(std::shared_ptr<ModelGLTF> modelGLTF);
  std::shared_ptr<Equirectangular> createEquirectangular(std::string path);
  std::shared_ptr<MaterialColor> createMaterialColor(MaterialTarget target);
  std::shared_ptr<MaterialPhong> createMaterialPhong(MaterialTarget target);
  std::shared_ptr<MaterialPBR> createMaterialPBR(MaterialTarget target);
  std::shared_ptr<Shape3D> createShape3D(ShapeType shapeType,
                                         std::shared_ptr<Mesh3D> mesh,
                                         VkCullModeFlagBits cullMode = VK_CULL_MODE_BACK_BIT);
  std::shared_ptr<Model3D> createModel3D(std::shared_ptr<ModelGLTF> modelGLTF);
  std::shared_ptr<Sprite> createSprite();
  std::shared_ptr<TerrainGPU> createTerrainComposition(std::shared_ptr<ImageCPU<uint8_t>> heightmap);
  std::shared_ptr<TerrainCPU> createTerrainCPU(std::vector<float> heights, std::tuple<int, int> resolution);
  std::shared_ptr<TerrainCPU> createTerrainCPU(std::shared_ptr<ImageCPU<uint8_t>> heightmap);
  std::shared_ptr<Line> createLine();
  std::shared_ptr<IBL> createIBL();
  std::shared_ptr<ParticleSystemGraphic> createParticleSystem(std::vector<Particle> particles,
                                                              std::shared_ptr<RenderGraph::Texture> particleTexture);
  std::shared_ptr<Skybox> createSkybox();
  std::shared_ptr<PointLight> createPointLight();
  std::shared_ptr<DirectionalLight> createDirectionalLight();
  std::shared_ptr<AmbientLight> createAmbientLight();
  std::shared_ptr<PointShadowResources> createPointShadow(std::shared_ptr<PointLight> pointLight, bool blur = false);
  std::shared_ptr<DirectionalShadowResources> createDirectionalShadow(
      std::shared_ptr<DirectionalLight> directionalLight,
      bool blur = true);
  std::shared_ptr<PhysicsManager> createPhysicsManager();
  std::shared_ptr<GUI> createGUI();
  std::shared_ptr<Postprocessing> createPostprocessing();

  const RenderGraph::CommandBuffer* getCommandBufferApplication();
  std::shared_ptr<ResourceManager> getResourceManager();
  const std::vector<std::shared_ptr<Drawable>>& getDrawables(AlphaType type);
  const std::vector<std::shared_ptr<PointLight>>& getPointLights();
  const std::vector<std::shared_ptr<DirectionalLight>>& getDirectionalLights();
  const std::vector<std::shared_ptr<PointShadowResources>>& getPointShadows();
  const std::vector<std::shared_ptr<DirectionalShadowResources>>& getDirectionalShadows();
  std::shared_ptr<Postprocessing> getPostprocessing();
  std::shared_ptr<GUI> getGUI();

  std::shared_ptr<EngineState> getEngineState();
  std::shared_ptr<GameState> getGameState();
  std::shared_ptr<Camera> getCamera();
  std::tuple<int, int> getFPS();
};
