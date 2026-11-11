#version 450

// "Triangle Density": separable gaussian (sigma = radius / 2). Pass 0 (horizontal) writes the intermediate R16F target;
// pass 1 (vertical) also maps the result to the heat colour and is alpha-blended over the flat grey scene_color

layout(set = 0, binding = 0) uniform sampler2D source;

layout(push_constant) uniform Push {
    int   radius;           // pixels, 0 = no blur
    int   vertical;         // 0 = horizontal pass, 1 = vertical pass + colour mapping
    float max_density;      // triangles per pixel shown as full red
    float _pad;
} pc;

layout(location = 0) out vec4 out_color;

// blue -> cyan -> green -> yellow -> red
vec3 heat(float t) {
    const vec3 c0 = vec3(0.05, 0.15, 0.85);
    const vec3 c1 = vec3(0.00, 0.75, 0.90);
    const vec3 c2 = vec3(0.10, 0.85, 0.20);
    const vec3 c3 = vec3(0.98, 0.85, 0.05);
    const vec3 c4 = vec3(0.95, 0.08, 0.05);
    t = clamp(t, 0.0, 1.0) * 4.0;
    if (t < 1.0) return mix(c0, c1, t);
    if (t < 2.0) return mix(c1, c2, t - 1.0);
    if (t < 3.0) return mix(c2, c3, t - 2.0);
    return mix(c3, c4, t - 3.0);
}

void main() {

    const ivec2 size = textureSize(source, 0);
    const ivec2 pixel = ivec2(gl_FragCoord.xy);
    const ivec2 axis = pc.vertical != 0 ? ivec2(0, 1) : ivec2(1, 0);

    float sum = 0.0;
    float weight_sum = 0.0;
    const int radius = clamp(pc.radius, 0, 32);
    const float sigma = max(float(radius) * 0.5, 0.5);
    for (int i = -radius; i <= radius; ++i) {
        const ivec2 p = clamp(pixel + axis * i, ivec2(0), size - 1);
        const float w = exp(-0.5 * float(i * i) / (sigma * sigma));
        sum += texelFetch(source, p, 0).r * w;
        weight_sum += w;
    }
    const float density = sum / weight_sum;

    if (pc.vertical == 0) {
        out_color = vec4(density, 0.0, 0.0, 0.0);
        return;
    }

    // Logarithmic: max_density = red, 10 stops below it = blue. Nothing there -> transparent (grey world shows)
    const float max_d = max(pc.max_density, 1e-4);
    const float min_d = max_d / 1024.0;
    if (density < min_d * 0.5) {
        out_color = vec4(0.0);
        return;
    }
    const float t = log2(max(density, min_d) / min_d) / 10.0;
    out_color = vec4(heat(t), 0.8 * smoothstep(0.0, 0.05, t) + 0.05);
}
