#include "include/graphics_pipeline.h"
#include <SDL3/SDL_gpu.h>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

bool MaskPipeline::create_pipeline() {
  SDL_GPUShader *vertex_shader_ = load_shader("./Data/shaders/mask_vertex.spv",
                                              SDL_GPU_SHADERSTAGE_VERTEX, 1);
  if (!vertex_shader_)
    return false;
  SDL_GPUShader *fragment_shader_ = load_shader(
      "./Data/shaders/mask_fragment.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
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
  colour_target[0].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM;
  colour_target[0].blend_state.enable_blend = false;

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

  pipeline_info.rasterizer_state.enable_depth_bias = false;
  pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pipeline_info);

  if (!pipeline_) {
    SDL_Log("[MaskPipeline] create_mask_pipeline: "
            "SDL_CreateGPUGraphicsPipeline "
            "failed: %s",
            SDL_GetError());
    return false;
  }

  SDL_Log("[MaskPipeline] create_mask_pipeline: succeeded");
  SDL_ReleaseGPUShader(device_, vertex_shader_);
  SDL_ReleaseGPUShader(device_, fragment_shader_);
  vertex_shader_ = nullptr;
  fragment_shader_ = nullptr;
  return true;
}

bool MaskPipeline::create_textures() {

  SDL_GPUTextureCreateInfo info{};
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
  info.width = mask_width;
  info.height = mask_height;
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
               SDL_GPU_TEXTUREUSAGE_SAMPLER |
               SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ |
               SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
  texture_manager_->create_texture(info, &mask_map_);
  if (!mask_map_) {
    SDL_Log("[MaskPipeline] create_mask_map: failed: %s", SDL_GetError());
    return false;
  }
  return true;
}

bool MaskPipeline::destroy_textures() {
  texture_manager_->queue_destruction(mask_map_);
  return true;
}

bool MaskPipeline::create_samplers() { return true; }
bool MaskPipeline::destroy_samplers() { return true; }

void MaskPipeline::begin_pass(SDL_GPUCommandBuffer *cmd_) {
  SDL_GPUColorTargetInfo target_info{};
  target_info.texture = mask_map_;
  target_info.clear_color = {-1};
  target_info.load_op = SDL_GPU_LOADOP_CLEAR;
  target_info.store_op = SDL_GPU_STOREOP_STORE;
  target_info.cycle = true;

  pass_ = SDL_BeginGPURenderPass(cmd_, &target_info, 1, nullptr);
  SDL_BindGPUGraphicsPipeline(pass_, pipeline_);
}

const void MaskPipeline::draw_model(std::shared_ptr<Model> model,
                                    const WorldTransformComponent &transform,
                                    SDL_GPUCommandBuffer *cmd_, DrawPass pass) {
#ifdef TRACY_ENABLE
  ZoneScoped;
#endif

  glm::mat4 model_matrix = transform.matrix;
  mask_uniform_.proj_view = proj_ * view_;
  mask_uniform_.model = model_matrix;
  model->bind(pass_);
  for (const auto &dc : model->draw_calls()) {
    SDL_PushGPUVertexUniformData(cmd_, 0, &mask_uniform_,
                                 sizeof(ShadowUniform));

    SDL_PushGPUFragmentUniformData(cmd_, 0, &screen_dimensions,
                                   sizeof(ScreenDimensions));
    SDL_DrawGPUIndexedPrimitives(pass_, dc.index_count, 1, dc.index_offset, 0,
                                 0);
  }
};
