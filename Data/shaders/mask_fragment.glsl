#version 460

layout(location = 0) out vec4 FragColour;

layout(std140, set = 3, binding = 0) uniform UniformBlock {
    vec2 resolution;
};

void main() {
    FragColour = vec4(gl_FragCoord.xy, 0, 1);
}
