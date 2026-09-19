#version 460
#extension GL_EXT_ray_tracing : require

struct CallableData {
    vec3 newColor;
};

layout(location = 1) callableDataInEXT CallableData callData;

bool checkerboard(float size) {
    uvec3 idx = gl_LaunchIDEXT;
    return (mod(float(idx.x), size) < size * 0.5) != (mod(float(idx.y), size) < size * 0.5);
}

void main() {
    const bool checker = checkerboard(16.0);
    callData.newColor = checker ? vec3(1.0, 1.0, 1.0) : vec3(1.0, 0.0, 0.0);   // white / red
}
