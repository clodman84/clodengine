#include "include/animations.h"

#include <SDL3/SDL.h>
#include <common/TracyProtocol.hpp>
#include <entt/entt.hpp>
#include <glm/ext/vector_float3.hpp>

template <>
glm::vec3 interpolate(glm::vec3 previous, glm::vec3 next, float alpha) {
  return glm::mix(previous, next, alpha);
}

template <>
glm::quat interpolate(glm::quat previous, glm::quat next, float alpha) {
  return glm::slerp(previous, next, alpha);
}

template <typename T> inline T make_value(const float *v);
template <> inline glm::vec3 make_value(const float *v) {
  return {v[0], v[1], v[2]};
}
template <> inline glm::quat make_value(const float *v) {
  return {v[3], v[0], v[1], v[2]};
}

template <typename T>
static AnimationSampler<T>
sampler_from_gltf_impl(cgltf_animation_sampler &animation_sampler) {
  AnimationSampler<T> out{};

  switch (animation_sampler.interpolation) {
  case cgltf_interpolation_type_step:
    out.interpolation_type = AnimationInterpolation::Step;
    break;
  case cgltf_interpolation_type_cubic_spline:
    out.interpolation_type = AnimationInterpolation::CubicSpline;
    break;
  case cgltf_interpolation_type_linear:
  default:
    out.interpolation_type = AnimationInterpolation::Linear;
    break;
  }

  const cgltf_size components =
      cgltf_num_components(animation_sampler.output->type);
  const cgltf_size values_per_key =
      out.interpolation_type == AnimationInterpolation::CubicSpline ? 3 : 1;

  const cgltf_size output_float_count =
      cgltf_accessor_unpack_floats(animation_sampler.output, nullptr, 0);
  std::vector<float> output(output_float_count);
  if (output_float_count) {
    cgltf_accessor_unpack_floats(animation_sampler.output, output.data(),
                                 output_float_count);
  }

  const cgltf_size input_float_count =
      cgltf_accessor_unpack_floats(animation_sampler.input, nullptr, 0);
  out.timestamps.resize(input_float_count);
  if (input_float_count) {
    cgltf_accessor_unpack_floats(animation_sampler.input, out.timestamps.data(),
                                 input_float_count);
  }

  const cgltf_size key_count =
      components ? (output_float_count / (components * values_per_key)) : 0;

  out.values.resize(key_count);

  if (out.interpolation_type == AnimationInterpolation::CubicSpline) {
    out.in_tangent.resize(key_count);
    out.out_tangent.resize(key_count);

    for (cgltf_size i = 0; i < key_count; ++i) {
      const float *base = output.data() + i * components * 3;
      out.in_tangent[i] = make_value<T>(base);
      out.values[i] = make_value<T>(base + components);
      out.out_tangent[i] = make_value<T>(base + components * 2);
    }
  } else {
    for (cgltf_size i = 0; i < key_count; ++i) {
      out.values[i] = make_value<T>(output.data() + i * components);
    }
  }

  return out;
}

std::variant<AnimationSampler<glm::vec3>, AnimationSampler<glm::quat>>
sampler_from_gltf(cgltf_animation_sampler &animation_sampler) {
  if (animation_sampler.output &&
      animation_sampler.output->type == cgltf_type_vec4) {
    return sampler_from_gltf_impl<glm::quat>(animation_sampler);
  }
  return sampler_from_gltf_impl<glm::vec3>(animation_sampler);
}

AnimationChannel AnimationChannel::from_gltf(
    cgltf_animation_channel &animation_channel,
    std::map<const cgltf_node *, entt::entity> &node_entity_map) {
  AnimationChannel out{};
  out.target = node_entity_map[animation_channel.target_node];
  out.path = AnimationPath::Translation;

  switch (animation_channel.target_path) {
  case cgltf_animation_path_type_rotation:
    out.path = AnimationPath::Rotation;
    break;
  case cgltf_animation_path_type_scale:
    out.path = AnimationPath::Scale;
    break;
  case cgltf_animation_path_type_translation:
  default:
    out.path = AnimationPath::Translation;
    break;
  }

  return out;
}

Animation Animation::from_gltf(
    cgltf_animation &animation_data,
    std::map<const cgltf_node *, entt::entity> &node_entity_map) {
  Animation out{};
  out.name = animation_data.name ? animation_data.name : "";
  out.time = 0.0f;
  SDL_Log("[Animation] loading animation from gLTF : %s", out.name.c_str());

  out.samplers.reserve(animation_data.samplers_count);
  for (cgltf_size i = 0; i < animation_data.samplers_count; ++i) {
    out.samplers.emplace_back(sampler_from_gltf(animation_data.samplers[i]));
  }

  out.channels.reserve(animation_data.channels_count);
  for (cgltf_size i = 0; i < animation_data.channels_count; ++i) {
    AnimationChannel channel = AnimationChannel{}.from_gltf(
        animation_data.channels[i], node_entity_map);

    if (animation_data.channels[i].sampler) {
      channel.sampler_index = cgltf_animation_sampler_index(
          &animation_data, animation_data.channels[i].sampler);
    }
    out.channels.push_back(channel);
  }

  for (const auto &s : out.samplers) {
    float s_dur = std::visit(
        [](const auto &s) {
          return s.timestamps.empty() ? 0.f : s.timestamps.back();
        },
        s);
    out.duration = std::max(out.duration, s_dur);
  }

  SDL_Log("[Animation] %ld samplers and %ld channels loaded from gLTF. "
          "Animation Duration: %f",
          out.samplers.size(), out.channels.size(), out.duration);
  return out;
}
