#include "include/scene.h"
#include "include/animations.h"
#include "include/cgltf.h"
#include "include/components.h"

#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>
#include <filesystem>
#include <glm/ext/scalar_constants.hpp>
#include <glm/ext/vector_float3.hpp>
#include <memory>

Scene::Scene(SDL_GPUDevice *device, SDL_Window *window,
             std::shared_ptr<TextureManager> texture_manager,
             std::shared_ptr<entt::registry> registry)
    : device_(device), window_(window), texture_manager_(texture_manager),
      registry_(registry) {
  SDL_Log("[Level] Level Object Created");
}

Scene::~Scene() { SDL_Log("[Level] Level Object Destroyed"); }

void Scene::attach_scene_node(entt::entity child, entt::entity parent) {
  auto &child_rel =
      registry_->get_or_emplace<SceneGraphRelationshipComponent>(child);
  auto &parent_rel =
      registry_->get_or_emplace<SceneGraphRelationshipComponent>(parent);

  // Detach from old parent first if needed
  if (child_rel.parent != entt::null)
    detach_scene_node(child);

  child_rel.parent = parent;

  // Prepend child into parent's child list
  child_rel.next_sibling = parent_rel.first_child;
  if (parent_rel.first_child != entt::null)
    registry_->get<SceneGraphRelationshipComponent>(parent_rel.first_child)
        .prev_sibling = child;
  parent_rel.first_child = child;
}

void Scene::detach_scene_node(entt::entity child) {
  auto &rel = registry_->get<SceneGraphRelationshipComponent>(child);
  if (rel.parent == entt::null)
    return;

  auto &parent_rel =
      registry_->get<SceneGraphRelationshipComponent>(rel.parent);

  if (rel.prev_sibling != entt::null)
    registry_->get<SceneGraphRelationshipComponent>(rel.prev_sibling)
        .next_sibling = rel.next_sibling;
  else
    parent_rel.first_child = rel.next_sibling; // was the first child

  if (rel.next_sibling != entt::null)
    registry_->get<SceneGraphRelationshipComponent>(rel.next_sibling)
        .prev_sibling = rel.prev_sibling;

  rel.parent = entt::null;
  rel.prev_sibling = entt::null;
  rel.next_sibling = entt::null;
}

void Scene::propagate_node(entt::entity e, const glm::mat4 &parent_world) {
  // Compute this node's world matrix
  glm::mat4 local = glm::mat4(1.f);
  if (auto *t = registry_->try_get<LocalTransformComponent>(e))
    local = t->matrix();

  glm::mat4 world = parent_world * local;
  registry_->get_or_emplace<WorldTransformComponent>(e).matrix = world;

  // Walk the linked-list of children
  if (auto *rel = registry_->try_get<SceneGraphRelationshipComponent>(e)) {
    entt::entity child = rel->first_child;
    while (child != entt::null) {
      auto &child_rel = registry_->get<SceneGraphRelationshipComponent>(child);
      propagate_node(child, world);
      child = child_rel.next_sibling;
    }
  }
}

void Scene::update_world_transforms() {
  registry_->view<SceneGraphRelationshipComponent>().each(
      [&](entt::entity e, const SceneGraphRelationshipComponent &rel) {
        if (rel.parent == entt::null)
          propagate_node(e, glm::mat4(1.f));
      });
}

void Scene::init(std::filesystem::path path_to_glb) {
  load_from_glb(path_to_glb.c_str());

  auto [ws_min, ws_max] = world_bounds();
  const glm::vec3 ws_center = (ws_min + ws_max) * 0.5f;
  const float ws_radius = glm::length(ws_max - ws_min) * 0.5f;

  entt::entity root = registry_->view<Anchor>().front();

  // Sun
  auto sun = registry_->create();
  registry_->emplace<Anchor>(sun);
  registry_->emplace<SceneGraphRelationshipComponent>(sun);
  registry_->emplace<WorldTransformComponent>(sun);

  auto &sun_transform = registry_->emplace<LocalTransformComponent>(sun);
  sun_transform.position =
      ws_center + glm::vec3(ws_radius, ws_radius * 2.f, ws_radius);
  sun_transform.look_at(ws_center);
  auto &light = registry_->emplace<DirectionalLightComponent>(sun);
  light.colour = {1, 1, 1};
  light.intensity = 1.f;
  light.enabled = true;
  auto &sun_cam = registry_->emplace<CameraComponent>(sun);
  sun_cam.near_plane = 0;
  sun_cam.far_plane = 5000;
  sun_cam.ortho_size = 400;

  // Player
  auto player = registry_->create();
  registry_->emplace<Player>(player);
  attach_scene_node(player, root);
  registry_->emplace<WorldTransformComponent>(player);
  auto &player_transform = registry_->emplace<LocalTransformComponent>(player);
  player_transform.position = ws_center + glm::vec3(0.f, ws_radius, 0.f);
  player_transform.look_at(ws_center);

  auto &camera = registry_->emplace<CameraComponent>(player);
  camera.type = ProjectionType::PERSPECTIVE;
}

static LocalTransformComponent node_to_local_transform(const cgltf_node *node) {
  LocalTransformComponent ltc;

  if (node->has_matrix) {
    glm::mat4 m;
    for (int col = 0; col < 4; ++col)
      for (int row = 0; row < 4; ++row)
        m[col][row] = node->matrix[col * 4 + row];

    // Extract translation from last column
    ltc.position = glm::vec3(m[3]);

    // Extract scale from column lengths
    ltc.scale.x = glm::length(glm::vec3(m[0]));
    ltc.scale.y = glm::length(glm::vec3(m[1]));
    ltc.scale.z = glm::length(glm::vec3(m[2]));

    // Remove scale to get pure rotation matrix
    glm::mat3 rot{
        glm::vec3(m[0]) / ltc.scale.x,
        glm::vec3(m[1]) / ltc.scale.y,
        glm::vec3(m[2]) / ltc.scale.z,
    };
    ltc.rotation = glm::normalize(glm::quat_cast(rot));

  } else {
    if (node->has_translation)
      ltc.position = {node->translation[0], node->translation[1],
                      node->translation[2]};

    if (node->has_rotation)
      ltc.rotation = glm::normalize(glm::quat(node->rotation[3], // w
                                              node->rotation[0], // x
                                              node->rotation[1], // y
                                              node->rotation[2]));
    if (node->has_scale)
      ltc.scale = {node->scale[0], node->scale[1], node->scale[2]};
  }

  return ltc;
}

entt::entity Scene::build_node(
    const cgltf_node *node, entt::entity parent,
    std::map<const cgltf_node *, entt::entity> &node_to_entity_map) {

  // Recursive node visitor.
  entt::entity e = registry_->create();
  node_to_entity_map[node] = e;
  registry_->emplace<LocalTransformComponent>(e, node_to_local_transform(node));

  if (node->mesh) {
    auto it = mesh_cache.find(node->mesh);
    if (it == mesh_cache.end()) {
      auto model = std::make_shared<Model>(device_, texture_manager_);
      if (model->load_from_cgltf_mesh(nullptr /*data unused*/, node->mesh,
                                      texture_cache)) {
        models_.push_back(model);
        mesh_cache[node->mesh] = model;
        it = mesh_cache.find(node->mesh);
      } else {
        SDL_Log("[Level] build_node: failed to load mesh '%s'",
                node->mesh->name ? node->mesh->name : "(unnamed)");
      }
    }
    if (it != mesh_cache.end())
      registry_->emplace<RenderableComponent>(e, it->second);
  }
  if (parent != entt::null)
    attach_scene_node(e, parent);

  // Recurse into children
  for (cgltf_size ci = 0; ci < node->children_count; ++ci)
    build_node(node->children[ci], e, node_to_entity_map);

  return e;
}

entt::entity Scene::load_from_glb(std::filesystem::path path) {
  cgltf_options options{};
  cgltf_data *data = nullptr;

  if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success) {
    SDL_Log("[Level] load_from_glb: cgltf_parse_file failed for %s",
            path.c_str());
    return entt::null;
  }

  if (cgltf_load_buffers(&options, data, path.c_str()) !=
      cgltf_result_success) {
    SDL_Log("[Level] load_from_glb: cgltf_load_buffers failed");
    cgltf_free(data);
    return entt::null;
  }

  // One anchor entity acts as the scene root
  entt::entity root = registry_->create();
  registry_->emplace<LocalTransformComponent>(root); // identity
  registry_->emplace<Anchor>(root);
  std::map<const cgltf_node *, entt::entity> node_to_entity_map;

  // Use the first glTF scene if present, otherwise walk every root node
  const cgltf_scene *scene =
      (data->scene != nullptr)
          ? data->scene
          : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
  if (scene) {
    for (cgltf_size ni = 0; ni < scene->nodes_count; ++ni)
      build_node(scene->nodes[ni], root, node_to_entity_map);
  } else {
    // No scenes defined visit every node that has no parent
    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
      const cgltf_node *node = &data->nodes[ni];
      if (node->parent == nullptr)
        build_node(node, root, node_to_entity_map);
    }
  }

  SDL_Log("[Level] load_from_glb: loaded '%s' - %zu models, root entity %u",
          path.c_str(), models_.size(), static_cast<uint32_t>(root));

  if (data->animations_count > 0) {
    auto &animation_list = registry_->emplace<AnimationComponent>(root);
    for (int i = 0; i < data->animations_count; i++) {
      Animation anime =
          Animation{}.from_gltf(data->animations[i], node_to_entity_map);
      animation_list.animations.emplace_back(std::move(anime));
    }
  }

  cgltf_free(data);
  update_world_transforms();
  return root;
}

std::pair<glm::vec3, glm::vec3> Scene::world_bounds() {
  glm::vec3 world_min(1e30f);
  glm::vec3 world_max(-1e30f);

  registry_->view<RenderableComponent, WorldTransformComponent>().each(
      [&](const RenderableComponent &rc, const WorldTransformComponent &wt) {
        const glm::vec3 lo = rc.model->bbox_min();
        const glm::vec3 hi = rc.model->bbox_max();

        // Transform all 8 corners of the local AABB into world space.
        // Can't just transform min/max directly the matrix may rotate/shear
        // them.
        for (int i = 0; i < 8; ++i) {
          glm::vec3 corner{
              (i & 1) ? hi.x : lo.x,
              (i & 2) ? hi.y : lo.y,
              (i & 4) ? hi.z : lo.z,
          };
          glm::vec3 world_corner =
              glm::vec3(wt.matrix * glm::vec4(corner, 1.f));
          world_min = glm::min(world_min, world_corner);
          world_max = glm::max(world_max, world_corner);
        }
      });

  return {world_min, world_max};
}
