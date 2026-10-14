

struct Globals {

    // Timing
    float           time;
    float           delta_time;
    uint            frame_index;
    float           _pad0;

    // Viewport
    float2          viewport_size;
    float2          render_size;

    // Camera
    float3          camera_position;
    float           near_plane;
    float3          camera_forward;
    float           far_plane;
    float3          camera_right;
    float           _pad1;
    float3          camera_up;
    float           _pad2;

    // Matrices
    float4x4        view;
    float4x4        projection;
    float4x4        view_projection;
    float4x4        inv_view;
    float4x4        inv_projection;
    float4x4        inv_view_projection;
};


struct vertex_input {

    float3          position_ws;
    float3          normal_ws;
    float4          tangent_ws;
    float2          uv0;
    float2          uv1;
    float3          color;
};


struct vertex_output {

    float3          position_ws;
    float3          normal_ws;
    float4          tangent_ws;
    float2          uv0;
    float2          uv1;
    float3          color;
};


struct surface_input {

    float3          position_ws;
    float3          normal_ws;
    float4          tangent_ws;
    float2          uv0;
    float2          uv1;
    float3          color;
    float3          view_dir_ws;
    float2          screen_uv;
    float           view_depth;
    float3          ddx_ws;
    float3          ddy_ws;
};


struct surface_output {

    float3          base_color;
    float           opacity;
    float3          normal_ws;
    float           metallic;
    float           roughness;
    float3          emissive;
    float           ao;
};


struct material_textures {

    Texture2D       base_color;
    Texture2D       metallic_roughness;
    Texture2D       normal;
    Texture2D       emissive;
    Texture2D       occlusion;
    Texture2D       height;
    SamplerState    sampler;
};
