#include "include/renderer2.h"
#include "include/components.h"
#include "include/graphics_pipeline.h"
#include <SDL3/SDL_gpu.h>

NewRenderer::NewRenderer(SDL_GPUDevice *device, SDL_Window *window,
                         std::shared_ptr<TextureManager> texture_manager)
    : device_(device), window_(window), texture_manager_(texture_manager),
      shadow_pipeline(device, window, texture_manager),
      standard_pipeline(device, window, texture_manager) {
  SDL_Log("[Renderer] Renderer object created");
};

NewRenderer::~NewRenderer() {
  if (compute_pipeline_)
    SDL_ReleaseGPUComputePipeline(device_, compute_pipeline_);
  if (compute_target_)
    SDL_ReleaseGPUTexture(device_, compute_target_);
  SDL_Log("[Renderer] Renderer object destroyed");
}

bool NewRenderer::init() {
  if (!shadow_pipeline.init()) {
    return false;
  }
  if (!standard_pipeline.init()) {
    return false;
  }

  fft_input =
      std::make_unique<Image>("./Data/assets/fft_input.png", texture_manager_);
  fft_input->load_fullres();
  create_compute_pipeline();
  create_compute_target();
  return true;
}

void NewRenderer::render(std::vector<RenderRequest> &render_request,
                         SceneLightBuffer lights, CameraComponent camera,
                         CameraComponent shadow_cam) {

  cmd_ = SDL_AcquireGPUCommandBuffer(device_);
  shadow_pipeline.set_projection(shadow_cam.projection);
  shadow_pipeline.set_view(shadow_cam.view);

  shadow_pipeline.begin_pass(cmd_);
  for (auto &request : render_request) {
    shadow_pipeline.draw_model(request.model, request.transform, cmd_,
                               DrawPass::OPAQUE);
  }
  shadow_pipeline.end_pass();

  standard_pipeline.set_shadow_map(shadow_pipeline.get_shadow_map());
  standard_pipeline.set_projection(camera.projection);
  standard_pipeline.set_view(camera.view);
  standard_pipeline.set_scene_lights(lights);
  standard_pipeline.set_light_space_proj_view(shadow_cam.projection,
                                              shadow_cam.view);

  standard_pipeline.begin_pass(cmd_);
  for (auto &request : render_request) {
    standard_pipeline.draw_model(request.model, request.transform, cmd_,
                                 DrawPass::OPAQUE);
  }
  for (auto &request : render_request) {
    standard_pipeline.draw_model(request.model, request.transform, cmd_,
                                 DrawPass::TRANSPARENT);
  }
  standard_pipeline.end_pass();
  SDL_SubmitGPUCommandBuffer(cmd_);
};

static std::vector<uint8_t> load_spirv(std::filesystem::path path) {
  SDL_IOStream *io = SDL_IOFromFile(path.c_str(), "rb");
  if (!io)
    return {};

  Sint64 size = SDL_GetIOSize(io);
  if (size <= 0) {
    SDL_CloseIO(io);
    return {};
  }

  std::vector<uint8_t> buf(static_cast<size_t>(size));
  SDL_ReadIO(io, buf.data(), buf.size());
  SDL_CloseIO(io);
  return buf;
}

bool NewRenderer::create_compute_pipeline() {
  auto spirv = load_spirv("./Data/shaders/fft.comp.spv");

  const SDL_GPUComputePipelineCreateInfo create_info = {
      .code_size = spirv.size(),
      .code = spirv.data(),
      .entrypoint = "main",
      .format = SDL_GPU_SHADERFORMAT_SPIRV,

      .num_samplers = 0,
      .num_readonly_storage_textures = 1,
      .num_readonly_storage_buffers = 0,
      .num_readwrite_storage_textures = 1,
      .num_readwrite_storage_buffers = 0,
      .num_uniform_buffers = 0,

      .threadcount_x = 64,
      .threadcount_y = 1,
      .threadcount_z = 1,

      .props = 0,
  };

  compute_pipeline_ = SDL_CreateGPUComputePipeline(device_, &create_info);
  if (!compute_pipeline_) {
    SDL_Log("[Renderer] create_compute_pipeline: SDL_CreateGPUComputePipeline "
            "failed: %s",
            SDL_GetError());
    return false;
  }
  return true;
}

bool NewRenderer::create_compute_target() {
  SDL_GPUTextureCreateInfo info{};
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  info.width = static_cast<Uint32>(fft_input->width);
  info.height = static_cast<Uint32>(fft_input->height);
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  info.usage =
      SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE // writeable by compute shader
      | SDL_GPU_TEXTUREUSAGE_SAMPLER;            // readable in fragment shader
  texture_manager_->create_texture(info, &compute_target_);
  if (!compute_target_) {
    SDL_Log("[Renderer] create_compute_target: failed: %s", SDL_GetError());
    return false;
  }
  return true;
}

void NewRenderer::run_compute_pass() {
  cmd_ = SDL_AcquireGPUCommandBuffer(device_);
  SDL_GPUStorageTextureReadWriteBinding rw_binding{};
  rw_binding.texture = compute_target_;
  rw_binding.mip_level = 0;
  rw_binding.layer = 0;
  rw_binding.cycle = true;

  SDL_GPUComputePass *compute_pass =
      SDL_BeginGPUComputePass(cmd_, &rw_binding, 1, nullptr, 0);

  if (!compute_pass) {
    SDL_Log("[Renderer] run_compute_pass: SDL_BeginGPUComputePass failed: %s",
            SDL_GetError());
    SDL_CancelGPUCommandBuffer(cmd_);
    return;
  }

  // 1. pipeline
  SDL_BindGPUComputePipeline(compute_pass, compute_pipeline_);
  // 2. readonly input texture (set = 1)
  SDL_BindGPUComputeStorageTextures(compute_pass, 0, &fft_input->texture, 1);
  // 3. dispatch
  uint32_t total = fft_input->height * fft_input->width;
  uint32_t groups = (total + 63) / 64;
  SDL_DispatchGPUCompute(compute_pass, groups, 1, 1);

  SDL_EndGPUComputePass(compute_pass);
  SDL_SubmitGPUCommandBuffer(cmd_);
}
