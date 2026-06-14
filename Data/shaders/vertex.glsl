#version 460

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec2 v_uv;
layout(location = 2) out vec3 v_world_pos;
layout(location = 3) out vec4 v_frag_pos_light_space;

layout(set = 1, binding = 0) uniform UBO {
    mat4  proj_view;
    mat4  light_space_proj_view;
    mat4  model;
    mat4  normal;
    vec4  base_colour;
    float has_texture;
    float time;
} ubo;

void main() {
    vec4 world_pos = ubo.model * vec4(a_position, 1.0);
    v_world_pos    = world_pos.xyz;
    v_normal       = normalize(mat3(ubo.normal) * a_normal);
    v_uv           = a_uv;
    v_frag_pos_light_space = ubo.light_space_proj_view * world_pos;
    gl_Position    = ubo.proj_view * world_pos;
}
