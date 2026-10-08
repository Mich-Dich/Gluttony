
#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_scalar_block_layout : require

layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;
layout(set = 0, binding = 1) uniform CameraUBO {
    mat4  view_inv;
    mat4  proj_inv;
    vec4  sun_direction;
    vec4  sun_color;
    mat4  prev_view_proj;
    mat4  view_proj;
    uvec4 temporal;
} cam;

layout(set = 0, binding = 14, rgba16f) uniform image2D current_raw;
layout(location = 0) rayPayloadEXT vec4 payload;



void main() {

    const vec2 uv = vec2(gl_LaunchIDEXT.xy) / vec2(gl_LaunchSizeEXT.xy);
    const vec2 ndc = uv * 2.0 - 1.0;
    const vec4 origin = cam.view_inv * vec4(0, 0, 0, 1);
    const vec4 target = cam.proj_inv * vec4(ndc.x, ndc.y, 1, 1);
    const vec4 direction = cam.view_inv * vec4(normalize(target.xyz / target.w), 0);

    payload = vec4(0.0, 0.0, 0.0, 0.0);
    traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, 0xFF, 0, 0, 0, origin.xyz, 0.001, direction.xyz, 1000.0, 0);

    // Only writes the raw shaded colour. The temporal resolve is a separate compute pass that reads 
    // current_raw + gbuffer_* and produces the output
    const ivec2 pixel = ivec2(gl_LaunchIDEXT.xy);
    imageStore(current_raw, pixel, vec4(payload.xyz, 1.0));
}
