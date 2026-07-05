#pragma once
#include "entt/entt.hpp"
#include "include/gpu_utils.h"
#include "include/model.h"
#include <entt/entity/fwd.hpp>
#include <filesystem>
#include <memory>

class Scene {
public:
  Scene(SDL_GPUDevice *device, SDL_Window *window,
        std::shared_ptr<TextureManager> texture_manager,
        std::shared_ptr<entt::registry> registry);
  ~Scene();
  void init(std::filesystem::path path_to_glb);
  void attach_scene_node(entt::entity child, entt::entity parent);
  void detach_scene_node(entt::entity node);
  void update_world_transforms();
  entt::entity
  build_node(const cgltf_node *node, entt::entity parent,
             std::map<const cgltf_node *, entt::entity> &node_to_entity_map);

  void propagate_node(entt::entity e, const glm::mat4 &parent_world);
  entt::entity load_from_glb(std::filesystem::path path);
  std::pair<glm::vec3, glm::vec3> world_bounds();

private:
  std::vector<std::shared_ptr<Model>> models_;
  std::shared_ptr<entt::registry> registry_;
  SDL_GPUDevice *device_;
  SDL_Window *window_;

  std::map<const cgltf_mesh *, std::shared_ptr<Model>> mesh_cache;
  std::shared_ptr<TextureManager> texture_manager_;
  std::string scene_name = "default";
  std::unordered_map<const cgltf_image *, SDL_GPUTexture *> texture_cache;
};
