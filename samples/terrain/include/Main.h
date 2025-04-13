#pragma once
#include "Engine/Core.h"
#include "Primitive/Shape3D.h"
#include "Primitive/Terrain.h"

class InputHandler : public InputSubscriber {
 private:
  std::shared_ptr<Core> _core;

 public:
  InputHandler(std::shared_ptr<Core> core);
  void cursorNotify(float xPos, float yPos) override;
  void mouseNotify(int button, int action, int mods) override;
  void keyNotify(int key, int scancode, int action, int mods) override;
  void charNotify(unsigned int code) override;
  void scrollNotify(double xOffset, double yOffset) override;
};

class Main {
 private:
  std::shared_ptr<Core> _core;
  std::shared_ptr<GUI> _gui;
  std::shared_ptr<CameraFly> _camera;
  std::shared_ptr<InputHandler> _inputHandler;
  std::shared_ptr<MaterialColor> _materialColor;
  std::shared_ptr<MaterialPhong> _materialPhong;
  std::shared_ptr<MaterialPBR> _materialPBR;

  std::shared_ptr<PointLight> _pointLightVertical, _pointLightHorizontal;
  std::shared_ptr<DirectionalLight> _directionalLight;
  std::shared_ptr<Shape3D> _cubeColoredLightVertical, _cubeColoredLightHorizontal;
  std::shared_ptr<Shape3D> _sphereClickDebug;
  std::shared_ptr<TerrainGPU> _terrain;
  std::shared_ptr<TerrainDebug> _terrainDebug;
  std::shared_ptr<TerrainCPU> _terrainCPU;
  bool _showDebug = false, _showTerrain = true;
  std::shared_ptr<PhysicsManager> _physicsManager;
  std::shared_ptr<TerrainPhysics> _terrainPhysics;
  bool _showLoD = false, _showWireframe = false, _showNormals = false, _showPatches = false;
  int _typeIndex = 1;
  int _interpolationIndex = 0;
  char _terrainPath[256] = "";
  int _patchX = 64, _patchY = 64;
  float _heightScale = 64.f;
  float _heightShift = 16.f;
  std::array<float, 4> _heightLevels = {16, 128, 192, 256};
  int _minTessellationLevel = 4, _maxTessellationLevel = 32;
  float _minDistance = 30, _maxDistance = 100;
  glm::vec3 _terrainPosition = glm::vec3(0.f, 0.f, 0.f);
  glm::vec3 _terrainPositionDebug = glm::vec3(0.f, 0.f, 0.f);
  glm::vec3 _terrainScale = glm::vec3(0.1f, 0.1f, 0.1f);
  std::optional<float> _stripeLeft, _stripeRight, _stripeTop, _stripeBot;
  std::vector<int> _patchTextures;
  std::vector<int> _patchRotationsIndex;

  enum class InrepolationMode { INTERPOLATION, COMPOSITION } _interpolationMode = InrepolationMode::INTERPOLATION;
  void _loadTerrain(std::string path);
  void _createTerrainColor(std::string path);
  void _createTerrainPhong(std::string path);
  void _createTerrainPBR(std::string path);
  void _createTerrainDebug(std::string path);

 public:
  Main();
  void update(std::shared_ptr<CommandBuffer> commandBuffer);
  void reset(int width, int height);
  void start();
};