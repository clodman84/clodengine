#include "include/graphics_pipeline.h"
#include <SDL3/SDL_gpu.h>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

bool ShadowPipeline::create_pipeline() {
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

  pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pipeline_info);
  if (!pipeline_) {
    SDL_Log("[ShadowPipeline] create_shadow_pipeline: "
            "SDL_CreateGPUGraphicsPipeline "
            "failed: %s",
            SDL_GetError());
    return false;
  }

  SDL_Log("[ShadowPipeline] create_shadow_pipeline: succeeded");
  SDL_ReleaseGPUShader(device_, vertex_shader_);
  SDL_ReleaseGPUShader(device_, fragment_shader_);
  vertex_shader_ = nullptr;
  fragment_shader_ = nullptr;
  return true;
}

bool ShadowPipeline::create_textures() {

  SDL_GPUTextureCreateInfo info{};
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = depth_format_;
  info.width = shadow_map_width;
  info.height = shadow_map_height;
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  info.usage =
      SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;

  texture_manager_->create_texture(info, &shadow_map_);
  if (!shadow_map_) {
    SDL_Log("[ShadowPipeline] create_shadow_map: failed: %s", SDL_GetError());
    return false;
  }
  return true;
}

bool ShadowPipeline::destroy_textures() {
  texture_manager_->queue_destruction(shadow_map_);
  return true;
}

bool ShadowPipeline::create_samplers() { return true; }
bool ShadowPipeline::destroy_samplers() { return true; }

void ShadowPipeline::begin_pass(SDL_GPUCommandBuffer *cmd_) {
  SDL_GPUDepthStencilTargetInfo depth_info{};
  depth_info.texture = shadow_map_;
  depth_info.clear_depth = 1.0f;
  depth_info.load_op = SDL_GPU_LOADOP_CLEAR;
  depth_info.store_op = SDL_GPU_STOREOP_STORE; // must store for sampling
  depth_info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
  depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
  depth_info.cycle = false;

  pass_ = SDL_BeginGPURenderPass(cmd_, nullptr, 0, &depth_info);
  SDL_BindGPUGraphicsPipeline(pass_, pipeline_);
}

const void ShadowPipeline::draw_model(std::shared_ptr<Model> model,
                                      const WorldTransformComponent &transform,
                                      SDL_GPUCommandBuffer *cmd_,
                                      DrawPass pass) {
#ifdef TRACY_ENABLE
  ZoneScoped;
#endif

  glm::mat4 model_matrix = transform.matrix;
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
    SDL_PushGPUVertexUniformData(cmd_, 0, &shadow_uniform_,
                                 sizeof(ShadowUniform));
    SDL_DrawGPUIndexedPrimitives(pass_, dc.index_count, 1, dc.index_offset, 0,
                                 0);
  }
};
