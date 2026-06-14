#pragma once

#include <cmath>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_common.hpp>
#include <glm/ext/quaternion_geometric.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

struct Camera {
  glm::vec3 target{0, 0, 0};
  float distance{10.f};
  float yaw{glm::radians(45.f)};
  float pitch{glm::radians(35.264f)};

  float ortho_size{1.f};
  float near_plane{-100};
  float far_plane{100};

  glm::vec3 eye_position() const {
    return target + glm::vec3(distance * cos(pitch) * sin(yaw),
                              distance * sin(pitch),
                              distance * cos(pitch) * cos(yaw));
  }

  glm::mat4 view() const {
    return glm::lookAt(eye_position(), target, glm::vec3(0, 1, 0));
  }

  glm::mat4 projection(float aspect) const {
    const float h = ortho_size;
    const float w = h * aspect;
    return glm::ortho(-w, w, -h, h, near_plane, far_plane);
  }

  void pan(float dx, float dy) {
    glm::vec3 right =
        glm::normalize(glm::cross(eye_position() - target, glm::vec3(0, 1, 0)));
    glm::vec3 forward = glm::normalize(glm::cross(right, glm::vec3(0, 1, 0)));
    target += right * dx + forward * dy;
  }

  void zoom(float delta) { ortho_size = glm::max(ortho_size - delta, 0.01f); }

  glm::vec2 world_to_screen(glm::vec3 world_pos, float aspect) const {
    glm::vec4 clip = projection(aspect) * view() * glm::vec4(world_pos, 1.f);
    return glm::vec2(clip.x / clip.w, clip.y / clip.w); // NDC [ -1 ,
  }

  glm::vec3 screen_to_world_ray(glm::vec2 ndc, float aspect) const {
    glm::mat4 inv = glm::inverse(projection(aspect) * view());
    glm::vec4 near_point = inv * glm::vec4(ndc.x, ndc.y, -1.f, 1.f);
    glm::vec4 far_point = inv * glm::vec4(ndc.x, ndc.y, 1.f, 1.f);
    near_point /= near_point.w;
    far_point /= far_point.w;
    return glm::normalize(glm::vec3(far_point - near_point));
  }
};
