#version 460

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

layout(set = 1, binding = 0) uniform UBO {
    mat4  proj_view;
    mat4  model;
} ubo;

void main() {
    vec4 world_pos = ubo.model * vec4(a_position, 1.0);
    gl_Position    = ubo.proj_view * world_pos;
}
