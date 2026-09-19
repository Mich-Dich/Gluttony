#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_scalar_block_layout : require

layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;
layout(set = 0, binding = 1) uniform CameraUBO {
    mat4 view_inv;
    mat4 proj_inv;
} cam;
layout(set = 0, binding = 2, rgba8) uniform image2D out_image;

layout(location = 0) rayPayloadEXT vec3 payload;

void main() {
    const vec2 uv = vec2(gl_LaunchIDEXT.xy) / vec2(gl_LaunchSizeEXT.xy);
    const vec2 ndc = uv * 2.0 - 1.0;

    vec4 origin    = cam.view_inv * vec4(0, 0, 0, 1);
    vec4 target    = cam.proj_inv * vec4(ndc.x, ndc.y, 1, 1);
    vec4 direction = cam.view_inv * vec4(normalize(target.xyz / target.w), 0);

    payload = vec3(0.1, 0.1, 0.15);   // background

    traceRayEXT(topLevelAS,
        gl_RayFlagsOpaqueEXT, 0xFF, 0, 0, 0,
        origin.xyz, 0.001, direction.xyz, 1000.0, 0);

    imageStore(out_image, ivec2(gl_LaunchIDEXT.xy), vec4(payload, 1.0));
}