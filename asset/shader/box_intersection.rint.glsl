#version 460
#extension GL_EXT_ray_tracing : require

// NO rayPayloadInEXT here - intersection shaders are not allowed to declare one.
hitAttributeEXT vec3 attribs;

void main() {
    // Box is centered at the origin with half-extent 1 (matches the C++ side).
    const vec3 boxMin = vec3(-1.0);
    const vec3 boxMax = vec3( 1.0);

    // IMPORTANT: object-space ray. The hardware AABB test happens in object space,
    // and reportIntersectionEXT's t is measured along this same ray. Using
    // gl_WorldRay*EXT here only works while the instance transform is identity.
    const vec3 o = gl_ObjectRayOriginEXT;
    const vec3 d = gl_ObjectRayDirectionEXT;

    // Slab test
    const vec3 inv_d = 1.0 / d;
    vec3 t0 = (boxMin - o) * inv_d;
    vec3 t1 = (boxMax - o) * inv_d;
    const vec3 tsmall = min(t0, t1);
    const vec3 tbig   = max(t0, t1);

    const float tmin = max(max(tsmall.x, tsmall.y), max(tsmall.z, gl_RayTminEXT));
    const float tmax = min(min(tbig.x,   tbig.y),   min(tbig.z,   gl_RayTmaxEXT));

    if (tmin > tmax)
        return;   // miss - do NOT report

    // Encode a colour by which face was hit (now in object space, so it rotates with the box)
    vec3 n = -sign(d) * step(tsmall.yzx, tsmall.xyz) * step(tsmall.zxy, tsmall.xyz);
    attribs = n * 0.5 + 0.5;

    reportIntersectionEXT(tmin, 0);
}
