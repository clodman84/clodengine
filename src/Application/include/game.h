#pragma once

#include "include/graphics_pipeline.h"
#include "include/renderer2.h"
#include "include/scene.h"

#include "entt/entt.hpp"
#include <SDL3/SDL.h>
#include <memory>

class Game {
public:
  Game(SDL_GPUDevice *device, SDL_Window *window,
       std::shared_ptr<TextureManager> texture_manager);
  ~Game();
  void init();
  void render_frame();

  float aspect() const {
    return (float)renderer.render_width() / renderer.render_height();
  }
  SDL_GPUTexture *render_target() const { return renderer.render_target(); }

  void advance_animations(float dt);
  void draw_debug();
  SceneLightBuffer gather_lights();
  void update_cameras();
  void handle_input(float dt);

private:
  float previous_frame_time = 0.;
  NewRenderer renderer;
  std::unique_ptr<Scene> level;
  std::shared_ptr<entt::registry> registry;
  SDL_GPUDevice *device_;
  SDL_Window *window_;
  std::shared_ptr<TextureManager> texture_manager_;
  bool mouse_captured_ = false;
  float fly_speed_ = 100.f; // units/sec, scaled by scroll
};
