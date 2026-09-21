#version 460
#extension GL_EXT_ray_tracing : require

// Reached only via the AO miss index (1). Reports "nothing hit" = unoccluded.
layout(location = 0) rayPayloadInEXT vec3 payload;

void main() {
    payload = vec3(0.0);      // 0.0 == unoccluded
}
