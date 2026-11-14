#version 460

// Physically based sky (single scattering, Rayleigh + Mie, Nishita style raymarch) into a cubemap.
// Output is linear HDR radiance in engine units: the model is evaluated for a sun irradiance of 1 and scaled by
// pc.sun_radiance (= sun colour * intensity * PI * sky intensity, see scene_renderer_environment.inl).
// The sun DISK is not part of this cubemap (it is direct light and would only add noise to the IBL filtering); the
// background pass draws it analytically.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, rgba16f) uniform writeonly image2DArray out_sky;

layout(push_constant) uniform PushConstants {
    vec4 sun_dir;           // xyz: unit vector TOWARDS the sun (world space, +Y up)
    vec4 sun_radiance;      // rgb: scale applied to the unit-irradiance model
    vec4 params;            // x: mie (haze) scale, y: ground albedo, z: face size (texels), w: unused
} pc;

// Earth / atmosphere (metres)
const float EARTH_RADIUS = 6360e3;
const float ATMOSPHERE_RADIUS = 6420e3;
const float VIEWER_HEIGHT = 50.0;
const float RAYLEIGH_SCALE_HEIGHT = 8000.0;
const float MIE_SCALE_HEIGHT = 1200.0;
const vec3  RAYLEIGH_BETA = vec3(5.802e-6, 13.558e-6, 33.1e-6);
const float MIE_BETA = 3.996e-6;
const float MIE_EXTINCTION_RATIO = 1.11;
const float MIE_G = 0.8;
const float PI = 3.14159265359;

const uint VIEW_STEPS = 24u;
const uint LIGHT_STEPS = 8u;


// Vulkan cube face orientation, uv in [-1, 1]
vec3 cube_dir(uint face, vec2 uv) {
    switch (face) {
        case 0u: return normalize(vec3( 1.0, -uv.y, -uv.x));
        case 1u: return normalize(vec3(-1.0, -uv.y,  uv.x));
        case 2u: return normalize(vec3( uv.x,  1.0,  uv.y));
        case 3u: return normalize(vec3( uv.x, -1.0, -uv.y));
        case 4u: return normalize(vec3( uv.x, -uv.y,  1.0));
        default: return normalize(vec3(-uv.x, -uv.y, -1.0));
    }
}


// Distance along the ray to the far intersection with a sphere centred at the origin (-1 if missed)
float sphere_exit(vec3 origin, vec3 dir, float radius) {
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float d = b * b - c;
    if (d < 0.0) return -1.0;
    return -b + sqrt(d);
}


// Distance to the near intersection (ground hit), -1 if missed / behind
float sphere_enter(vec3 origin, vec3 dir, float radius) {
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float d = b * b - c;
    if (d < 0.0) return -1.0;
    float t = -b - sqrt(d);
    return t > 0.0 ? t : -1.0;
}


float phase_rayleigh(float mu) { return 3.0 / (16.0 * PI) * (1.0 + mu * mu); }


float phase_mie(float mu) {
    const float g2 = MIE_G * MIE_G;
    return 3.0 / (8.0 * PI) * ((1.0 - g2) * (1.0 + mu * mu)) / ((2.0 + g2) * pow(1.0 + g2 - 2.0 * MIE_G * mu, 1.5));
}


// Optical depth (rayleigh, mie) from p towards the sun; returns false if the earth blocks it
bool sun_optical_depth(vec3 p, vec3 sun, out vec2 depth) {
    depth = vec2(0.0);
    if (sphere_enter(p, sun, EARTH_RADIUS) > 0.0)
        return false;
    float t_max = sphere_exit(p, sun, ATMOSPHERE_RADIUS);
    float ds = t_max / float(LIGHT_STEPS);
    for (uint i = 0u; i < LIGHT_STEPS; ++i) {
        vec3 q = p + sun * (ds * (float(i) + 0.5));
        float h = length(q) - EARTH_RADIUS;
        depth += vec2(exp(-h / RAYLEIGH_SCALE_HEIGHT), exp(-h / MIE_SCALE_HEIGHT)) * ds;
    }
    return true;
}


vec3 extinction(vec2 depth, float mie_scale) {
    return exp(-(RAYLEIGH_BETA * depth.x + vec3(MIE_BETA * MIE_EXTINCTION_RATIO * mie_scale) * depth.y));
}


vec3 sky_radiance(vec3 dir, vec3 sun) {

    const float mie_scale = max(pc.params.x, 0.0);
    const vec3 origin = vec3(0.0, EARTH_RADIUS + VIEWER_HEIGHT, 0.0);

    float t_ground = sphere_enter(origin, dir, EARTH_RADIUS);
    float t_max = t_ground > 0.0 ? t_ground : sphere_exit(origin, dir, ATMOSPHERE_RADIUS);
    float ds = t_max / float(VIEW_STEPS);

    const float mu = dot(dir, sun);
    const float pr = phase_rayleigh(mu);
    const float pm = phase_mie(mu);

    vec3 rayleigh = vec3(0.0);
    vec3 mie = vec3(0.0);
    vec2 view_depth = vec2(0.0);

    for (uint i = 0u; i < VIEW_STEPS; ++i) {
        vec3 p = origin + dir * (ds * (float(i) + 0.5));
        float h = max(length(p) - EARTH_RADIUS, 0.0);
        vec2 density = vec2(exp(-h / RAYLEIGH_SCALE_HEIGHT), exp(-h / MIE_SCALE_HEIGHT)) * ds;
        view_depth += density;

        vec2 light_depth;
        if (!sun_optical_depth(p, sun, light_depth))
            continue;
        vec3 transmittance = extinction(view_depth + light_depth, mie_scale);
        rayleigh += transmittance * density.x;
        mie += transmittance * density.y;
    }

    vec3 radiance = rayleigh * RAYLEIGH_BETA * pr + mie * (MIE_BETA * mie_scale) * pm;

    // Ground: lambert lit by the attenuated sun (+ a little sky light), seen through the atmosphere in front of it
    if (t_ground > 0.0) {
        vec3 p = origin + dir * t_ground;
        vec3 n = normalize(p);
        vec2 light_depth;
        vec3 sun_at_ground = sun_optical_depth(p + n * 1.0, sun, light_depth) ? extinction(light_depth, mie_scale) : vec3(0.0);
        float albedo = clamp(pc.params.y, 0.0, 1.0);
        vec3 ground = albedo / PI * (sun_at_ground * max(dot(n, sun), 0.0) + 0.1 * max(sun.y + 0.1, 0.0));
        radiance += ground * extinction(view_depth, mie_scale);
    }

    return radiance;
}


void main() {

    const uint size = uint(pc.params.z);
    const uvec3 id = gl_GlobalInvocationID;
    if (id.x >= size || id.y >= size)
        return;

    const vec2 uv = (vec2(id.xy) + 0.5) / float(size) * 2.0 - 1.0;
    const vec3 dir = cube_dir(id.z, uv);
    const vec3 sun = normalize(pc.sun_dir.xyz);

    vec3 radiance = sky_radiance(dir, sun) * pc.sun_radiance.rgb;
    imageStore(out_sky, ivec3(id), vec4(min(radiance, vec3(65000.0)), 1.0));
}
