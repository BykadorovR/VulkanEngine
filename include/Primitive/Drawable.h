#pragma once
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <string>
#include "Graphic/Light.h"

import Command;
#undef OPAQUE
#undef TRANSPARENT

enum class AlphaType { TRANSPARENT, OPAQUE };

class Named {
 private:
  std::string _name;

 public:
  void setName(std::string name);
  std::string getName();
};

struct BufferMVP {
  glm::mat4 model;
  glm::mat4 view;
  glm::mat4 projection;
};

class Transformable : virtual public Named {
 protected:
  glm::vec3 _originShift = glm::vec3{0.f, 0.f, 0.f};
  glm::quat _originRotate = glm::identity<glm::quat>();
  glm::vec3 _translate = glm::vec3{0.f, 0.f, 0.f};
  glm::quat _rotate = glm::identity<glm::quat>();
  glm::vec3 _scale = glm::vec3{1.f, 1.f, 1.f};

 public:
  void setOriginShift(glm::vec3 originShift);
  void setOriginRotate(glm::quat rotate);
  void setTranslate(glm::vec3 translate);
  void setRotate(glm::quat rotate);
  void setScale(glm::vec3 scale);
  glm::vec3 getOriginShift();
  glm::vec3 getTranslate();
  glm::quat getRotate();
  glm::vec3 getScale();
  glm::mat4 getModel();
};

class Drawable : public Transformable {
 public:
  virtual void draw(const RenderGraph::CommandBuffer* commandBuffer) = 0;
  virtual ~Drawable() = default;
};

// Such objects are being rendered on the shadow map, it doesn't mean that such objects will be shadowed.
// It means that such objects can cast a shadow.
class Shadowable : virtual public Named {
 public:
  virtual void drawShadow(LightType lightType,
                          int lightIndex,
                          int face,
                          const RenderGraph::CommandBuffer* commandBuffer) = 0;
};
