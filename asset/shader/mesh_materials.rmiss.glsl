#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 payload;

void main() {

    const vec3  dir = normalize(gl_WorldRayDirectionEXT);
    const float t   = smoothstep(-0.1, 0.6, dir.y);
    const vec3 horizon = vec3(0.85, 0.75, 0.60);   // warm haze
    const vec3 zenith  = vec3(0.20, 0.40, 0.85);   // blue up top

    payload = mix(horizon, zenith, t);
}
