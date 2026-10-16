#pragma once
#include "Engine/Core.h"
#include "DebugVisualization.h"

class InputHandler : public InputSubscriber {
 private:
  bool _cursorEnabled = false;
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
  std::shared_ptr<CameraFly> _camera;
  std::shared_ptr<GUI> _gui;
  std::shared_ptr<InputHandler> _inputHandler;
  std::shared_ptr<DebugVisualization> _debugVisualization;
  std::shared_ptr<PointLight> _pointLightHorizontal;
  std::shared_ptr<DirectionalLight> _directionalLight;
  std::shared_ptr<Cubemap> _cubemapSkybox;
  std::shared_ptr<IBL> _ibl;
  std::shared_ptr<Equirectangular> _equirectangular;
  std::shared_ptr<MaterialColor> _materialColor;
  std::shared_ptr<MaterialPhong> _materialPhong;
  std::shared_ptr<MaterialPBR> _materialPBR;
  std::shared_ptr<TerrainGPU> _terrain;
  int _patchX = 12, _patchY = 12;
  float _heightScale = 64.f;
  float _heightShift = 16.f;
  std::array<float, 4> _heightLevels = {16, 128, 192, 256};
  int _minTessellationLevel = 4, _maxTessellationLevel = 32;
  float _minDistance = 30, _maxDistance = 100;
  glm::vec3 _terrainPosition = glm::vec3(2.f, -6.f, 0.f);
  glm::vec3 _terrainScale = glm::vec3(0.1f, 0.1f, 0.1f);
  int _typeIndex = 0;
  void _createTerrainColor();
  void _createTerrainPhong();
  void _createTerrainPBR();

 public:
  Main();
  void update();
  void reset(int width, int height);
  void start();
};