#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_scalar_block_layout : require

layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;
layout(set = 0, binding = 1) uniform CameraUBO {
    mat4  view_inv;
    mat4  proj_inv;
    vec4  sun_direction;
    vec4  sun_color;
    uvec4 accum_params;    // x = samples already accumulated
} cam;
layout(set = 0, binding = 2, rgba8)   uniform image2D out_image;
layout(set = 0, binding = 8, rgba32f) uniform image2D accum_image;

// MUST match every rayPayloadInEXT declaration at location 0 across the pipeline.
layout(location = 0) rayPayloadEXT vec4 payload;

void main() {

    const vec2 uv  = vec2(gl_LaunchIDEXT.xy) / vec2(gl_LaunchSizeEXT.xy);
    const vec2 ndc = uv * 2.0 - 1.0;
    const vec4 origin    = cam.view_inv * vec4(0, 0, 0, 1);
    const vec4 target    = cam.proj_inv * vec4(ndc.x, ndc.y, 1, 1);
    const vec4 direction = cam.view_inv * vec4(normalize(target.xyz / target.w), 0);

    payload = vec4(0.0, 0.0, 0.0, 0.0);
    traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, 0xFF, 0, 0, 0,
        origin.xyz, 0.001, direction.xyz, 1000.0, 0);

    const ivec2 pixel   = ivec2(gl_LaunchIDEXT.xy);
    const vec4  current = vec4(payload.xyz, 1.0);

    // Running average. When sample_count is 0 the accum buffer holds nothing
    // meaningful (undefined or stale from a previous accumulation), so we skip
    // the read entirely and write `current` straight out.
    //
    // The weight 1/(n+1) is the standard unbiased running-mean estimator: after
    // N samples, the buffer is exactly the mean of those N samples. It's also
    // what makes the result converge: the weight falls off as 1/N, so later
    // samples barely perturb the estimate.
    vec4 result;
    const uint n = cam.accum_params.x;
    if (n == 0u) {
        result = current;
    } else {
        const vec4 prev = imageLoad(accum_image, pixel);
        result = mix(prev, current, 1.0 / float(n + 1u));
    }

    imageStore(accum_image, pixel, result);
    imageStore(out_image,   pixel, result);
}
