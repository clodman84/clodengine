#include "include/graphics_pipeline.h"

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

bool StandardPipeline::create_pipeline() {
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
    SDL_Log("[Standard Pipeline] create_pipeline: "
            "SDL_CreateGPUGraphicsPipeline failed: %s",
            SDL_GetError());
    return false;
  }

  SDL_Log("[StandardPipeline] create_pipeline: succeeded");
  SDL_ReleaseGPUShader(device_, vertex_shader_);
  vertex_shader_ = nullptr;
  SDL_ReleaseGPUShader(device_, fragment_shader_);
  fragment_shader_ = nullptr;
  return true;
}

bool StandardPipeline::create_textures() {
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
  texture_manager_->create_texture(tex_info, &render_target_);

  if (!render_target_) {
    SDL_Log("[Standard Pipeline] create_textures: SDL_CreateGPUTexture failed: "
            "%s while making render_target_",
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
  texture_manager_->create_texture(depth_info, &depth_target_);

  if (!depth_target_) {
    SDL_Log("[StandardPipeline] create_textures: depth texture creation "
            "failed: %s while making depth_target_",
            SDL_GetError());
    return false;
  }

  unsigned char gray[4] = {128, 128, 128, 255};
  return texture_manager_->upload_texture_data_to_gpu(gray, 1, 1,
                                                      &gray_texture_);
  return true;
}

bool StandardPipeline::destroy_textures() {
  texture_manager_->queue_destruction(render_target_);
  texture_manager_->queue_destruction(depth_target_);
  texture_manager_->queue_destruction(gray_texture_);
  return true;
}

bool StandardPipeline::create_samplers() {
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
    SDL_Log(
        "[StandardPipeline] create_sampler: SDL_CreateGPUSampler failed: %s",
        SDL_GetError());
    return false;
  }

  // sampler for the shadow map
  SDL_GPUSamplerCreateInfo shadow_info{};
  shadow_info.min_filter = SDL_GPU_FILTER_LINEAR;
  shadow_info.mag_filter = SDL_GPU_FILTER_LINEAR;
  shadow_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  shadow_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  shadow_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  shadow_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  shadow_info.enable_compare = true;
  shadow_info.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;

  shadow_sampler_ = SDL_CreateGPUSampler(device_, &shadow_info);
  if (!shadow_sampler_) {
    SDL_Log("[StandardPipeline] create_shadow_sampler: failed: %s",
            SDL_GetError());
    return false;
  }
  return true;
}

bool StandardPipeline::destroy_samplers() {
  if (sampler_)
    SDL_ReleaseGPUSampler(device_, sampler_);
  if (shadow_sampler_)
    SDL_ReleaseGPUSampler(device_, shadow_sampler_);
  return true;
}

void StandardPipeline::begin_pass(SDL_GPUCommandBuffer *cmd_) {
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

const void
StandardPipeline::draw_model(std::shared_ptr<Model> model,
                             const WorldTransformComponent &transform,
                             SDL_GPUCommandBuffer *cmd_, DrawPass pass) {

#ifdef TRACY_ENABLE
  ZoneScoped;
#endif

  glm::mat4 model_matrix = transform.matrix;
  uniform_.proj_view = proj_ * view_;
  uniform_.light_space_proj_view = light_space_proj_view;
  uniform_.model = model_matrix;
  float det = glm::determinant(model_matrix);
  uniform_.normal_matrix = glm::transpose(glm::inverse(model_matrix));
  uniform_.time = SDL_GetTicksNS() / 1e9f;
  model->bind(pass_);

  for (const auto &dc : model->draw_calls()) {
    bool dc_transparent = !(dc.alpha_mode == AlphaMode::OPAQUE);
    if (pass == DrawPass::OPAQUE && dc_transparent)
      continue;
    if (pass == DrawPass::TRANSPARENT && !dc_transparent)
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
