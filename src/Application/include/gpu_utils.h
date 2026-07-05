#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <unordered_set>
#include <vector>

unsigned char *
load_texture_data_from_file(const std::filesystem::path &file_name, int *width,
                            int *height, float scale);

unsigned char *resize_image_rgba8(const unsigned char *src_data, int src_w,
                                  int src_h, int dst_w, int dst_h);

class TextureManager {
public:
  TextureManager(SDL_GPUDevice *device) : device(device) {};
  ~TextureManager() = default;

  TextureManager(const TextureManager &) = delete;
  TextureManager &operator=(const TextureManager &) = delete;

  TextureManager(TextureManager &&other) noexcept
      : device(other.device), textures_in_use(std::move(other.textures_in_use)),
        textures_to_release(std::move(other.textures_to_release)) {}

  bool upload_texture_data_to_gpu(unsigned char *image_data, int width,
                                  int height, SDL_GPUTexture **out_texture);
  const bool create_texture(SDL_GPUTextureCreateInfo &tex_info,
                            SDL_GPUTexture **out_texture);

  void queue_destruction(SDL_GPUTexture *texture) {
    std::lock_guard lock(blingus_mutexus_biggus_problemus);
    if (textures_in_use.find(texture) != textures_in_use.end())
      textures_to_release.push_back(texture);
  }

  void release_textures() {
    std::lock_guard lock(blingus_mutexus_biggus_problemus);
    for (auto texture : textures_to_release) {
      auto it = textures_in_use.find(texture);
      if (it != textures_in_use.end()) {
        SDL_ReleaseGPUTexture(device, texture);
        textures_in_use.erase(it);
      }
    }
    textures_to_release.clear();
  }
  size_t texture_count() const { return textures_in_use.size(); }
  size_t pending_release_count() const { return textures_to_release.size(); }
  const std::unordered_set<SDL_GPUTexture *> &textures() const {
    return textures_in_use;
  }

private:
  std::mutex blingus_mutexus_biggus_problemus;
  std::unordered_set<SDL_GPUTexture *> textures_in_use;
  std::vector<SDL_GPUTexture *> textures_to_release;
  SDL_GPUDevice *device;
};
