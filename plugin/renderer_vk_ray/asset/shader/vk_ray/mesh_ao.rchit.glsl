#version 460
#extension GL_EXT_ray_tracing : require

// AO closest-hit and shadow closest-hit. Writes 1.0 = occluded.
layout(location = 0) rayPayloadInEXT vec4 payload;

void main() {
    payload.x = 1.0;      // 1.0 == occluded
}
