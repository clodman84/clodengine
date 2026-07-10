#version 460
layout(set = 2, binding = 0) uniform sampler2D jfa_source;
layout(set = 3, binding = 0) uniform TimeUBO {
    float time;
} ubo;
layout(location = 0) out vec4 FragColour;
layout(location = 0) in vec2 uv;

const vec3  GLOW_COLOUR      = vec3(0.914, 0.118, 0.388); // #e91e63
const float GLOW_RADIUS      = 64.0;
const float INV_GLOW_RADIUS  = 1.0 / GLOW_RADIUS;
const float GLOW_RADIUS2     = GLOW_RADIUS * GLOW_RADIUS;
const float PULSE_SPEED      = -10.0;
const float PULSE_FREQ       = 40.0;

void main() {
    vec2 my_uv = vec2(uv.x, 1.0 - uv.y);
    vec4 jfa_data = texture(jfa_source, my_uv);
    if (jfa_data.r < 0.0) discard;

    vec2 delta = jfa_data.rg - gl_FragCoord.xy;
    float d2 = dot(delta, delta);
    if (d2 >= GLOW_RADIUS2 || d2 < 0.0001) discard;

    float d = sqrt(d2);
    float norm = d * INV_GLOW_RADIUS;
    float norm2 = norm * norm;
    float fade = 1.0 - norm2;

    float t = norm + 0.2;
    float t2 = t * t;
    float phase = ubo.time * PULSE_SPEED + PULSE_FREQ * t2 * t2;
    float pulse = sin(phase);
    float edge_w = max(fwidth(pulse), 0.0001);
    float ring = smoothstep(0.0, edge_w, pulse);
    float glow = fade * ring;
    if (glow <= 0.0) discard;

    float u = 1.0 - norm;
    float u2 = u * u;
    vec3 tint = mix(GLOW_COLOUR, vec3(1.0), 0.5 * u2 * u2);
    FragColour = vec4(tint * glow, glow);
}
