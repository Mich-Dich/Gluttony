#version 460

// Fullscreen triangle at the far plane (z = w): with LESS_OR_EQUAL depth testing against the scene depth it only
// covers pixels no geometry was drawn to

void main() {
    vec2 pos = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    gl_Position = vec4(pos, 1.0, 1.0);
}
