add_library(dopy_game STATIC
    gpu_utils.cpp
    image.cpp
    model.cpp
    renderer.cpp
    game.cpp
    scene.cpp
    animations.cpp
    debug.cpp
)

target_include_directories(dopy_game
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(dopy_game PUBLIC
    dopygame_project_optimizations
    imgui::imgui
    SDL3::SDL3
    nlohmann_json::nlohmann_json
)

target_link_libraries(dopy_game PRIVATE $<IF:$<TARGET_EXISTS:libjpeg-turbo::turbojpeg>,libjpeg-turbo::turbojpeg,libjpeg-turbo::turbojpeg-static>)

if(DOPYGAME_ENABLE_TRACY)
    target_link_libraries(dopy_game PUBLIC Tracy::TracyClient)
    target_compile_definitions(dopy_game PUBLIC TRACY_ENABLE)
endif()
