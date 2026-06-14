#include "include/components.h"
#include "include/game.h"
#include <SDL3/SDL_gpu.h>
#include <cstdint>
#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>
#include <functional>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float4.hpp>
#include <imgui.h>
#include <memory>
#include <string>
#include <vector>

// To add a new filter, add one line to make_component_filters():
//   { "Label", "B", colour, [](entt::entity e, std::shared_ptr<entt::registry>
//   r){ return r->try_get<YourComponent>(e) != nullptr; } }
struct ComponentFilter {
  std::string label; // shown in the filter combo
  std::string badge; // short tag shown inline on tree nodes
  ImVec4 colour;     // badge colour
  std::function<bool(entt::entity, std::shared_ptr<entt::registry>)>
      has_component;
  bool enabled{false};
};

static std::vector<ComponentFilter> make_component_filters() {
  return {
      {"Renderable",
       "R",
       {0.35f, 0.75f, 1.00f, 1.f},
       [](entt::entity e, std::shared_ptr<entt::registry> r) {
         return r->try_get<RenderableComponent>(e) != nullptr;
       }},
      {"Transform",
       "T",
       {0.75f, 0.75f, 0.75f, 1.f},
       [](entt::entity e, std::shared_ptr<entt::registry> r) {
         return r->try_get<LocalTransformComponent>(e) != nullptr;
       }},
      {"Camera",
       "C",
       {1.00f, 0.85f, 0.20f, 1.f},
       [](entt::entity e, std::shared_ptr<entt::registry> r) {
         return r->try_get<CameraComponent>(e) != nullptr;
       }},
      {"Light",
       "L",
       {1.00f, 0.95f, 0.50f, 1.f},
       [](entt::entity e, std::shared_ptr<entt::registry> r) {
         return r->try_get<DirectionalLightComponent>(e) != nullptr;
       }},
      {"Animation",
       "A",
       {0.75f, 0.45f, 1.00f, 1.f},
       [](entt::entity e, std::shared_ptr<entt::registry> r) {
         return r->try_get<AnimationComponent>(e) != nullptr;
       }},
  };
}

struct SceneGraphFilter {
  char search[128]{};
  std::vector<ComponentFilter> components{make_component_filters()};

  bool any_active() const {
    if (search[0] != '\0')
      return true;
    for (const auto &f : components)
      if (f.enabled)
        return true;
    return false;
  }

  void reset() {
    search[0] = '\0';
    for (auto &f : components)
      f.enabled = false;
  }
};

static bool entity_matches(entt::entity e,
                           std::shared_ptr<entt::registry> registry,
                           const SceneGraphFilter &f) {
  for (const auto &cf : f.components)
    if (cf.enabled && !cf.has_component(e, registry))
      return false;

  if (f.search[0] != '\0') {
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%d", entt::to_integral(e));
    if (!strstr(id_str, f.search))
      return false;
  }
  return true;
}

static bool subtree_has_match(entt::entity e,
                              std::shared_ptr<entt::registry> registry,
                              const SceneGraphFilter &f) {
  if (entity_matches(e, registry, f))
    return true;
  auto *sg = registry->try_get<SceneGraphRelationshipComponent>(e);
  if (!sg)
    return false;
  entt::entity child = sg->first_child;
  while (child != entt::null) {
    if (subtree_has_match(child, registry, f))
      return true;
    auto *cr = registry->try_get<SceneGraphRelationshipComponent>(child);
    child = cr ? cr->next_sibling : entt::null;
  }
  return false;
}

static void draw_entity_badges(entt::entity e,
                               std::shared_ptr<entt::registry> registry,
                               const std::vector<ComponentFilter> &components) {
  ImGui::SameLine();
  ImGui::Text("[");
  ImGui::SameLine();
  bool first = true;
  for (const auto &cf : components) {
    if (!cf.has_component(e, registry))
      continue;
    if (!first)
      ImGui::SameLine(0, 2.f);
    ImGui::PushStyleColor(ImGuiCol_Text, cf.colour);
    ImGui::TextUnformatted(cf.badge.c_str());
    ImGui::PopStyleColor();
    first = false;
  }
  ImGui::SameLine();
  ImGui::Text("]");
}

static void draw_entity_node(entt::entity e,
                             std::shared_ptr<entt::registry> registry,
                             const SceneGraphFilter &filter,
                             entt::entity &selected, entt::entity &scroll_to) {
  auto *sg = registry->try_get<SceneGraphRelationshipComponent>(e);
  const bool has_children = sg && sg->first_child != entt::null;
  const bool filtering = filter.any_active();

  const bool self_matches = !filtering || entity_matches(e, registry, filter);
  const bool subtree_matches =
      !filtering || subtree_has_match(e, registry, filter);

  if (filtering && !subtree_matches)
    return;

  if (filtering && subtree_matches)
    ImGui::SetNextItemOpen(true, ImGuiCond_Always);

  ImGuiTreeNodeFlags flags =
      ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
  if (!has_children)
    flags |= ImGuiTreeNodeFlags_Leaf;
  if (e == selected)
    flags |= ImGuiTreeNodeFlags_Selected;

  if (filtering && !self_matches)
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  else if (filtering)
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.55f, 1.f));

  const bool open = ImGui::TreeNodeEx((void *)(intptr_t)entt::to_integral(e),
                                      flags, "Entity %d", entt::to_integral(e));

  if (filtering)
    ImGui::PopStyleColor();

  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    selected = e;

  draw_entity_badges(e, registry, filter.components);

  if (e == scroll_to) {
    ImGui::SetScrollHereY(0.25f);
    scroll_to = entt::null;
  }

  if (open) {
    if (sg) {
      entt::entity child = sg->first_child;
      while (child != entt::null) {
        auto *cr = registry->try_get<SceneGraphRelationshipComponent>(child);
        draw_entity_node(child, registry, filter, selected, scroll_to);
        child = cr ? cr->next_sibling : entt::null;
      }
    }
    ImGui::TreePop();
  }
}

static void draw_scene_graph_panel(std::shared_ptr<entt::registry> registry,
                                   entt::entity &selected) {
  static SceneGraphFilter filter;
  static entt::entity scroll_to = entt::null;

  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
  bool changed =
      ImGui::InputText("##search", filter.search, sizeof(filter.search));
  ImGui::SameLine();
  if (ImGui::Button("Clear")) {
    filter.reset();
    changed = true;
  }
  ImGui::SameLine();
  if (ImGui::BeginCombo("##filters",
                        filter.any_active() ? "Filters*" : "Filters",
                        ImGuiComboFlags_NoArrowButton)) {
    for (auto &cf : filter.components) {
      changed |= ImGui::Checkbox(cf.label.c_str(), &cf.enabled);
      const float pad = 4.f;
      float width = ImGui::CalcTextSize(cf.badge.c_str()).x + pad * 2.f;
      const float right_edge = ImGui::GetContentRegionAvail().x;
      ImGui::SameLine(right_edge - width);
      ImGui::PushStyleColor(ImGuiCol_Text, cf.colour);
      ImGui::TextUnformatted(cf.badge.c_str());
      ImGui::PopStyleColor();
    }
    ImGui::EndCombo();
  }

  // On change, find the first match to scroll to
  if (changed && filter.any_active()) {
    scroll_to = entt::null;
    std::function<entt::entity(entt::entity)> first_match =
        [&](entt::entity e) -> entt::entity {
      if (entity_matches(e, registry, filter))
        return e;
      auto *sg = registry->try_get<SceneGraphRelationshipComponent>(e);
      if (!sg)
        return entt::null;
      entt::entity child = sg->first_child;
      while (child != entt::null) {
        if (auto found = first_match(child); found != entt::null)
          return found;
        auto *cr = registry->try_get<SceneGraphRelationshipComponent>(child);
        child = cr ? cr->next_sibling : entt::null;
      }
      return entt::null;
    };
    for (auto [root] : registry->view<Anchor>().each()) {
      scroll_to = first_match(root);
      if (scroll_to != entt::null)
        break;
    }
  }

  ImGui::Separator();
  for (auto [entity] : registry->view<Anchor>().each())
    draw_entity_node(entity, registry, filter, selected, scroll_to);
}

static void draw_transform_tab(std::shared_ptr<entt::registry> registry,
                               entt::entity e) {
  auto *t = registry->try_get<LocalTransformComponent>(e);
  if (ImGui::BeginTable("##transform_table", 2,
                        ImGuiTableFlags_BordersInnerV |
                            ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("position");
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::DragFloat3("##pos", &t->position.x, 0.01f);

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("rotation");
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::DragFloat4("##rot", &t->rotation.x, 0.001f);

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("scale");
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::DragFloat3("##scl", &t->scale.x, 0.01f);

    ImGui::EndTable();
  }
}

static void draw_world_tab(std::shared_ptr<entt::registry> registry,
                           entt::entity e) {
  auto *wt = registry->try_get<WorldTransformComponent>(e);
  if (ImGui::BeginTable("##world_table", 4,
                        ImGuiTableFlags_BordersInnerV |
                            ImGuiTableFlags_BordersOuter |
                            ImGuiTableFlags_SizingStretchSame)) {
    for (int row = 0; row < 4; ++row) {
      ImGui::TableNextRow();
      for (int col = 0; col < 4; ++col) {
        ImGui::TableSetColumnIndex(col);
        ImGui::Text("%7.3f", wt->matrix[row][col]);
      }
    }
    ImGui::EndTable();
  }
}

static void draw_entity_bounding_box(std::shared_ptr<entt::registry> registry,
                                     entt::entity e, glm::mat4 proj_view) {
  // drawing bounding box on screen
  auto world_transform = registry->get<WorldTransformComponent>(e);
  auto renderable = registry->get<RenderableComponent>(e);
  glm::mat4 proj_view_model = proj_view * world_transform.matrix;

  glm::vec3 mn = renderable.model->bbox_min();
  glm::vec3 mx = renderable.model->bbox_max();

  struct Face {
    glm::vec3 corners[4];
  };

  ImVec4 y = {1.f, 1.f, 0.f, 0.35f};
  ImDrawList *bg = ImGui::GetBackgroundDrawList();
  ImGuiIO &io = ImGui::GetIO();

  Face faces[6] = {// +Y top
                   {
                       {{mn.x, mx.y, mn.z},
                        {mx.x, mx.y, mn.z},
                        {mx.x, mx.y, mx.z},
                        {mn.x, mx.y, mx.z}},
                   },
                   // -Y bottom (darkest)
                   {
                       {{mn.x, mn.y, mn.z},
                        {mx.x, mn.y, mn.z},
                        {mx.x, mn.y, mx.z},
                        {mn.x, mn.y, mx.z}},
                   },
                   // +X right
                   {
                       {{mx.x, mn.y, mn.z},
                        {mx.x, mx.y, mn.z},
                        {mx.x, mx.y, mx.z},
                        {mx.x, mn.y, mx.z}},
                   },
                   // -X left
                   {
                       {{mn.x, mn.y, mn.z},
                        {mn.x, mx.y, mn.z},
                        {mn.x, mx.y, mx.z},
                        {mn.x, mn.y, mx.z}},
                   },
                   // +Z front
                   {
                       {{mn.x, mn.y, mx.z},
                        {mx.x, mn.y, mx.z},
                        {mx.x, mx.y, mx.z},
                        {mn.x, mx.y, mx.z}},
                   },
                   // -Z back
                   {
                       {{mn.x, mn.y, mn.z},
                        {mx.x, mn.y, mn.z},
                        {mx.x, mx.y, mn.z},
                        {mn.x, mx.y, mn.z}},
                   }};

  auto project = [&](glm::vec3 c) -> std::pair<ImVec2, bool> {
    glm::vec4 clip = proj_view_model * glm::vec4(c, 1.0f);
    if (clip.w <= 0.0f)
      return {{}, false};
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return {{(ndc.x * 0.5f + 0.5f) * io.DisplaySize.x,
             (ndc.y * -0.5f + 0.5f) * io.DisplaySize.y},
            true};
  };

  ImU32 edge_col = ImGui::GetColorU32({1.f, 1.f, 0.f, 0.9f});
  for (auto &face : faces) {
    ImVec2 p[4];
    bool ok = true;
    for (int i = 0; i < 4; i++) {
      auto [px, valid] = project(face.corners[i]);
      if (!valid) {
        ok = false;
        break;
      }
      p[i] = px;
    }
    if (!ok)
      continue;
    bg->AddQuadFilled(p[0], p[1], p[2], p[3], ImGui::GetColorU32(y));
    bg->AddQuad(p[0], p[1], p[2], p[3], edge_col, 1.5f);
  }
}

static void draw_renderable_tab(std::shared_ptr<entt::registry> registry,
                                entt::entity e) {
  auto *r = registry->try_get<RenderableComponent>(e);
  if (r->model) {
    ImGui::Text("ptr:    %p", (void *)r->model.get());
    for (auto dc : r->model->draw_calls()) {
      ImGui::Text("Texture");
      ImGui::Image((ImTextureID)dc.texture, {512, 512});
    }
  } else {
    ImGui::TextDisabled("(no model)");
  }
}

static void draw_light_tab(std::shared_ptr<entt::registry> registry,
                           entt::entity e) {
  auto *dl = registry->try_get<DirectionalLightComponent>(e);
  ImGui::Checkbox("enabled", &dl->enabled);
  ImGui::DragFloat("intensity", &dl->intensity, 0.01f, 0.f, 100.f);
  ImGui::ColorEdit3("colour", &dl->colour.x);
}

static void draw_camera_tab(std::shared_ptr<entt::registry> registry,
                            entt::entity e) {

  auto *cam = registry->try_get<CameraComponent>(e);
  const char *types[] = {"Ortho", "Perspective"};
  int type_idx = (cam->type == ProjectionType::PERSPECTIVE) ? 1 : 0;
  if (ImGui::Combo("type", &type_idx, types, 2))
    cam->type =
        (type_idx == 1) ? ProjectionType::PERSPECTIVE : ProjectionType::ORTHO;

  float fov_deg = glm::degrees(cam->fov);
  if (ImGui::DragFloat("fov", &fov_deg, 0.1f, 1.f, 170.f))
    cam->fov = glm::radians(fov_deg);

  ImGui::DragFloat("near", &cam->near_plane, 0.1f, 0.01f, cam->far_plane);
  ImGui::DragFloat("far", &cam->far_plane, 1.f, cam->near_plane, 100000.f);

  if (cam->type == ProjectionType::ORTHO)
    ImGui::DragFloat("ortho_size", &cam->ortho_size, 1.f, 1.f, 10000.f);

  if (ImGui::Button("Switch")) {
    auto player = registry->view<Player>().front();
    registry->remove<Player>(player);
    registry->emplace<Player>(e);
  }
}

static void draw_animation_tab(std::shared_ptr<entt::registry> registry,
                               entt::entity e) {
  auto *anim = registry->try_get<AnimationComponent>(e);
  ImGui::Text("%zu animation(s)", anim->animations.size());
  ImGui::Separator();
  for (size_t i = 0; i < anim->animations.size(); ++i) {
    ImGui::Text("[%zu] %s", i, anim->animations[i].name.c_str());
  }
}

static void draw_tags_tab(bool has_player, bool has_anchor) {
  if (has_player)
    ImGui::TextColored({0.4f, 0.8f, 0.4f, 1.f}, "Player");
  if (has_anchor)
    ImGui::TextColored({0.4f, 0.8f, 0.4f, 1.f}, "Anchor");
}

static void
draw_entity_attributes_panel(std::shared_ptr<entt::registry> registry,
                             entt::entity selected) {
  if (selected == entt::null) {
    ImGui::TextDisabled("No entity selected.");
    return;
  }

  if (ImGui::BeginTabBar("##entity_tabs")) {
    if (auto *t = registry->try_get<LocalTransformComponent>(selected))
      if (ImGui::BeginTabItem("Transform")) {
        draw_transform_tab(registry, selected);
        ImGui::EndTabItem();
      }

    if (auto *wt = registry->try_get<WorldTransformComponent>(selected))
      if (ImGui::BeginTabItem("World")) {
        draw_world_tab(registry, selected);
        ImGui::EndTabItem();
      }

    if (auto *r = registry->try_get<RenderableComponent>(selected))
      if (ImGui::BeginTabItem("Renderable")) {
        draw_renderable_tab(registry, selected);
        ImGui::EndTabItem();
      }

    if (auto *dl = registry->try_get<DirectionalLightComponent>(selected))
      if (ImGui::BeginTabItem("Light")) {
        draw_light_tab(registry, selected);
        ImGui::EndTabItem();
      }

    if (auto *cam = registry->try_get<CameraComponent>(selected))
      if (ImGui::BeginTabItem("Camera")) {
        draw_camera_tab(registry, selected);
        ImGui::EndTabItem();
      }

    if (auto *anim = registry->try_get<AnimationComponent>(selected))
      if (ImGui::BeginTabItem("Animation")) {
        draw_animation_tab(registry, selected);
        ImGui::EndTabItem();
      }

    bool has_player = registry->all_of<Player>(selected);
    bool has_anchor = registry->all_of<Anchor>(selected);
    if (has_player || has_anchor)
      if (ImGui::BeginTabItem("Tags")) {
        draw_tags_tab(has_player, has_anchor);
        ImGui::EndTabItem();
      }

    ImGui::EndTabBar();
  }
}

void Game::draw_debug() {
  static entt::entity selected = entt::null;
  ImGui::Begin("Debug");
  ImGui::TextDisabled("%zu entities | %.2f FPS",
                      registry->storage<entt::entity>().size(),
                      ImGui::GetIO().Framerate);
  if (ImGui::BeginTabBar("Debugger Windows")) {
    if (ImGui::BeginTabItem("Scene Objects")) {
      if (ImGui::BeginChild("##sg_panel", ImVec2(0, 500),
                            ImGuiChildFlags_ResizeY)) {
        ImGui::SeparatorText("Scene Graph");
        draw_scene_graph_panel(registry, selected);
      }
      ImGui::EndChild();

      if (ImGui::BeginChild("##attr_panel", ImVec2(0, 0))) {
        if (selected != entt::null)
          ImGui::SeparatorText(
              ("Entity " + std::to_string(entt::to_integral(selected)))
                  .c_str());
        else
          ImGui::SeparatorText("Entity Attributes");
        draw_entity_attributes_panel(registry, selected);
      }
      ImGui::EndChild();

      if (registry->try_get<RenderableComponent>(selected)) {
        draw_entity_bounding_box(registry, selected, renderer.proj_view());
      }
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Resources")) {
      if (ImGui::CollapsingHeader("Textures")) {
        for (SDL_GPUTexture *texture : texture_manager_->textures()) {
          if (ImGui::TreeNode(texture, "Texture: %p", texture)) {
            ImGui::Image((ImTextureID)texture, {512, 512});
            if (ImGui::BeginTable("TextureUsers", 3,
                                  ImGuiTableFlags_Borders |
                                      ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_Resizable |
                                      ImGuiTableFlags_SizingStretchProp)) {
              ImGui::TableSetupColumn("#");
              ImGui::TableSetupColumn("Entity");
              ImGui::TableSetupColumn("Model");
              ImGui::TableHeadersRow();
              int i = 1;
              for (auto [e, renderable] :
                   registry->view<RenderableComponent>().each()) {
                for (auto dc : renderable.model->draw_calls()) {
                  if (dc.texture != texture)
                    continue;
                  ImGui::TableNextRow();
                  ImGui::TableSetColumnIndex(0);
                  ImGui::Text("%d", i++);
                  ImGui::TableSetColumnIndex(1);
                  ImGui::Text("%d", entt::to_integral(e));
                  ImGui::TableSetColumnIndex(2);
                  ImGui::TextUnformatted(renderable.model->name().c_str());
                }
              }
              ImGui::EndTable();
            }
            ImGui::TreePop();
          }
        }
      }
      if (ImGui::CollapsingHeader("ShadowMap")) {
        ImGui::Image((ImTextureID)renderer.shadow_map(), {512, 512});
      }
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}
