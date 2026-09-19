#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) callableDataInEXT vec3 result;

void main() {
    const ivec2 p = ivec2(gl_LaunchIDEXT.xy);
    const bool checker = ((p.x / 16) & 1) != ((p.y / 16) & 1);
    result = checker ? vec3(1.0, 1.0, 1.0) : vec3(1.0, 0.0, 0.0);
}
