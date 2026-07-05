#pragma once

#include "include/components.h"
#include "include/gpu_utils.h"
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

enum class DrawAttributes { Opaque, Transparent };

class Pipeline {
public:
  explicit Pipeline(SDL_GPUDevice *device, SDL_Window *window,
                    std::shared_ptr<TextureManager> texture_manager,
                    std::string name);
  ~Pipeline();

  void init();
  void deinit();

  virtual void begin_pass() = 0;
  void end_pass();
  virtual const void draw_model(std::shared_ptr<Model> model,
                                const WorldTransformComponent &transform,
                                DrawAttributes draw_attributes,
                                SDL_GPUCommandBuffer *cmd_) = 0;

private:
  // boilerplate
  SDL_GPUShader *load_shader(std::filesystem::path path,
                             SDL_GPUShaderStage stage,
                             Uint32 uniform_buffers = 0,
                             Uint32 samplers = 0) const;
  SDL_GPUTextureFormat get_supported_depth_format();
  SDL_GPUSampler *create_sampler(SDL_GPUSamplerCreateInfo *info);

  // resource "creators"
  virtual bool create_pipeline() = 0;
  virtual bool create_textures() = 0;
  virtual bool create_samplers() = 0;

  virtual bool destroy_textures() = 0;
  virtual bool destroy_samplers() = 0;

  std::string name;
  std::shared_ptr<TextureManager> texture_manager_;

  SDL_GPURenderPass *pass_ = nullptr;
  SDL_GPUGraphicsPipeline *pipeline_ = nullptr;
};
