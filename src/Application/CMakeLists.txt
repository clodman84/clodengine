add_library(clodengine STATIC
    gpu_utils.cpp
    image.cpp
    model.cpp
    renderer.cpp
    game.cpp
    scene.cpp
    animations.cpp
    debug.cpp
    pipeline.cpp
    standard_pipeline.cpp
    shadow_pipeline.cpp
    renderer2.cpp
)

target_include_directories(clodengine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(clodengine PUBLIC
    clodengine_project_optimizations
    imgui::imgui
    SDL3::SDL3
    nlohmann_json::nlohmann_json
)

target_link_libraries(clodengine PRIVATE $<IF:$<TARGET_EXISTS:libjpeg-turbo::turbojpeg>,libjpeg-turbo::turbojpeg,libjpeg-turbo::turbojpeg-static>)

if(CLODENGINE_ENABLE_TRACY)
    target_link_libraries(clodengine PUBLIC Tracy::TracyClient)
    target_compile_definitions(clodengine PUBLIC TRACY_ENABLE)
endif()
