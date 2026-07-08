#pragma once

#include "include/components.h"
#include "include/graphics_pipeline.h"
#include "include/image.h"
#include "include/model.h"
#include <SDL3/SDL_gpu.h>
#include <memory>

struct RenderRequest {
  std::shared_ptr<Model> model;
  WorldTransformComponent transform;
};

class Renderer {
public:
  explicit Renderer(SDL_GPUDevice *device, SDL_Window *window,
                    std::shared_ptr<TextureManager> texture_manager);
  ~Renderer();
  bool init();
  void render(std::vector<RenderRequest> &render_request,
              SceneLightBuffer lights, CameraComponent camera,
              CameraComponent shadow_cam);
  int render_width() const { return standard_pipeline.render_width(); };
  int render_height() const { return standard_pipeline.render_height(); };
  SDL_GPUTexture *render_target() const {
    return standard_pipeline.render_target();
  };
  SDL_GPUTexture *shadow_map() const {
    return shadow_pipeline.get_shadow_map();
  }
  SDL_GPUTexture *fft_source() const { return fft_input->texture; }
  SDL_GPUTexture *compute_target() const { return compute_target_; }
  SDL_GPUTexture *mask_map() const { return mask_pipeline.get_mask(); }
  float shadow_map_aspect() const {
    return shadow_pipeline.shadow_map_aspect();
  }
  glm::mat4 proj_view() const { return standard_pipeline.proj_view(); }

private:
  // Compute pipelines are not currently a part of the pipeline infrastructure
  bool create_compute_pipeline();
  // textures
  bool create_compute_target();

  void run_compute_pass(int k, SDL_GPUCommandBuffer *cmd);
  ShadowPipeline shadow_pipeline;
  StandardPipeline standard_pipeline;
  MaskPipeline mask_pipeline;

  std::shared_ptr<TextureManager> texture_manager_;

  SDL_GPUDevice *device_;
  SDL_Window *window_;

  SDL_GPUComputePipeline *compute_pipeline_ = nullptr;
  SDL_GPUTexture *compute_target_ = nullptr;
  std::unique_ptr<Image> fft_input;
  SDL_GPUTexture *jfa_source = nullptr;
};
