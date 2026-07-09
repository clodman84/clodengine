#version 460
layout(set = 2, binding = 0) uniform sampler2D jfa_source;

layout(set = 3, binding = 0) uniform TimeUBO {
    float time;
} ubo;

layout(location = 0) out vec4 FragColour;
layout(location = 0) in vec2 uv;

const vec3 GLOW_COLOUR = vec3(0.914, 0.118, 0.388); // #e91e63
const float GLOW_RADIUS  = 64.0;

const float PULSE_SPEED = -10.0;
const float PULSE_FREQ  = 40.0;

void main() {
    vec2 my_uv = vec2(uv.x, 1 - uv.y);
    vec4 jfa_data = texture(jfa_source, my_uv);

    if (jfa_data.r < 0.0) {
        discard;
    }

    float d = distance(jfa_data.rg, gl_FragCoord.xy);

    if (d < 0.0001) {
        discard;
    }

    float norm = d / GLOW_RADIUS;
    if (norm >= 1.0) {
        discard;
    }

    float fade = 1.0 - pow(norm, 2.0);

    float phase = ubo.time * PULSE_SPEED + PULSE_FREQ * pow(norm + 0.2, 4.0);
    float pulse = sin(phase);

    float edge_w = max(fwidth(pulse), 0.0001);
    float ring = smoothstep(0.0, edge_w, pulse);

    float glow = fade * ring;
    if (glow <= 0.0) {
        discard;
    }

    vec3 tint = mix(GLOW_COLOUR, vec3(1.0), 0.5 * pow(1.0 - norm, 4.0));

    FragColour = vec4(tint * glow, glow);
}
