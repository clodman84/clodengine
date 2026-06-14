#pragma once

#include "include/components.h"
#include "include/model.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <memory>

struct UniformBuffer {
  glm::mat4 proj_view;
  glm::mat4 light_space_proj_view;
  glm::mat4 model;
  glm::mat4 normal_matrix;
  glm::vec4 base_colour;

  float has_texture;
  float time;
  float _pad[2];
};

struct ShadowUniform {
  glm::mat4 proj_view;
  glm::mat4 model;
};

struct DirectionalLight {
  glm::vec4 direction;
  glm::vec4 colour;
};

struct SceneLightBuffer {
  int num_directional;
  int num_point;
  float ambient;
  float _pad;
  DirectionalLight directional_lights[4];
};

enum class DrawPass { Opaque, Transparent };

class Renderer {
public:
  explicit Renderer(SDL_GPUDevice *device, SDL_Window *window,
                    std::shared_ptr<TextureManager> texture_manager);
  ~Renderer();

  bool init();

  void begin_frame();
  void end_frame();

  void draw_model(std::shared_ptr<Model> model,
                  const WorldTransformComponent &transform, DrawPass pass);

  void begin_shadow_pass();
  void end_shadow_pass();
  void draw_model_shadow(std::shared_ptr<Model> model,
                         const WorldTransformComponent &transform);

  void set_view(const glm::mat4 &v) { view_ = v; }
  void set_projection(const glm::mat4 &p) { proj_ = p; }

  SDL_GPUTexture *render_target() const { return render_target_; }
  SDL_GPUTexture *shadow_map() const { return shadow_map_; }

  int render_width() const { return render_width_; }
  int render_height() const { return render_height_; }

  float shadow_aspect() const {
    return (float)shadow_map_width / shadow_map_height;
  }
  void set_scene_lights(const SceneLightBuffer &lights) {
    light_buffer_ = lights;
  }
  glm::mat4 proj_view() const { return proj_ * view_; }

private:
  SDL_GPUShader *load_shader(std::filesystem::path path,
                             SDL_GPUShaderStage stage,
                             Uint32 uniform_buffers = 0,
                             Uint32 samplers = 0) const;
  SDL_GPUTextureFormat get_supported_depth_format();

  bool create_pipeline();
  bool create_render_target();
  bool create_sampler();
  bool create_gray_texture();

  bool create_shadow_pipeline();
  bool create_shadow_map();
  bool create_shadow_sampler();

  std::shared_ptr<TextureManager> texture_manager_;

  SDL_GPUDevice *device_;
  SDL_Window *window_;

  SDL_GPUGraphicsPipeline *pipeline_ = nullptr;
  SDL_GPUGraphicsPipeline *shadow_pipeline_ = nullptr;

  SDL_GPUTexture *render_target_ = nullptr;
  SDL_GPUTexture *gray_texture_ = nullptr;
  SDL_GPUTexture *depth_target_ = nullptr;
  SDL_GPUTexture *shadow_map_ = nullptr;

  SDL_GPUSampler *sampler_ = nullptr;
  SDL_GPUSampler *shadow_sampler_ = nullptr;

  SDL_GPUCommandBuffer *cmd_ = nullptr;
  SDL_GPURenderPass *pass_ = nullptr;

  SDL_GPUTextureFormat depth_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;

  glm::mat4 proj_;
  glm::mat4 view_;

  UniformBuffer uniform_{};
  SceneLightBuffer light_buffer_{};
  ShadowUniform shadow_uniform_{};

  static constexpr int render_width_ = 1920;
  static constexpr int render_height_ = 1080;
  static constexpr int shadow_map_width = 2048;
  static constexpr int shadow_map_height = 2048;
};
