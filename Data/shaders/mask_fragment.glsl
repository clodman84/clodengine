#version 460

layout(location = 0) out vec4 FragColour;

layout(std140, set = 3, binding = 0) uniform UniformBlock {
    vec2 resolution;
};

void main() {
    vec2 uv = gl_FragCoord.xy / resolution;
    uv.y = 1 - uv.y; // is this even necessary lowkey, (for JFA) 
    FragColour = vec4(uv, 0, 1);
}
