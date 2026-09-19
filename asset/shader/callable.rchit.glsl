#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 payload;

// Callable data travels through a dedicated channel (not the ray payload).
// The callee writes to the same location, and we read it back after the call.
layout(location = 0) callableDataEXT vec3 callResult;

void main() {
    // Sample uses GeometryIndex() to pick which callable to invoke.
    // In GLSL that's gl_GeometryIndexEXT (0 = left triangle, 1 = right triangle).
    const uint call_index = gl_GeometryIndexEXT;

    // Default in case the callable doesn't run (shouldn't happen, but be safe)
    callResult = vec3(0.0);

    // invoke callable at SBT index `call_index` (0 or 1), writing to location 0
    executeCallableEXT(call_index, 0);

    payload = callResult;
}
