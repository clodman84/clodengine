#include "include/graphics_pipeline.h"

Pipeline::Pipeline(SDL_GPUDevice *device, SDL_Window *window,
                   std::shared_ptr<TextureManager> texture_manager)
    : device_(device), window_(window), texture_manager_(texture_manager) {
  SDL_Log("[Pipeline] %s object created", name.c_str());
};

Pipeline::~Pipeline() {
  if (pipeline_)
    SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
}

bool Pipeline::init() {
  depth_format_ = get_supported_depth_format();
  if (depth_format_ == SDL_GPU_TEXTUREFORMAT_INVALID) {
    SDL_Log("[Pipeline] init: no supported depth format");
    return false;
  }
  if (!create_pipeline())
    return false;
  if (!create_samplers())
    return false;
  if (!create_textures())
    return false;
  return true;
}

SDL_GPUTextureFormat Pipeline::get_supported_depth_format() {
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

void Pipeline::end_pass() { SDL_EndGPURenderPass(pass_); }

SDL_GPUShader *Pipeline::load_shader(std::filesystem::path path,
                                     SDL_GPUShaderStage stage,
                                     Uint32 num_uniform_buffers,
                                     Uint32 num_samplers) const {
  size_t code_size = 0;
  void *code = SDL_LoadFile(path.c_str(), &code_size);
  if (!code) {
    SDL_Log("[Pipeline] load_shader: failed to load shader: %s - %s",
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
    SDL_Log("[Pipeline] load_shader: SDL_CreateGPUShader failed for %s - %s",
            path.c_str(), SDL_GetError());
  return shader;
}
