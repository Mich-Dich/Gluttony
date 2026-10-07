#version 460
#extension GL_EXT_ray_tracing : require

// AO miss and shadow miss. Writes 0.0 = unoccluded.
layout(location = 0) rayPayloadInEXT vec4 payload;

void main() {
    payload.x = 0.0;      // 0.0 == unoccluded
}
