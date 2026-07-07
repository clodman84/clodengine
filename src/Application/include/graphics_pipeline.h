#pragma once

#include "include/components.h"
#include "include/gpu_utils.h"
#include <SDL3/SDL_gpu.h>
#include <glm/ext/matrix_float4x4.hpp>
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

struct ScreenDimensions {
  glm::vec2 dimensions;
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

enum class DrawPass { OPAQUE, TRANSPARENT };

class Pipeline {
public:
  explicit Pipeline(SDL_GPUDevice *device, SDL_Window *window,
                    std::shared_ptr<TextureManager> texture_manager);
  virtual ~Pipeline();
  bool init();

  virtual void begin_pass(SDL_GPUCommandBuffer *cmd_) = 0;
  void end_pass();
  virtual const void draw_model(std::shared_ptr<Model> model,
                                const WorldTransformComponent &transform,
                                SDL_GPUCommandBuffer *cmd_, DrawPass pass) = 0;

protected:
  // boilerplate
  SDL_GPUShader *load_shader(std::filesystem::path path,
                             SDL_GPUShaderStage stage,
                             Uint32 uniform_buffers = 0,
                             Uint32 samplers = 0) const;
  SDL_GPUTextureFormat get_supported_depth_format();
  SDL_GPUTextureFormat depth_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;

  std::shared_ptr<TextureManager> texture_manager_;

  SDL_GPUDevice *device_;
  SDL_Window *window_;

  SDL_GPURenderPass *pass_ = nullptr;
  SDL_GPUGraphicsPipeline *pipeline_ = nullptr;

private:
  // resource "creators"
  virtual bool create_pipeline() = 0;
  virtual bool create_textures() = 0;
  virtual bool create_samplers() = 0;

  virtual bool destroy_textures() = 0;
  virtual bool destroy_samplers() = 0;

  std::string name;
};

class StandardPipeline : public Pipeline {
public:
  StandardPipeline(SDL_GPUDevice *device, SDL_Window *window,
                   std::shared_ptr<TextureManager> texture_manager)
      : Pipeline(device, window, texture_manager) {};
  ~StandardPipeline() {
    destroy_samplers();
    destroy_textures();
  };
  void begin_pass(SDL_GPUCommandBuffer *cmd_) override;

  void set_shadow_map(SDL_GPUTexture *map) { shadow_map_ = map; }
  void set_light_space_proj_view(glm::mat4 proj, glm::mat4 view) {
    light_space_proj_view = proj * view;
  }
  void set_scene_lights(const SceneLightBuffer &lights) {
    light_buffer_ = lights;
  }

  const void draw_model(std::shared_ptr<Model> model,
                        const WorldTransformComponent &transform,
                        SDL_GPUCommandBuffer *cmd_, DrawPass pass) override;

  int render_width() const { return render_width_; };
  int render_height() const { return render_height_; };
  SDL_GPUTexture *render_target() const { return render_target_; };
  void set_view(const glm::mat4 &v) { view_ = v; }
  void set_projection(const glm::mat4 &p) { proj_ = p; }
  glm::mat4 proj_view() const { return proj_ * view_; }

private:
  bool create_pipeline() override;
  bool create_textures() override;
  bool create_samplers() override;

  bool destroy_textures() override;
  bool destroy_samplers() override;

  UniformBuffer uniform_{};
  SceneLightBuffer light_buffer_{};

  // the shadow map is not made by the StandardPipeline, how do I enforce this?
  // just let it be like this, and assign a setter for now, ideally, the
  // texture_manager_ would have to step in at some point
  SDL_GPUTexture *shadow_map_ = nullptr;

  SDL_GPUTexture *render_target_ = nullptr;
  SDL_GPUTexture *gray_texture_ = nullptr;
  SDL_GPUTexture *depth_target_ = nullptr;

  SDL_GPUSampler *sampler_ = nullptr;
  SDL_GPUSampler *shadow_sampler_ = nullptr;

  glm::mat4 proj_;
  glm::mat4 view_;
  glm::mat4 light_space_proj_view;

  std::string name = "Standard Pipeline";

  static constexpr int render_width_ = 1920;
  static constexpr int render_height_ = 1080;
};

class ShadowPipeline : public Pipeline {
public:
  ShadowPipeline(SDL_GPUDevice *device, SDL_Window *window,
                 std::shared_ptr<TextureManager> texture_manager)
      : Pipeline(device, window, texture_manager) {};
  ~ShadowPipeline() {
    destroy_samplers();
    destroy_textures();
  };
  void begin_pass(SDL_GPUCommandBuffer *cmd_) override;
  SDL_GPUTexture *get_shadow_map() const { return shadow_map_; }
  const void draw_model(std::shared_ptr<Model> model,
                        const WorldTransformComponent &transform,
                        SDL_GPUCommandBuffer *cmd_, DrawPass pass) override;
  float shadow_map_aspect() const {
    return (float)shadow_map_width / shadow_map_height;
  };
  void set_view(const glm::mat4 &v) { view_ = v; }
  void set_projection(const glm::mat4 &p) { proj_ = p; }

private:
  bool create_pipeline() override;
  bool create_textures() override;
  bool create_samplers() override;

  bool destroy_textures() override;
  bool destroy_samplers() override;
  ShadowUniform shadow_uniform_{};
  SDL_GPUTexture *shadow_map_ = nullptr;

  glm::mat4 proj_;
  glm::mat4 view_;

  std::string name = "Shadow Pipeline";

  static constexpr int shadow_map_width = 2048;
  static constexpr int shadow_map_height = 2048;
};

class MaskPipeline : public Pipeline {
public:
  // I am not sure if this is the optimal way to generate stencils? but okay I
  // guess, for JFA I need a regular ass texture that's just a mask of where the
  // object is
  MaskPipeline(SDL_GPUDevice *device, SDL_Window *window,
               std::shared_ptr<TextureManager> texture_manager)
      : Pipeline(device, window, texture_manager) {};
  ~MaskPipeline() {
    destroy_samplers();
    destroy_textures();
  };
  void begin_pass(SDL_GPUCommandBuffer *cmd_) override;
  SDL_GPUTexture *get_mask() const { return mask_map_; }
  const void draw_model(std::shared_ptr<Model> model,
                        const WorldTransformComponent &transform,
                        SDL_GPUCommandBuffer *cmd_, DrawPass pass) override;
  float mask_map_aspect() const { return (float)mask_width / mask_height; };
  void set_view(const glm::mat4 &v) { view_ = v; }
  void set_projection(const glm::mat4 &p) { proj_ = p; }

private:
  bool create_pipeline() override;
  bool create_textures() override;
  bool create_samplers() override;

  bool destroy_textures() override;
  bool destroy_samplers() override;
  ShadowUniform mask_uniform_{};
  SDL_GPUTexture *mask_map_ = nullptr;

  glm::mat4 proj_;
  glm::mat4 view_;

  std::string name = "Mask Pipeline";

  static constexpr int mask_width = 1920;
  static constexpr int mask_height = 1080;
  ScreenDimensions screen_dimensions{{1920, 1080}};
};

// class CompositePipeline : public Pipeline {};
