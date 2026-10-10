
#version 450


// Depth-only fragment shader. It has no color outputs and does nothing. The driver runs it after the rasterizer and early-z,
// so occluded fragments are still discarded before this code — but there is nothing here anyway
void main() { }
