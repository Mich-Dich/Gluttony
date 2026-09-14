#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 hitValue;

void main() {
    // Get current pixel coordinates and screen size
    ivec2 pixel = ivec2(gl_LaunchIDEXT.xy);
    ivec2 size = ivec2(gl_LaunchSizeEXT.xy);
    
    // Define border thickness (in pixels)
    const int borderThickness = 2;
    
    // Check if pixel is within border region
    if (pixel.x < borderThickness || pixel.x >= size.x - borderThickness ||
        pixel.y < borderThickness || pixel.y >= size.y - borderThickness) {
        // Red border
        hitValue = vec3(1.0, 0.0, 0.0);
    } else {
        // Original dark blue background
        hitValue = vec3(0.0, 0.0, 0.2);
    }
}