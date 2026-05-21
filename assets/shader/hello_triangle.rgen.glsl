#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;
layout(set = 0, binding = 1) uniform UniformBuffer {
    mat4 viewInverse;
    mat4 projInverse;
    vec4 otherInfo;
};
layout(set = 0, binding = 2, rgba32f) uniform image2D storageImage;

layout(location = 0) rayPayloadEXT vec3 hitValue;

void main() {
    ivec2 launchID = ivec2(gl_LaunchIDEXT.xy);
    ivec2 launchSize = ivec2(gl_LaunchSizeEXT.xy);

    vec2 pixelCenter = vec2(launchID) + vec2(0.5, 0.5);
    vec2 inUV = pixelCenter / vec2(launchSize);
    vec2 d = inUV * 2.0 - 1.0;

    vec4 target = projInverse * vec4(d.x, d.y, 1.0, 1.0);

    vec3 origin = vec3(viewInverse * vec4(0.0, 0.0, 0.0, 1.0));
    vec3 direction = normalize(vec3(viewInverse * vec4(normalize(target.xyz), 0.0)));

    traceRayEXT(
        topLevelAS,
        gl_RayFlagsOpaqueEXT,
        0xff,
        0,
        0,
        0,
        origin,
        0.001,
        direction,
        10000.0,
        0
    );

    imageStore(storageImage, launchID, vec4(hitValue, 0.0));
}
