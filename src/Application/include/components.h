#pragma once

#include "include/animations.h"
#include "include/model.h"

#include <glm/trigonometric.hpp>
#include <memory>
#include <optional>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_common.hpp>
#include <glm/ext/quaternion_geometric.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include "entt/entt.hpp"

struct LocalTransformComponent {
  glm::vec3 position{0, 0, 0};
  glm::quat rotation{glm::identity<glm::quat>()};
  glm::vec3 scale{1, 1, 1};

  glm::mat4 matrix() const {
    glm::mat4 t = glm::translate(glm::mat4(1.f), position);
    glm::mat4 r = glm::toMat4(rotation);
    glm::mat4 s = glm::scale(glm::mat4(1.f), scale);
    return t * r * s;
  }

  void rotate(float angle, glm::vec3 axis,
              std::optional<glm::vec3> rotation_pivot = std::nullopt) {
    glm::quat delta = glm::angleAxis(angle, glm::normalize(axis));
    if (rotation_pivot) {
      glm::vec3 pivot = *rotation_pivot;
      rotation = glm::normalize(delta * rotation);
      position = pivot + glm::rotate(delta, position - pivot);
    } else {
      rotation = glm::normalize(delta * rotation);
    }
  }

  void look_at(glm::vec3 target, glm::vec3 up = {0, 1, 0}) {
    glm::vec3 dir = glm::normalize(target - position);
    rotation = glm::quatLookAt(dir, up);
  }

  LocalTransformComponent lerp(const LocalTransformComponent &other,
                               float t) const {
    return {glm::mix(position, other.position, t),
            glm::slerp(rotation, other.rotation, t),
            glm::mix(scale, other.scale, t)};
  }
};

struct SceneGraphRelationshipComponent {
  entt::entity parent{entt::null};
  entt::entity first_child{entt::null};
  entt::entity next_sibling{entt::null};
  entt::entity prev_sibling{entt::null};
};

struct RenderableComponent {
  std::shared_ptr<Model> model;
};

struct WorldTransformComponent {
  glm::mat4 matrix;
};

struct Anchor {};

struct AnimationComponent {
  std::vector<Animation> animations;
};

struct DirectionalLightComponent {
  glm::vec3 colour;
  float intensity;
  bool enabled;
};

enum class ProjectionType { ORTHO, PERSPECTIVE };

struct CameraComponent {
  glm::mat4 view;
  glm::mat4 projection;
  float fov{glm::radians(60.f)};
  float near_plane{20};
  float far_plane{1000};
  float ortho_size{800.f};
  ProjectionType type{ProjectionType::ORTHO};
};

// Camera + Player = Render
// Camera + Light = Shadows
struct Player {};
