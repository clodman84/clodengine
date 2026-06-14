#include "include/model.h"
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <filesystem>
#include <unordered_map>

#define CGLTF_IMPLEMENTATION
#include "include/cgltf.h"
#include "include/stb_image.h"

#include <map>

Model::Model(SDL_GPUDevice *device, std::shared_ptr<TextureManager> tex_manager)
    : device_(device), tex_manager_(tex_manager) {
  SDL_Log("[Model] Model object made");
};

Model::~Model() {
  SDL_Log("[Model %s] Model object destroyed", model_name.c_str());
  for (auto draw_call : draw_calls_) {
    SDL_Log("[Model %s] Queing Texture for Release: %p", model_name.c_str(),
            draw_call.texture);
    tex_manager_->queue_destruction(draw_call.texture);
  }
  SDL_Log("[Model %s] Releasing GPU Buffers", model_name.c_str());
  SDL_ReleaseGPUBuffer(device_, vertex_buffer_);
  SDL_ReleaseGPUBuffer(device_, index_buffer_);
}

bool Model::load_from_glb(const std::filesystem::path &path) {
  cgltf_options options{};
  cgltf_data *data = nullptr;
  model_name = path.string();

  if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success) {
    SDL_Log("[Model %s] load_from_glb: cgltf_parse_file failed for %s",
            model_name.c_str(), path.c_str());
    return false;
  }

  if (cgltf_load_buffers(&options, data, path.c_str()) !=
      cgltf_result_success) {
    SDL_Log("[Model %s] load_from_glb: cgltf_load_buffers failed",
            model_name.c_str());
    cgltf_free(data);
    return false;
  }

  std::map<const cgltf_image *, SDL_GPUTexture *> texture_cache;
  auto get_or_upload_texture = [&](const cgltf_image *img) -> SDL_GPUTexture * {
    auto it = texture_cache.find(img);
    if (it != texture_cache.end())
      return it->second;

    SDL_GPUTexture *out = nullptr;

    if (img->buffer_view) {
      const uint8_t *encoded =
          static_cast<const uint8_t *>(img->buffer_view->buffer->data) +
          img->buffer_view->offset;
      const int encoded_size = static_cast<int>(img->buffer_view->size);
      int w = 0, h = 0;
      unsigned char *pixels =
          stbi_load_from_memory(encoded, encoded_size, &w, &h, nullptr, 4);
      if (pixels) {
        tex_manager_->upload_texture_data_to_gpu(pixels, w, h, &out);
        stbi_image_free(pixels);
      }
    } else if (img->uri) {
      SDL_Log("[Model %s] Loading from URI is not supported!",
              model_name.c_str());
    }

    texture_cache[img] = out;
    return out;
  };

  for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
    const cgltf_mesh &mesh = data->meshes[mi];

    for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi) {
      const cgltf_primitive &prim = mesh.primitives[pi];

      if (prim.type != cgltf_primitive_type_triangles)
        continue;

      const cgltf_accessor *pos_acc = nullptr;
      const cgltf_accessor *uv_acc = nullptr;

      for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
        const cgltf_attribute &attr = prim.attributes[ai];
        if (attr.type == cgltf_attribute_type_position)
          pos_acc = attr.data;
        else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0)
          uv_acc = attr.data;
      }

      if (!pos_acc) {
        SDL_Log("[Model %s] load_from_glb: primitive has no POSITION, skipping",
                model_name.c_str());
        continue;
      }

      DrawCall dc{};
      dc.base_colour = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);

      if (prim.material) {
        const auto &pbr = prim.material->pbr_metallic_roughness;
        dc.base_colour =
            glm::vec4(pbr.base_color_factor[0], pbr.base_color_factor[1],
                      pbr.base_color_factor[2], pbr.base_color_factor[3]);
        const cgltf_texture *tex = pbr.base_color_texture.texture;
        if (tex && tex->image)
          dc.texture = get_or_upload_texture(tex->image);

        switch (prim.material->alpha_mode) {
        case cgltf_alpha_mode_blend:
          dc.alpha_mode = AlphaMode::BLEND;
          break;
        case cgltf_alpha_mode_mask:
          dc.alpha_mode = AlphaMode::MASK;
          break;
        default:
          dc.alpha_mode = AlphaMode::OPAQUE;
        }
      }

      const uint32_t base_vertex = static_cast<uint32_t>(vertices_.size());

      // Push vertices normals zeroed, computed below
      for (cgltf_size i = 0; i < pos_acc->count; ++i) {
        float pos[3] = {0.f, 0.f, 0.f};
        float uv[2] = {0.f, 0.f};
        cgltf_accessor_read_float(pos_acc, i, pos, 3);
        if (uv_acc)
          cgltf_accessor_read_float(uv_acc, i, uv, 2);
        vertices_.push_back(
            {pos[0], pos[1], pos[2], 0.f, 0.f, 0.f, uv[0], uv[1]});
      }

      dc.index_offset = static_cast<uint32_t>(indices_.size());

      if (prim.indices) {
        dc.index_count = static_cast<uint32_t>(prim.indices->count);
        for (cgltf_size i = 0; i < prim.indices->count; ++i) {
          cgltf_uint idx = 0;
          cgltf_accessor_read_uint(prim.indices, i, &idx, 1);
          indices_.push_back(base_vertex + idx);
        }
      } else {
        dc.index_count = static_cast<uint32_t>(pos_acc->count);
        for (cgltf_size i = 0; i < pos_acc->count; ++i)
          indices_.push_back(base_vertex + static_cast<uint32_t>(i));
      }

      draw_calls_.push_back(dc);
    }
  }

  cgltf_free(data);

  if (vertices_.empty()) {
    SDL_Log("[Model %s] load_from_glb: no usable geometry in %s",
            model_name.c_str(), path.c_str());
    return false;
  }

  // Accumulate face normals into each vertex (smooth shading)
  for (size_t i = 0; i + 2 < indices_.size(); i += 3) {
    Vertex &v0 = vertices_[indices_[i]];
    Vertex &v1 = vertices_[indices_[i + 1]];
    Vertex &v2 = vertices_[indices_[i + 2]];

    const glm::vec3 p0{v0.x, v0.y, v0.z};
    const glm::vec3 p1{v1.x, v1.y, v1.z};
    const glm::vec3 p2{v2.x, v2.y, v2.z};

    // Area-weighted face normal — larger triangles contribute more
    const glm::vec3 n = glm::cross(p1 - p0, p2 - p0);

    v0.nx += n.x;
    v0.ny += n.y;
    v0.nz += n.z;
    v1.nx += n.x;
    v1.ny += n.y;
    v1.nz += n.z;
    v2.nx += n.x;
    v2.ny += n.y;
    v2.nz += n.z;
  }

  // Normalize accumulated normals
  for (Vertex &v : vertices_) {
    const glm::vec3 raw{v.nx, v.ny, v.nz};
    const glm::vec3 n = (glm::dot(raw, raw) > 1e-10f)
                            ? glm::normalize(raw)
                            : glm::vec3(0.f, 1.f, 0.f); // safe fallback
    v.nx = n.x;
    v.ny = n.y;
    v.nz = n.z;
  }
  // Compute bounding box
  bbox_min_ = glm::vec3(1e30f);
  bbox_max_ = glm::vec3(-1e30f);
  for (const auto &v : vertices_) {
    bbox_min_ = glm::min(bbox_min_, glm::vec3(v.x, v.y, v.z));
    bbox_max_ = glm::max(bbox_max_, glm::vec3(v.x, v.y, v.z));
  }

  const glm::vec3 center = (bbox_min_ + bbox_max_) * 0.5f;
  const glm::vec3 extent = bbox_max_ - bbox_min_;
  SDL_Log("[Model %s] load_from_glb: %ld vertices, %ld indices, %ld draw calls",
          model_name.c_str(), vertices_.size(), indices_.size(),
          draw_calls_.size());
  SDL_Log(
      "[Model %s] load_from_glb: bbox center=(%f, %f, %f), extent=(%f, %f, %f)",
      model_name.c_str(), center.x, center.y, center.z, extent.x, extent.y,
      extent.z);

  // HMMM should I make the user call these??
  create_buffers();
  upload_buffers();
  return true;
};

bool Model::load_from_cgltf_mesh(
    const cgltf_data *data, const cgltf_mesh *mesh,
    std::unordered_map<const cgltf_image *, SDL_GPUTexture *> &texture_cache) {

  model_name = mesh->name ? mesh->name : "(unnamed)";
  auto get_or_upload_texture = [&](const cgltf_image *img) -> SDL_GPUTexture * {
    auto it = texture_cache.find(img);
    if (it != texture_cache.end())
      return it->second;

    SDL_GPUTexture *out = nullptr;

    if (img->buffer_view) {
      const uint8_t *encoded =
          static_cast<const uint8_t *>(img->buffer_view->buffer->data) +
          img->buffer_view->offset;
      const int encoded_size = static_cast<int>(img->buffer_view->size);
      int w = 0, h = 0;
      unsigned char *pixels =
          stbi_load_from_memory(encoded, encoded_size, &w, &h, nullptr, 4);
      if (pixels) {
        tex_manager_->upload_texture_data_to_gpu(pixels, w, h, &out);
        stbi_image_free(pixels);
      }
    } else if (img->uri) {
      SDL_Log("[Model %s] URI textures not supported", model_name.c_str());
    }

    texture_cache[img] = out;
    return out;
  };

  for (cgltf_size pi = 0; pi < mesh->primitives_count; ++pi) {
    const cgltf_primitive &prim = mesh->primitives[pi];

    if (prim.type != cgltf_primitive_type_triangles)
      continue;

    const cgltf_accessor *pos_acc = nullptr;
    const cgltf_accessor *uv_acc = nullptr;

    for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
      const cgltf_attribute &attr = prim.attributes[ai];
      if (attr.type == cgltf_attribute_type_position)
        pos_acc = attr.data;
      else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0)
        uv_acc = attr.data;
    }

    if (!pos_acc) {
      SDL_Log("[Model %s] primitive %zu has no POSITION, skipping",
              model_name.c_str(), pi);
      continue;
    }

    DrawCall dc{};
    dc.base_colour = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);

    if (prim.material) {
      const auto &pbr = prim.material->pbr_metallic_roughness;
      dc.base_colour =
          glm::vec4(pbr.base_color_factor[0], pbr.base_color_factor[1],
                    pbr.base_color_factor[2], pbr.base_color_factor[3]);
      const cgltf_texture *tex = pbr.base_color_texture.texture;
      if (tex && tex->image)
        dc.texture = get_or_upload_texture(tex->image);
      switch (prim.material->alpha_mode) {
      case cgltf_alpha_mode_blend:
        dc.alpha_mode = AlphaMode::BLEND;
        break;
      case cgltf_alpha_mode_mask:
        dc.alpha_mode = AlphaMode::MASK;
        break;
      default:
        dc.alpha_mode = AlphaMode::OPAQUE;
      }
    }

    const uint32_t base_vertex = static_cast<uint32_t>(vertices_.size());

    for (cgltf_size i = 0; i < pos_acc->count; ++i) {
      float pos[3] = {}, uv[2] = {};
      cgltf_accessor_read_float(pos_acc, i, pos, 3);
      if (uv_acc)
        cgltf_accessor_read_float(uv_acc, i, uv, 2);
      vertices_.push_back(
          {pos[0], pos[1], pos[2], 0.f, 0.f, 0.f, uv[0], uv[1]});
    }

    dc.index_offset = static_cast<uint32_t>(indices_.size());

    if (prim.indices) {
      dc.index_count = static_cast<uint32_t>(prim.indices->count);
      for (cgltf_size i = 0; i < prim.indices->count; ++i) {
        cgltf_uint idx = 0;
        cgltf_accessor_read_uint(prim.indices, i, &idx, 1);
        indices_.push_back(base_vertex + idx);
      }
    } else {
      dc.index_count = static_cast<uint32_t>(pos_acc->count);
      for (cgltf_size i = 0; i < pos_acc->count; ++i)
        indices_.push_back(base_vertex + static_cast<uint32_t>(i));
    }

    draw_calls_.push_back(dc);
  }

  if (vertices_.empty()) {
    SDL_Log("[Model %s] load_from_cgltf_mesh: no usable geometry",
            model_name.c_str());
    return false;
  }

  for (size_t i = 0; i + 2 < indices_.size(); i += 3) {
    Vertex &v0 = vertices_[indices_[i]];
    Vertex &v1 = vertices_[indices_[i + 1]];
    Vertex &v2 = vertices_[indices_[i + 2]];
    const glm::vec3 n =
        glm::cross(glm::vec3(v1.x - v0.x, v1.y - v0.y, v1.z - v0.z),
                   glm::vec3(v2.x - v0.x, v2.y - v0.y, v2.z - v0.z));
    v0.nx += n.x;
    v0.ny += n.y;
    v0.nz += n.z;
    v1.nx += n.x;
    v1.ny += n.y;
    v1.nz += n.z;
    v2.nx += n.x;
    v2.ny += n.y;
    v2.nz += n.z;
  }
  for (Vertex &v : vertices_) {
    const glm::vec3 raw{v.nx, v.ny, v.nz};
    const glm::vec3 n = (glm::dot(raw, raw) > 1e-10f)
                            ? glm::normalize(raw)
                            : glm::vec3(0.f, 1.f, 0.f);
    v.nx = n.x;
    v.ny = n.y;
    v.nz = n.z;
  }

  bbox_min_ = glm::vec3(1e30f);
  bbox_max_ = glm::vec3(-1e30f);
  for (const Vertex &v : vertices_) {
    bbox_min_ = glm::min(bbox_min_, glm::vec3(v.x, v.y, v.z));
    bbox_max_ = glm::max(bbox_max_, glm::vec3(v.x, v.y, v.z));
  }

  SDL_Log("[Model %s] %zu verts, %zu indices, %zu draw calls",
          model_name.c_str(), vertices_.size(), indices_.size(),
          draw_calls_.size());

  create_buffers();
  upload_buffers();
  return true;
}

void Model::bind(SDL_GPURenderPass *pass) const {
  // Bind vertex + index buffers (shared across all draw calls)
  SDL_GPUBufferBinding vb_binding{};
  vb_binding.buffer = vertex_buffer_;
  vb_binding.offset = 0;
  SDL_BindGPUVertexBuffers(pass, 0, &vb_binding, 1);

  SDL_GPUBufferBinding ib_binding{};
  ib_binding.buffer = index_buffer_;
  ib_binding.offset = 0;
  SDL_BindGPUIndexBuffer(pass, &ib_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
}

bool Model::create_buffers() {
  const Uint32 vb_size = static_cast<Uint32>(vertices_.size() * sizeof(Vertex));
  const Uint32 ib_size =
      static_cast<Uint32>(indices_.size() * sizeof(uint32_t));
  const Uint32 xfer_size = vb_size + ib_size;

  SDL_GPUBufferCreateInfo buf_info{};
  buf_info.size = vb_size;
  buf_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
  vertex_buffer_ = SDL_CreateGPUBuffer(device_, &buf_info);
  if (!vertex_buffer_) {
    SDL_Log(
        "[Model %s] create_buffers: SDL_CreateGPUBuffer (vertex) failed: %s",
        model_name.c_str(), SDL_GetError());
    return false;
  }

  buf_info.size = ib_size;
  buf_info.usage = SDL_GPU_BUFFERUSAGE_INDEX;
  index_buffer_ = SDL_CreateGPUBuffer(device_, &buf_info);
  if (!index_buffer_) {
    SDL_Log("[Model %s] create_buffers: SDL_CreateGPUBuffer (index) failed: %s",
            model_name.c_str(), SDL_GetError());
    return false;
  }

  SDL_GPUTransferBufferCreateInfo xfer_info{};
  xfer_info.size = xfer_size;
  xfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transfer_buffer_ = SDL_CreateGPUTransferBuffer(device_, &xfer_info);
  if (!transfer_buffer_) {
    SDL_Log("[Model %s] create_buffers: SDL_CreateGPUTransferBuffer failed: %s",
            model_name.c_str(), SDL_GetError());
    return false;
  }
  return true;
}

bool Model::upload_buffers() {
  const Uint32 vb_size = static_cast<Uint32>(vertices_.size() * sizeof(Vertex));
  const Uint32 ib_size =
      static_cast<Uint32>(indices_.size() * sizeof(uint32_t));

  uint8_t *ptr = static_cast<uint8_t *>(
      SDL_MapGPUTransferBuffer(device_, transfer_buffer_, false));
  memcpy(ptr, vertices_.data(), vb_size);
  memcpy(ptr + vb_size, indices_.data(), ib_size);
  SDL_UnmapGPUTransferBuffer(device_, transfer_buffer_);

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device_);
  SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);

  SDL_GPUTransferBufferLocation src{};
  src.transfer_buffer = transfer_buffer_;

  SDL_GPUBufferRegion dst{};

  // Vertices
  src.offset = 0;
  dst.buffer = vertex_buffer_;
  dst.offset = 0;
  dst.size = vb_size;
  SDL_UploadToGPUBuffer(copy_pass, &src, &dst, false);

  // Indices
  src.offset = vb_size;
  dst.buffer = index_buffer_;
  dst.offset = 0;
  dst.size = ib_size;
  SDL_UploadToGPUBuffer(copy_pass, &src, &dst, false);

  SDL_EndGPUCopyPass(copy_pass);
  SDL_SubmitGPUCommandBuffer(cmd);
  return true;
}
