#pragma once

#include "include/cgltf.h"
#include "include/gpu_utils.h"

#include <SDL3/SDL_gpu.h>
#include <filesystem>
#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

struct Vertex {
  float x, y, z;
  float nx, ny, nz;
  float u, v;
};

enum class AlphaMode { OPAQUE, MASK, BLEND };

struct DrawCall {
  uint32_t index_offset;
  uint32_t index_count;
  SDL_GPUTexture *texture = nullptr;
  glm::vec4 base_colour{0.8f};
  AlphaMode alpha_mode;
};

class Model {
public:
  explicit Model(SDL_GPUDevice *device,
                 std::shared_ptr<TextureManager> tex_manager);

  ~Model();

  Model(const Model &) = delete;
  Model &operator=(const Model &) = delete;

  bool load_from_glb(const std::filesystem::path &path);
  bool load_from_cgltf_mesh(
      const cgltf_data *data, const cgltf_mesh *mesh,
      std::unordered_map<const cgltf_image *, SDL_GPUTexture *> &texture_cache);
  const std::string &name() const { return model_name; }

  void bind(SDL_GPURenderPass *pass) const;

  const std::vector<DrawCall> &draw_calls() const { return draw_calls_; }

  glm::vec3 bbox_min() const { return bbox_min_; }
  glm::vec3 bbox_max() const { return bbox_max_; }

private:
  bool create_buffers();
  bool upload_buffers();

  SDL_GPUDevice *device_;
  std::shared_ptr<TextureManager> tex_manager_;

  std::vector<Vertex> vertices_;
  std::vector<uint32_t> indices_;
  std::vector<DrawCall> draw_calls_;

  SDL_GPUBuffer *vertex_buffer_ = nullptr;
  SDL_GPUBuffer *index_buffer_ = nullptr;
  SDL_GPUTransferBuffer *transfer_buffer_ = nullptr;

  glm::vec3 bbox_min_{};
  glm::vec3 bbox_max_{};
  std::string model_name;
};
