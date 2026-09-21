#version 460
#extension GL_EXT_ray_tracing : require

// The AO closest-hit *always* reports occlusion. Reached only via the AO hit group,
// which the primary chit selects through sbtRecordOffset = 1.
layout(location = 0) rayPayloadInEXT vec3 payload;

void main() {
    payload = vec3(1.0);      // 1.0 == occluded
}
