#include "include/game.h"
#include "include/components.h"
#include "include/graphics_pipeline.h"
#include "include/renderer2.h"
#include "include/scene.h"
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>
#include <entt/entity/fwd.hpp>
#include <glm/ext/vector_float3.hpp>
#include <imgui.h>
#include <memory>
#include <sys/types.h>
#include <vector>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

Game::Game(SDL_GPUDevice *device, SDL_Window *window,
           std::shared_ptr<TextureManager> texture_manager)
    : device_(device), window_(window), texture_manager_(texture_manager),
      renderer(device, window, texture_manager) {
  SDL_Log("[Game] Welcome to the jungle");
  registry = std::make_shared<entt::registry>();
  level = std::make_unique<Scene>(device, window, texture_manager, registry);
};

Animation make_rotation_animation(entt::entity target, float duration) {
  glm::quat keyframe_1 = glm::identity<glm::quat>();
  glm::quat keyframe_2 = glm::angleAxis(glm::pi<float>(), glm::vec3{0, 1, 0});
  glm::quat keyframe_3 =
      glm::angleAxis(glm::two_pi<float>(), glm::vec3{0, 1, 0});

  AnimationSampler<glm::quat> sampler{};
  sampler.interpolation_type = AnimationInterpolation::Linear;
  sampler.timestamps = {0.0f, duration / 2, duration};
  sampler.values = {keyframe_1, keyframe_2, keyframe_3};

  AnimationChannel channel{};
  channel.sampler_index = 0;
  channel.target = target;
  channel.path = AnimationPath::Rotation;

  Animation anim{};
  anim.name = "Rotation";
  anim.duration = duration;
  anim.time = 0.0f;
  anim.is_looping = true;
  anim.is_paused = false;
  anim.samplers.emplace_back(std::move(sampler));
  anim.channels.push_back(channel);

  return anim;
}

void Game::init() {
  renderer.init();
  level->init("./Data/assets/littlest_tokyo.glb");
  registry->view<Anchor, AnimationComponent>().each(
      [&](entt::entity e, AnimationComponent &anim_comp) {
        anim_comp.animations.push_back(make_rotation_animation(e, 20.0f));
      });
  SDL_Log("[Game] Game Initialised!");
}

Game::~Game() { SDL_Log("[Game] Game Destoyed!"); }

SceneLightBuffer Game::gather_lights() {
  SceneLightBuffer lights{};
  lights.ambient = 0.15; // Hardcoded ambient light is yucky
  for (auto [e, l, world] :
       registry->view<DirectionalLightComponent, WorldTransformComponent>()
           .each()) {
    if (!l.enabled)
      continue;
    if (lights.num_directional >= 4)
      break;
    lights.directional_lights[lights.num_directional++] = {
        -world.matrix[2], glm::vec4(l.colour * l.intensity, 0.f)};
  }
  return lights;
}

void Game::update_cameras() {
  auto view = registry->view<CameraComponent, WorldTransformComponent>();
  view.each([&](entt::entity e, CameraComponent &cam,
                const WorldTransformComponent &world) {
    cam.view = glm::inverse(world.matrix);
    switch (cam.type) {
    case ProjectionType::ORTHO: {
      const float h = cam.ortho_size;
      float w;
      if (registry->try_get<DirectionalLightComponent>(e))
        w = h * renderer.shadow_map_aspect();
      else
        w = h * aspect();
      cam.projection =
          glm::orthoRH_ZO(-w, w, -h, h, cam.near_plane, cam.far_plane);
      break;
    }
    case ProjectionType::PERSPECTIVE:
      cam.projection = glm::perspectiveRH_ZO(cam.fov, aspect(), cam.near_plane,
                                             cam.far_plane);
      break;
    }
  });
}

void Game::handle_input(float dt) {
  ImGuiIO &io = ImGui::GetIO();

  if (!io.WantCaptureMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    mouse_captured_ = true;
  if (mouse_captured_ && ImGui::IsKeyPressed(ImGuiKey_Escape, /*repeat=*/false))
    mouse_captured_ = false;
  SDL_SetWindowRelativeMouseMode(SDL_GetMouseFocus(), mouse_captured_);

  auto pv = registry->view<Player, CameraComponent, LocalTransformComponent>();
  if (pv.begin() == pv.end())
    return;
  auto player = pv.front();
  auto &ltc = pv.get<LocalTransformComponent>(player);

  if (!io.WantCaptureMouse && io.MouseWheel != 0.f) {
    fly_speed_ =
        std::clamp(fly_speed_ * (1.f + io.MouseWheel * 0.15f), 0.1f, 500.f);
  }

  if (!mouse_captured_)
    return;

  constexpr float kSensitivity = 0.0015f; // rad/pixel

  if (io.MouseDelta.x != 0.f || io.MouseDelta.y != 0.f) {
    const glm::vec3 fwd =
        glm::normalize(glm::vec3(ltc.rotation * glm::vec4(0, 0, -1, 0)));
    float cur_pitch = std::asin(std::clamp(fwd.y, -1.f, 1.f));
    float cur_yaw = std::atan2(-fwd.x, -fwd.z); // yaw around world Y

    cur_yaw -= io.MouseDelta.x * kSensitivity;
    cur_pitch -= io.MouseDelta.y * kSensitivity;
    cur_pitch = std::clamp(cur_pitch, glm::radians(-89.f), glm::radians(89.f));

    const glm::quat qyaw = glm::angleAxis(cur_yaw, glm::vec3(0, 1, 0));
    const glm::quat qpitch = glm::angleAxis(cur_pitch, glm::vec3(1, 0, 0));
    ltc.rotation = glm::normalize(qyaw * qpitch);
  }

  glm::vec3 move{0.f};

  const glm::vec3 fwd = glm::normalize(ltc.rotation * glm::vec3(0, 0, -1));
  const glm::vec3 right = glm::normalize(ltc.rotation * glm::vec3(1, 0, 0));
  const glm::vec3 up = glm::vec3(0, 1, 0); // world up for E/Q

  if (ImGui::IsKeyDown(ImGuiKey_W))
    move += fwd;
  if (ImGui::IsKeyDown(ImGuiKey_S))
    move -= fwd;
  if (ImGui::IsKeyDown(ImGuiKey_D))
    move += right;
  if (ImGui::IsKeyDown(ImGuiKey_A))
    move -= right;
  if (ImGui::IsKeyDown(ImGuiKey_E))
    move += up;
  if (ImGui::IsKeyDown(ImGuiKey_Q))
    move -= up;

  float speed = fly_speed_;
  if (ImGui::IsKeyDown(ImGuiKey_LeftShift))
    speed *= 3.f;
  if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
    speed *= 0.2f;

  if (glm::length(move) > 1e-4f)
    ltc.position += glm::normalize(move) * speed * dt;
}

void Game::render_frame() {
#ifdef TRACY_ENABLE
  ZoneScoped;
#endif
  float current_time = SDL_GetTicksNS() / 1e9f;
  float dt = current_time - previous_frame_time;
  previous_frame_time = current_time;

  handle_input(dt);
  advance_animations(dt);
  SceneLightBuffer lights = gather_lights();
  level->update_world_transforms();
  update_cameras();

  auto blah = registry->view<DirectionalLightComponent, CameraComponent>();
  auto &active_light = blah.get<CameraComponent>(blah.front());

  auto bleh = registry->view<Player, CameraComponent>();
  auto &active_camera = bleh.get<CameraComponent>(bleh.front());

  std::vector<RenderRequest> render_request;

  for (auto [e, renderable, transform] :
       registry->view<RenderableComponent, WorldTransformComponent>().each()) {
    render_request.push_back({renderable.model, transform});
  };

  renderer.render(render_request, lights, active_camera, active_light);
}

void Game::advance_animations(float dt) {
  registry->view<AnimationComponent>().each(
      [&](AnimationComponent &animation_comp) {
        for (auto &anime : animation_comp.animations) {
          if (anime.is_paused)
            continue;
          anime.time += dt;
          if (anime.is_looping && anime.time > anime.duration) {
            anime.time = fmod(anime.time, anime.duration);
          }
          for (const auto &channel : anime.channels) {
            auto &transform =
                registry->get<LocalTransformComponent>(channel.target);
            switch (channel.path) {
            case AnimationPath::Rotation: {
              auto &sampler = std::get<AnimationSampler<glm::quat>>(
                  anime.samplers[channel.sampler_index]);
              transform.rotation = sampler.sample(anime.time);
              break;
            }
            case AnimationPath::Translation: {
              auto &sampler = std::get<AnimationSampler<glm::vec3>>(
                  anime.samplers[channel.sampler_index]);
              transform.position = sampler.sample(anime.time);
              break;
            }
            case AnimationPath::Scale: {
              auto &sampler = std::get<AnimationSampler<glm::vec3>>(
                  anime.samplers[channel.sampler_index]);
              transform.scale = sampler.sample(anime.time);
              break;
            }
            }
          }
        }
      });
  level->update_world_transforms();
}
