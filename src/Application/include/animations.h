#pragma once

#include <entt/entity/fwd.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/fwd.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "include/cgltf.h"
#include <algorithm>
#include <map>
#include <variant>
#include <vector>

enum class AnimationInterpolation { Linear, Step, CubicSpline };
enum class AnimationPath { Translation, Rotation, Scale };

template <typename T> T interpolate(T previous, T next, float alpha);

template <typename T> struct AnimationSampler {
  std::vector<float> timestamps;

  std::vector<T> values;
  std::vector<T> in_tangent;
  std::vector<T> out_tangent;

  AnimationInterpolation interpolation_type;

  T sample(float t) {
    if (values.size() == 1)
      return values[0];

    size_t upper_bound_idx = std::distance(
        timestamps.begin(),
        std::upper_bound(timestamps.begin(), timestamps.end(), t));

    // clamp so upper never goes past the last valid index
    upper_bound_idx = std::min(upper_bound_idx, timestamps.size() - 1);
    size_t lower_bound_idx = upper_bound_idx > 0 ? upper_bound_idx - 1 : 0;

    T previous_value = values[lower_bound_idx];
    T next_value = values[upper_bound_idx];
    float previous_time = timestamps[lower_bound_idx];
    float next_time = timestamps[upper_bound_idx];
    float delta_time = next_time - previous_time;
    float alpha = (delta_time > 1e-6f) ? (t - previous_time) / delta_time : 0.f;
    alpha = std::clamp(alpha, 0.f, 1.f);

    switch (interpolation_type) {
    case AnimationInterpolation::Step:
      return previous_value;
    case AnimationInterpolation::Linear:
      return interpolate(previous_value, next_value, alpha);
    case AnimationInterpolation::CubicSpline: {
      T previous_tangent = delta_time * out_tangent[lower_bound_idx];
      T next_tangent = delta_time * in_tangent[upper_bound_idx];
      float t = alpha, t2 = t * t, t3 = t2 * t;
      return (2 * t3 - 3 * t2 + 1) * previous_value +
             (t3 - 2 * t2 + t) * previous_tangent +
             (-2 * t3 + 3 * t2) * next_value + (t3 - t2) * next_tangent;
    }
    }
    return previous_value;
  };
};

struct AnimationChannel {
  size_t sampler_index;
  entt::entity target;
  AnimationPath path;
  AnimationChannel
  from_gltf(cgltf_animation_channel &animation_channel,
            std::map<const cgltf_node *, entt::entity> &node_entity_map);
};

struct Animation {
  std::vector<AnimationChannel> channels;
  std::vector<
      std::variant<AnimationSampler<glm::vec3>, AnimationSampler<glm::quat>>>
      samplers;
  float time;
  float duration;
  Animation
  from_gltf(cgltf_animation &animation_data,
            std::map<const cgltf_node *, entt::entity> &node_entity_map);
  bool is_looping = true;
  bool is_paused = false;
  std::string name;
};
