#version 460

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in vec3 v_world_pos;
layout(location = 3) in vec4 v_frag_pos_light_space;

layout(location = 0) out vec4 FragColour;

layout(set = 2, binding = 0) uniform sampler2D        u_texture;
layout(set = 2, binding = 1) uniform sampler2DShadow  u_shadow_map;

layout(std140, set = 3, binding = 0) uniform UniformBlock {
    mat4  proj_view;
    mat4  light_space_proj_view;
    mat4  model;
    mat4  normal_matrix;
    vec4  base_colour;
    float has_texture;
    float time;
};

struct DirectionalLight {
    vec4 direction;
    vec4 colour;
};

layout(std140, set = 3, binding = 1) uniform SceneLights {
    int              num_directional;
    int              num_point;
    float            ambient;
    float            _pad;
    DirectionalLight directional_lights[4];
};

float ShadowCalculation(vec4 fragPosLightSpace, vec3 n, vec3 l)
{
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;

    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    projCoords.y  = 1.0 - projCoords.y;
    if (projCoords.z > 1.0 || projCoords.z < 0.0)
        return 0.0;

    float bias = max(0.005 * (1.0 - dot(n, l)), 0.001);

    float shadow = 0.0;
    vec2 texel_size = 1.0 / vec2(textureSize(u_shadow_map, 0));
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 offset = vec2(x, y) * texel_size;
            shadow += texture(u_shadow_map, vec3(projCoords.xy + offset, projCoords.z - bias));
        }
    }
    shadow /= 9.0;
    return 1.0 - shadow;
}

void main() {
    vec4 albedo = has_texture > 0.5
        ? texture(u_texture, v_uv)
        : base_colour;

    vec3 n = normalize(v_normal);
    vec3 lighting = vec3(ambient);

    for (int i = 0; i < num_directional; i++) {
        vec3  l      = normalize(-directional_lights[i].direction.xyz);
        float diff   = max(dot(n, l), 0.0);
        float shadow = ShadowCalculation(v_frag_pos_light_space, n, l);
        lighting    += (1.0 - shadow) * diff * directional_lights[i].colour.rgb;
    }

    FragColour = vec4(albedo.rgb * lighting, albedo.a);
}
