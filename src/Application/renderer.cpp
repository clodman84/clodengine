#include "include/renderer.h"
#include "include/components.h"
#include "include/image.h"
#include "include/model.h"
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>

#include <SDL3/SDL.h>
#include <filesystem>
#include <memory>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

Renderer::Renderer(SDL_GPUDevice *device, SDL_Window *window,
                   std::shared_ptr<TextureManager> texture_manager)
    : device_(device), window_(window), texture_manager_(texture_manager) {
  SDL_Log("[Renderer] Renderer object created");
};

Renderer::~Renderer() {
  if (shadow_pipeline_)
    SDL_ReleaseGPUGraphicsPipeline(device_, shadow_pipeline_);
  if (shadow_map_)
    SDL_ReleaseGPUTexture(device_, shadow_map_);
  if (shadow_sampler_)
    SDL_ReleaseGPUSampler(device_, shadow_sampler_);
  if (compute_pipeline_)
    SDL_ReleaseGPUComputePipeline(device_, compute_pipeline_);
  if (compute_target_)
    SDL_ReleaseGPUTexture(device_, compute_target_);
  if (pipeline_)
    SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
  if (render_target_)
    SDL_ReleaseGPUTexture(device_, render_target_);
  if (depth_target_)
    SDL_ReleaseGPUTexture(device_, depth_target_);
  if (gray_texture_)
    SDL_ReleaseGPUTexture(device_, gray_texture_);
  if (sampler_)
    SDL_ReleaseGPUSampler(device_, sampler_);
  SDL_Log("[Renderer] Renderer object destroyed");
}

SDL_GPUTextureFormat Renderer::get_supported_depth_format() {
  // Prefer 32-bit, fall back to 16-bit
  const SDL_GPUTextureFormat candidates[] = {
      SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
      SDL_GPU_TEXTUREFORMAT_D16_UNORM,
  };
  for (auto fmt : candidates) {
    if (SDL_GPUTextureSupportsFormat(device_, fmt, SDL_GPU_TEXTURETYPE_2D,
                                     SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
      return fmt;
  }
  return SDL_GPU_TEXTUREFORMAT_INVALID;
}

bool Renderer::init() {
  depth_format_ = get_supported_depth_format();
  if (depth_format_ == SDL_GPU_TEXTUREFORMAT_INVALID) {
    SDL_Log("[Renderer] init: no supported depth format");
    return false;
  }

  fft_input =
      std::make_unique<Image>("./Data/assets/fft_input.png", texture_manager_);
  fft_input->load_fullres();

  if (!create_pipeline())
    return false;
  if (!create_sampler())
    return false;
  if (!create_gray_texture())
    return false;
  if (!create_render_target())
    return false;
  if (!create_shadow_pipeline())
    return false;
  if (!create_shadow_map())
    return false;
  if (!create_shadow_sampler())
    return false;
  if (!create_compute_pipeline())
    return false;
  if (!create_compute_target())
    return false;

  return true;
}

void Renderer::begin_frame() {
  cmd_ = SDL_AcquireGPUCommandBuffer(device_);

  SDL_GPUColorTargetInfo target_info{};
  target_info.texture = render_target_;
  target_info.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 1.0f};
  target_info.load_op = SDL_GPU_LOADOP_CLEAR;
  target_info.store_op = SDL_GPU_STOREOP_STORE;
  target_info.cycle = false;

  SDL_GPUDepthStencilTargetInfo depth_info{};
  depth_info.texture = depth_target_;
  depth_info.clear_depth = 1.0f;
  depth_info.load_op = SDL_GPU_LOADOP_CLEAR;
  depth_info.store_op =
      SDL_GPU_STOREOP_DONT_CARE; // don't need depth after rendering
  depth_info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
  depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
  depth_info.cycle = false;

  pass_ = SDL_BeginGPURenderPass(cmd_, &target_info, 1, &depth_info);
  SDL_BindGPUGraphicsPipeline(pass_, pipeline_);
}

void Renderer::end_frame() {
  SDL_EndGPURenderPass(pass_);
  SDL_SubmitGPUCommandBuffer(cmd_);
}

void Renderer::draw_model(std::shared_ptr<Model> model,
                          const WorldTransformComponent &transform,
                          DrawPass pass) {
  glm::mat4 model_matrix = transform.matrix;
#ifdef TRACY_ENABLE
  ZoneScoped;
#endif

  uniform_.proj_view = proj_ * view_;
  uniform_.light_space_proj_view = shadow_uniform_.proj_view;
  uniform_.model = model_matrix;
  float det = glm::determinant(model_matrix);
  if (!std::isfinite(det) || std::abs(det) < 0.0001f) {
    SDL_Log("[Renderer] BAD!!! draw_model: det=%.6f entity has degenerate "
            "transform",
            det);
  }
  uniform_.normal_matrix = glm::transpose(glm::inverse(model_matrix));
  // uniform_.light = glm::vec4(glm::normalize(glm::vec3(1, 2, 1)), 0.f);
  uniform_.time = SDL_GetTicksNS() / 1e9f;

  model->bind(pass_);

  for (const auto &dc : model->draw_calls()) {
    bool dc_transparent = !(dc.alpha_mode == AlphaMode::OPAQUE);
    if (pass == DrawPass::Opaque && dc_transparent)
      continue;
    if (pass == DrawPass::Transparent && !dc_transparent)
      continue;
    uniform_.base_colour = dc.base_colour;
    uniform_.has_texture = dc.texture ? 1.f : 0.f;

    SDL_PushGPUVertexUniformData(cmd_, 0, &uniform_, sizeof(UniformBuffer));
    SDL_PushGPUFragmentUniformData(cmd_, 0, &uniform_, sizeof(UniformBuffer));
    SDL_PushGPUFragmentUniformData(cmd_, 1, &light_buffer_,
                                   sizeof(SceneLightBuffer));
    SDL_GPUTextureSamplerBinding bindings[2]{};
    bindings[0].texture = dc.texture ? dc.texture : gray_texture_;
    bindings[0].sampler = sampler_;
    bindings[1].texture = shadow_map_;
    bindings[1].sampler = shadow_sampler_;
    SDL_BindGPUFragmentSamplers(pass_, 0, bindings, 2);
    SDL_DrawGPUIndexedPrimitives(pass_, dc.index_count, 1, dc.index_offset, 0,
                                 0);
  }
};

SDL_GPUShader *Renderer::load_shader(std::filesystem::path path,
                                     SDL_GPUShaderStage stage,
                                     Uint32 num_uniform_buffers,
                                     Uint32 num_samplers) const {
  size_t code_size = 0;
  void *code = SDL_LoadFile(path.c_str(), &code_size);
  if (!code) {
    SDL_Log("[Renderer] load_shader: failed to load shader: %s - %s",
            path.c_str(), SDL_GetError());
    return nullptr;
  }

  SDL_GPUShaderCreateInfo info{};
  info.code = static_cast<const Uint8 *>(code);
  info.code_size = code_size;
  info.entrypoint = "main";
  info.format = SDL_GPU_SHADERFORMAT_SPIRV;
  info.stage = stage;
  info.num_uniform_buffers = num_uniform_buffers;
  info.num_samplers = num_samplers;

  SDL_GPUShader *shader = SDL_CreateGPUShader(device_, &info);
  SDL_free(code);

  if (!shader)
    SDL_Log(
        "[Renderer] load_shader: Game: SDL_CreateGPUShader failed for %s - %s",
        path.c_str(), SDL_GetError());
  return shader;
}

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

bool Renderer::create_compute_pipeline() {
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

bool Renderer::create_compute_target() {
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

  compute_target_ = SDL_CreateGPUTexture(device_, &info);
  if (!compute_target_) {
    SDL_Log("[Renderer] create_compute_target: failed: %s", SDL_GetError());
    return false;
  }
  return true;
}

void Renderer::run_compute_pass() {
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

bool Renderer::create_pipeline() {
  SDL_GPUShader *vertex_shader_ =
      load_shader("./Data/shaders/vertex.spv", SDL_GPU_SHADERSTAGE_VERTEX, 1);
  if (!vertex_shader_)
    return false;
  SDL_GPUShader *fragment_shader_ = load_shader(
      "./Data/shaders/fragment.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 2);
  if (!fragment_shader_)
    return false;

  SDL_GPUVertexBufferDescription vb_desc{};
  vb_desc.slot = 0;
  vb_desc.pitch = sizeof(Vertex);
  vb_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
  vb_desc.instance_step_rate = 0;

  SDL_GPUVertexAttribute attribs[3]{};

  // location 0: position (float3, offset 0)
  attribs[0].buffer_slot = 0;
  attribs[0].location = 0;
  attribs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
  attribs[0].offset = 0;

  // location 1: normal (float3, offset 12)
  attribs[1].buffer_slot = 0;
  attribs[1].location = 1;
  attribs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
  attribs[1].offset = sizeof(float) * 3;

  // location 2: uv (float2, offset 24)
  attribs[2].buffer_slot = 0;
  attribs[2].location = 2;
  attribs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
  attribs[2].offset = sizeof(float) * 6;

  SDL_GPUColorTargetDescription colour_target[1]{};
  colour_target[0].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  colour_target[0].blend_state.enable_blend = true;

  // RGB: out = src.rgb * src.a + dst.rgb * (1 - src.a)
  colour_target[0].blend_state.src_color_blendfactor =
      SDL_GPU_BLENDFACTOR_SRC_ALPHA;
  colour_target[0].blend_state.dst_color_blendfactor =
      SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  colour_target[0].blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;

  // Alpha: out = src.a * 1 + dst.a * (1 - src.a)
  colour_target[0].blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  colour_target[0].blend_state.dst_alpha_blendfactor =
      SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  colour_target[0].blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

  SDL_GPUGraphicsPipelineCreateInfo pipeline_info{};
  pipeline_info.vertex_shader = vertex_shader_;
  pipeline_info.fragment_shader = fragment_shader_;
  pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

  pipeline_info.vertex_input_state.num_vertex_buffers = 1;
  pipeline_info.vertex_input_state.vertex_buffer_descriptions = &vb_desc;
  pipeline_info.vertex_input_state.num_vertex_attributes = 3;
  pipeline_info.vertex_input_state.vertex_attributes = attribs;

  pipeline_info.target_info.num_color_targets = 1;
  pipeline_info.target_info.color_target_descriptions = colour_target;

  pipeline_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
  pipeline_info.rasterizer_state.front_face =
      SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

  pipeline_info.depth_stencil_state.enable_depth_test = true;
  pipeline_info.depth_stencil_state.enable_depth_write = true;
  pipeline_info.depth_stencil_state.compare_op =
      SDL_GPU_COMPAREOP_LESS_OR_EQUAL;

  pipeline_info.target_info.has_depth_stencil_target = true;
  pipeline_info.target_info.depth_stencil_format = depth_format_;

  pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pipeline_info);
  if (!pipeline_) {
    SDL_Log(
        "[Renderer] create_pipeline: SDL_CreateGPUGraphicsPipeline failed: %s",
        SDL_GetError());
    return false;
  }

  SDL_ReleaseGPUShader(device_, vertex_shader_);
  vertex_shader_ = nullptr;
  SDL_ReleaseGPUShader(device_, fragment_shader_);
  fragment_shader_ = nullptr;
  return true;
}

bool Renderer::create_shadow_pipeline() {
  // TODO: USING THE SAME SHADER IS PROBABLY NOT IDEAL
  SDL_GPUShader *vertex_shader_ = load_shader(
      "./Data/shaders/shadow_vertex.spv", SDL_GPU_SHADERSTAGE_VERTEX, 1);
  if (!vertex_shader_)
    return false;
  SDL_GPUShader *fragment_shader_ = load_shader(
      "./Data/shaders/shadow_fragment.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
  if (!fragment_shader_)
    return false;

  SDL_GPUVertexBufferDescription vb_desc{};
  vb_desc.slot = 0;
  vb_desc.pitch = sizeof(Vertex);
  vb_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
  vb_desc.instance_step_rate = 0;

  SDL_GPUVertexAttribute attribs[3]{};

  // location 0: position (float3, offset 0)
  attribs[0].buffer_slot = 0;
  attribs[0].location = 0;
  attribs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
  attribs[0].offset = 0;

  // location 1: normal (float3, offset 12)
  attribs[1].buffer_slot = 0;
  attribs[1].location = 1;
  attribs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
  attribs[1].offset = sizeof(float) * 3;

  // location 2: uv (float2, offset 24)
  attribs[2].buffer_slot = 0;
  attribs[2].location = 2;
  attribs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
  attribs[2].offset = sizeof(float) * 6;

  SDL_GPUGraphicsPipelineCreateInfo pipeline_info{};
  pipeline_info.vertex_shader = vertex_shader_;
  pipeline_info.fragment_shader = fragment_shader_;
  pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

  pipeline_info.vertex_input_state.num_vertex_buffers = 1;
  pipeline_info.vertex_input_state.vertex_buffer_descriptions = &vb_desc;
  pipeline_info.vertex_input_state.num_vertex_attributes = 3;
  pipeline_info.vertex_input_state.vertex_attributes = attribs;

  pipeline_info.target_info.num_color_targets = 0;

  pipeline_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_FRONT;
  pipeline_info.rasterizer_state.front_face =
      SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

  pipeline_info.rasterizer_state.enable_depth_bias = true;
  pipeline_info.rasterizer_state.depth_bias_constant_factor = 2.0f;
  pipeline_info.rasterizer_state.depth_bias_slope_factor = 1.5f;

  pipeline_info.depth_stencil_state.enable_depth_test = true;
  pipeline_info.depth_stencil_state.enable_depth_write = true;
  pipeline_info.depth_stencil_state.compare_op =
      SDL_GPU_COMPAREOP_LESS_OR_EQUAL;

  pipeline_info.target_info.has_depth_stencil_target = true;
  pipeline_info.target_info.depth_stencil_format = depth_format_;

  shadow_pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pipeline_info);
  if (!shadow_pipeline_) {
    SDL_Log("[Renderer] create_shadow_pipeline: SDL_CreateGPUGraphicsPipeline "
            "failed: %s",
            SDL_GetError());
    return false;
  }

  SDL_Log("[Renderer] create_shadow_pipeline: succeeded");
  SDL_ReleaseGPUShader(device_, vertex_shader_);
  SDL_ReleaseGPUShader(device_, fragment_shader_);
  vertex_shader_ = nullptr;
  fragment_shader_ = nullptr;
  return true;
}

void Renderer::begin_shadow_pass() {
  cmd_ = SDL_AcquireGPUCommandBuffer(device_);

  SDL_GPUDepthStencilTargetInfo depth_info{};
  depth_info.texture = shadow_map_;
  depth_info.clear_depth = 1.0f;
  depth_info.load_op = SDL_GPU_LOADOP_CLEAR;
  depth_info.store_op = SDL_GPU_STOREOP_STORE; // must store for sampling
  depth_info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
  depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
  depth_info.cycle = false;

  pass_ = SDL_BeginGPURenderPass(cmd_, nullptr, 0, &depth_info);
  SDL_BindGPUGraphicsPipeline(pass_, shadow_pipeline_);
}

void Renderer::end_shadow_pass() {
  SDL_EndGPURenderPass(pass_);
  SDL_SubmitGPUCommandBuffer(cmd_);
}

void Renderer::draw_model_shadow(std::shared_ptr<Model> model,
                                 const WorldTransformComponent &transform) {
  glm::mat4 model_matrix = transform.matrix;

#ifdef TRACY_ENABLE
  ZoneScoped;
#endif

  shadow_uniform_.proj_view = proj_ * view_;
  shadow_uniform_.model = model_matrix;
  float det = glm::determinant(model_matrix);
  if (!std::isfinite(det) || std::abs(det) < 0.0001f) {
    SDL_Log("[Renderer] BAD!!! draw_model: det=%.6f entity has degenerate "
            "transform",
            det);
  }
  model->bind(pass_);

  for (const auto &dc : model->draw_calls()) {
    if (dc.alpha_mode != AlphaMode::OPAQUE)
      continue;

    SDL_PushGPUVertexUniformData(cmd_, 0, &shadow_uniform_,
                                 sizeof(ShadowUniform));
    SDL_DrawGPUIndexedPrimitives(pass_, dc.index_count, 1, dc.index_offset, 0,
                                 0);
  }
}

bool Renderer::create_shadow_map() {
  SDL_GPUTextureCreateInfo info{};
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = depth_format_;
  info.width = shadow_map_width;
  info.height = shadow_map_height;
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  info.usage =
      SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;

  shadow_map_ = SDL_CreateGPUTexture(device_, &info);
  if (!shadow_map_) {
    SDL_Log("[Renderer] create_shadow_map: failed: %s", SDL_GetError());
    return false;
  }
  return true;
}

bool Renderer::create_shadow_sampler() {
  SDL_GPUSamplerCreateInfo info{};
  info.min_filter = SDL_GPU_FILTER_LINEAR;
  info.mag_filter = SDL_GPU_FILTER_LINEAR;
  info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  info.enable_compare = true;
  info.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;

  shadow_sampler_ = SDL_CreateGPUSampler(device_, &info);
  if (!shadow_sampler_) {
    SDL_Log("[Renderer] create_shadow_sampler: failed: %s", SDL_GetError());
    return false;
  }
  return true;
}

bool Renderer::create_render_target() {
  SDL_GPUTextureCreateInfo tex_info{};
  tex_info.type = SDL_GPU_TEXTURETYPE_2D;
  tex_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  tex_info.width = static_cast<Uint32>(render_width_);
  tex_info.height = static_cast<Uint32>(render_height_);
  tex_info.layer_count_or_depth = 1;
  tex_info.num_levels = 1;
  tex_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                   SDL_GPU_TEXTUREUSAGE_SAMPLER |
                   SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ;

  render_target_ = SDL_CreateGPUTexture(device_, &tex_info);
  if (!render_target_) {
    SDL_Log("[Renderer] create_render_target: SDL_CreateGPUTexture failed: %s",
            SDL_GetError());
    return false;
  }

  SDL_GPUTextureCreateInfo depth_info{};
  depth_info.type = SDL_GPU_TEXTURETYPE_2D;
  depth_info.format = depth_format_;
  depth_info.width = static_cast<Uint32>(render_width_);
  depth_info.height = static_cast<Uint32>(render_height_);
  depth_info.layer_count_or_depth = 1;
  depth_info.num_levels = 1;
  depth_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;

  depth_target_ = SDL_CreateGPUTexture(device_, &depth_info);
  if (!depth_target_) {
    SDL_Log(
        "[Renderer] create_render_target: depth texture creation failed: %s",
        SDL_GetError());
    return false;
  }
  return true;
}

bool Renderer::create_sampler() {

  // Sampler for models: nearest neighbour, clamp to edge
  SDL_GPUSamplerCreateInfo info{};
  info.min_filter = SDL_GPU_FILTER_NEAREST;
  info.mag_filter = SDL_GPU_FILTER_NEAREST;
  info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;

  sampler_ = SDL_CreateGPUSampler(device_, &info);
  if (!sampler_) {
    SDL_Log("[Renderer] create_sampler: SDL_CreateGPUSampler failed: %s",
            SDL_GetError());
    return false;
  }
  return true;
}

bool Renderer::create_gray_texture() {
  unsigned char gray[4] = {128, 128, 128, 255};
  return texture_manager_->upload_texture_data_to_gpu(gray, 1, 1,
                                                      &gray_texture_);
}
