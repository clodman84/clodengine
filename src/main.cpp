#include "include/game.h"
#include "include/image.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>
#include <SDL3_shadercross/SDL_shadercross.h>

#include <glm/ext/vector_float3.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <memory>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

class Application {
public:
  Application(int argc, char *argv[]) : argc_(argc), argv_(argv) {}
  ~Application() { cleanup(); }

  int run() {
    if (!init())
      return 1;

    bool done = false;
    while (!done) {
#ifdef TRACY_ENABLE
      FrameMark;
#endif
      process_events(done);

      if (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) {
        SDL_Delay(10);
        continue;
      }

      render_frame();
    }
    return 0;
  }

private:
  int argc_;
  char **argv_;

  std::unique_ptr<Image> background_image;
  std::unique_ptr<Game> game;
  SDL_Window *window_ = nullptr;
  SDL_GPUDevice *gpu_device_ = nullptr;

  std::shared_ptr<TextureManager> texture_manager;

  bool init() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
      std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
      return false;
    }

    const float main_scale =
        SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    const SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE |
                                         SDL_WINDOW_HIDDEN |
                                         SDL_WINDOW_HIGH_PIXEL_DENSITY;
    window_ = SDL_CreateWindow("DoPy Game", (int)(1080 * main_scale),
                               (int)(800 * main_scale), window_flags);

    if (!window_) {
      std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
      return false;
    }

    gpu_device_ = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
            SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB,
        true, nullptr); // nullptr = let SDL pick the best available driver

    if (!gpu_device_) {
      std::cerr << "SDL_CreateGPUDevice failed: " << SDL_GetError()
                << std::endl;
      return false;
    }

    if (!SDL_ShaderCross_Init()) {
      std::cerr << "SDL_ShaderCross_Init failed\n";
      return false;
    }

    // SDL_SetWindowPosition is not supported on Wayland for normal windows
    // and causes SDL_ClaimWindowForGPUDevice to fail. Omitted intentionally.
    SDL_ShowWindow(window_);

    texture_manager =
        std::make_shared<TextureManager>(TextureManager{gpu_device_});

    if (!SDL_ClaimWindowForGPUDevice(gpu_device_, window_)) {
      std::cerr << "SDL_ClaimWindowForGPUDevice failed: " << SDL_GetError()
                << std::endl;
      return false;
    }

    SDL_SetGPUSwapchainParameters(gpu_device_, window_,
                                  SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                  SDL_GPU_PRESENTMODE_IMMEDIATE);

    game = std::make_unique<Game>(gpu_device_, window_, texture_manager);
    game->init();

    if (!init_imgui(main_scale))
      return false;

    return true;
  }

  bool init_imgui(float main_scale) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;
    style.FontSizeBase = 20.0f;

    if (!ImGui_ImplSDL3_InitForSDLGPU(window_))
      return false;

    ImGui_ImplSDLGPU3_InitInfo init_info = {};
    init_info.Device = gpu_device_;
    init_info.ColorTargetFormat =
        SDL_GetGPUSwapchainTextureFormat(gpu_device_, window_);
    init_info.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;

    if (!ImGui_ImplSDLGPU3_Init(&init_info))
      return false;

    ImFontConfig text_cfg;
    text_cfg.GlyphOffset = ImVec2(0.0f, 0.0f);

    ImFont *combined_font = io.Fonts->AddFontFromFileTTF(
        "./Data/Quantico-Regular.ttf", 20.0f, &text_cfg);
    if (!combined_font) {
      std::cerr << "Warning: Failed to load UI font: Quantico-Regular.ttf\n";
    }
    io.Fonts->Build();
    background_image =
        std::make_unique<Image>("./Data/logo.png", texture_manager);
    background_image->load_fullres();
    return true;
  }

  void cleanup() {
    if (gpu_device_)
      SDL_WaitForGPUIdle(gpu_device_);

    // GPU resources must be released before the device is destroyed.
    if (background_image)
      background_image->destroy_texture();
    if (texture_manager)
      texture_manager->release_textures();

    // ImGui GPU backend first (holds GPU resources), then platform, then
    // context.
    if (gpu_device_) {
      game.reset();
      ImGui_ImplSDLGPU3_Shutdown();
      ImGui_ImplSDL3_Shutdown();
      ImGui::DestroyContext();
      if (window_)
        SDL_ReleaseWindowFromGPUDevice(gpu_device_, window_);
      SDL_DestroyGPUDevice(gpu_device_);
    }
    if (window_)
      SDL_DestroyWindow(window_);
    SDL_Quit();
  }

  void process_events(bool &done) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) {
        done = true;
      }
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
          event.window.windowID == SDL_GetWindowID(window_)) {
        done = true;
      }
    }
  }

  void render_background() {
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##background", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoSavedSettings);

    float img_w = background_image->width;
    float img_h = background_image->height;
    float scale =
        std::min(io.DisplaySize.x / img_w, io.DisplaySize.y / img_h) * 0.8;
    ImVec2 scaled_size(img_w * scale, img_h * scale);
    ImVec2 offset((io.DisplaySize.x - scaled_size.x) * 0.5f,
                  (io.DisplaySize.y - scaled_size.y) * 0.5f + 20);
    ImGui::SetCursorPos(offset);
    ImGui::Image(background_image->texture, scaled_size);
    ImGui::End();
  }

  static void set_nearest_sampler_callback(const ImDrawList *,
                                           const ImDrawCmd *cmd) {
    auto *state = static_cast<ImGui_ImplSDLGPU3_RenderState *>(
        ImGui::GetPlatformIO().Renderer_RenderState);
    state->SamplerCurrent =
        cmd->UserCallbackData ? state->SamplerNearest : state->SamplerLinear;
  }

  void render_frame() {
    texture_manager->release_textures();
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    game->render_frame();
    ImGui::NewFrame();

    ImGuiIO &io = ImGui::GetIO();
    ImVec2 p0 = {0.f, 0.f};
    ImVec2 p1 = io.DisplaySize;

    ImDrawList *bg = ImGui::GetBackgroundDrawList();
    // bg->AddCallback(set_nearest_sampler_callback, (void *)1);
    bg->AddImage((ImTextureID)(uintptr_t)game->render_target(), p0, p1);
    // bg->AddCallback(set_nearest_sampler_callback, nullptr);

    game->draw_debug();
    ImGui::Render();
    submit_gpu_commands();
  }

  void submit_gpu_commands() {
    ImDrawData *draw_data = ImGui::GetDrawData();
    SDL_GPUCommandBuffer *cmd_buffer = SDL_AcquireGPUCommandBuffer(gpu_device_);
    if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
      return;

    SDL_GPUTexture *swapchain_tex = nullptr;
    SDL_WaitAndAcquireGPUSwapchainTexture(cmd_buffer, window_, &swapchain_tex,
                                          nullptr, nullptr);

    if (swapchain_tex) {
      ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, cmd_buffer);

      SDL_GPUColorTargetInfo target_info = {};
      target_info.texture = swapchain_tex;
      target_info.clear_color = SDL_FColor{0.f, 0.f, 0.f, 1.0f};
      target_info.load_op = SDL_GPU_LOADOP_CLEAR;
      target_info.store_op = SDL_GPU_STOREOP_STORE;

      SDL_GPURenderPass *render_pass =
          SDL_BeginGPURenderPass(cmd_buffer, &target_info, 1, nullptr);
      ImGui_ImplSDLGPU3_RenderDrawData(draw_data, cmd_buffer, render_pass);
      SDL_EndGPURenderPass(render_pass);
    }
    SDL_SubmitGPUCommandBuffer(cmd_buffer);
  }
};

int main(int argc, char *argv[]) {
  Application app(argc, argv);
  return app.run();
}
