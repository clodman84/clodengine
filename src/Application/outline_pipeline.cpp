#include "include/graphics_pipeline.h"
#include <SDL3/SDL.h>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

bool OutlinePipeline::create_pipeline() {
  SDL_GPUShader *vertex_shader_ = load_shader(
      "./Data/shaders/outline_vertex.spv", SDL_GPU_SHADERSTAGE_VERTEX);
  if (!vertex_shader_)
    return false;
  SDL_GPUShader *fragment_shader_ =
      load_shader("./Data/shaders/outline_fragment.spv",
                  SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
  if (!fragment_shader_)
    return false;

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

  pipeline_info.target_info.num_color_targets = 1;
  pipeline_info.target_info.color_target_descriptions = colour_target;

  pipeline_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  pipeline_info.depth_stencil_state.enable_depth_test = false;
  pipeline_info.depth_stencil_state.enable_depth_write = false;
  pipeline_info.target_info.has_depth_stencil_target = false;

  pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pipeline_info);
  if (!pipeline_) {
    SDL_Log("[Outline Pipeline] create_pipeline: "
            "SDL_CreateGPUGraphicsPipeline failed: %s",
            SDL_GetError());
    return false;
  }

  SDL_Log("[OutlinePipeline] create_pipeline: succeeded");
  SDL_ReleaseGPUShader(device_, vertex_shader_);
  vertex_shader_ = nullptr;
  SDL_ReleaseGPUShader(device_, fragment_shader_);
  fragment_shader_ = nullptr;
  return true;
}

bool OutlinePipeline::create_textures() { return true; }

bool OutlinePipeline::destroy_textures() { return true; }

bool OutlinePipeline::create_samplers() {
  SDL_GPUSamplerCreateInfo info{};
  info.min_filter = SDL_GPU_FILTER_NEAREST;
  info.mag_filter = SDL_GPU_FILTER_NEAREST;
  info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

  sampler_ = SDL_CreateGPUSampler(device_, &info);
  if (!sampler_) {
    SDL_Log("[OutlinePipeline] create_sampler: SDL_CreateGPUSampler failed: %s",
            SDL_GetError());
    return false;
  }
  return true;
}

bool OutlinePipeline::destroy_samplers() {
  if (sampler_)
    SDL_ReleaseGPUSampler(device_, sampler_);
  return true;
}

void OutlinePipeline::begin_pass(SDL_GPUCommandBuffer *cmd_) {
  SDL_GPUColorTargetInfo target_info{};
  target_info.texture = render_target_;
  target_info.load_op = SDL_GPU_LOADOP_LOAD;
  target_info.store_op = SDL_GPU_STOREOP_STORE;
  target_info.cycle = false;

  pass_ = SDL_BeginGPURenderPass(cmd_, &target_info, 1, nullptr);
  SDL_BindGPUGraphicsPipeline(pass_, pipeline_);
}

const void OutlinePipeline::draw_model(std::shared_ptr<Model> model,
                                       const WorldTransformComponent &transform,
                                       SDL_GPUCommandBuffer *cmd_,
                                       DrawPass pass) {
#ifdef TRACY_ENABLE
  ZoneScoped;
#endif
  SDL_Log("[OutlinePipeline] cannot draw models, use draw_outline instead");
}

const void OutlinePipeline::draw_outline(SDL_GPUCommandBuffer *cmd_) {
#ifdef TRACY_ENABLE
  ZoneScoped;
#endif
  if (!pass_) {
    SDL_Log(
        "[OutlinePipeline] draw_outline: called outside begin_pass/end_pass");
    return;
  }
  if (!jfa_source) {
    SDL_Log("[OutlinePipeline] draw_outline: jfa_source not set, call "
            "set_jfa_source first");
    return;
  }
  if (!render_target_) {
    SDL_Log("[OutlinePipeline] draw_outline: render_target_ not set, call "
            "set_render_target first");
    return;
  }

  SDL_GPUTextureSamplerBinding binding{};
  binding.texture = jfa_source;
  binding.sampler = sampler_;
  TimeUniform time{SDL_GetTicksNS() / 1e9f};
  SDL_BindGPUFragmentSamplers(pass_, 0, &binding, 1);
  SDL_PushGPUFragmentUniformData(cmd_, 0, &time, sizeof(TimeUniform));

  // No vertex buffer: the vertex shader generates a fullscreen covering
  // triangle from gl_VertexIndex.
  SDL_DrawGPUPrimitives(pass_, 3, 1, 0, 0);
};
