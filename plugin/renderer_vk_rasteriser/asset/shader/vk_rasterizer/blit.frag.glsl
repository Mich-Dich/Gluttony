
#version 460

layout(binding = 0) uniform sampler2D src_tex;
layout(location = 0) out vec4 out_color;



void main() {
    ivec2 size = textureSize(src_tex, 0);
    vec2 uv = gl_FragCoord.xy / vec2(size);
    out_color = texture(src_tex, uv);
}
