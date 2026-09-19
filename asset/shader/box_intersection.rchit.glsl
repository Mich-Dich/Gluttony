#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 payload;
hitAttributeEXT vec3 attribs;

void main() {
    payload = attribs;
}
