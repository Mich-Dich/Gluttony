#version 460

// Sky background into the linear HDR scene colour: the sky cubemap along the view ray plus an analytic sun disk

layout(set = 0, binding = 0) uniform samplerCube sky;

layout(push_constant) uniform PushConstants {
    mat4 inv_view_proj;
    vec4 sun_dir;           // xyz towards the sun, w = cos(sun angular radius)
    vec4 sun_disk;          // rgb = disk radiance, w = viewport width
    vec4 params;            // x = viewport height
} pc;

layout(location = 0) out vec4 out_color;


void main() {

    const vec2 ndc = gl_FragCoord.xy / vec2(pc.sun_disk.w, pc.params.x) * 2.0 - 1.0;
    const vec4 near_h = pc.inv_view_proj * vec4(ndc, 0.0, 1.0);
    const vec4 far_h = pc.inv_view_proj * vec4(ndc, 1.0, 1.0);
    const vec3 dir = normalize(far_h.xyz / far_h.w - near_h.xyz / near_h.w);

    vec3 color = textureLod(sky, dir, 0.0).rgb;

    // Sun disk with a soft edge, faded out below the horizon
    const float cos_angle = dot(dir, normalize(pc.sun_dir.xyz));
    const float cos_radius = pc.sun_dir.w;
    const float edge = (1.0 - cos_radius) * 0.25;
    const float disk = smoothstep(cos_radius - edge, cos_radius + edge, cos_angle) * smoothstep(-0.01, 0.01, dir.y);
    color += pc.sun_disk.rgb * disk;

    out_color = vec4(min(color, vec3(65000.0)), 1.0);
}
