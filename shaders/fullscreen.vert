#version 300 es
precision highp float;

// Generates a single fullscreen triangle covering NDC [-1, 1]^2 without a VBO.
void main() {
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
