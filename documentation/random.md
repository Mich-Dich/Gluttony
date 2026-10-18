
sudo chown -R mich:mich ~/workspace/Gluttony

validate SSH:
```bash
ssh -T git@github.com
```


# TODO
[ ] ensure asset registry is thread save
[ ] renderer: shift mesh loading to thread pool (should also load needed material from registry)
[x] bring already open asset editor to front instead of opening anew one for the asset!
[ ] update the asset path reference of an editor if the asset is moved
[x] let CMake compile even without internet connection (currently it fails)
[ ] Add a function to the plugins (new interface) that lets the plugin manager check if that plugin is even supported for the current users architecture (like my main renderer needs WR-TR)
[ ] render debug stuff in lines (light cones/radius, Colliders, AABBs, ...)
[ ] HiZ occlusion culling
[ ] Post-processing: Complete the render pipeline with tonemapping, bloom, etc.


# Existing Editors

- Image viewer
- Console / Log viewer
- Statistics / Profiler HUD


# Planned Editors

- Settings / Preferences Editor
- Plugin manager UI
- Dependency graph viewer
- Keybinding editor
- Theme / style editor





"generator": "Unix Makefiles",


















globals.slang:
```slang
import types;
[[vk::binding(9, 0)]] ConstantBuffer<Globals> globals;
```

helpers.slang:
```slang
import types;


vertex_output passthrough(vertex_input v) {

    vertex_output o;
    o.position_ws = v.position_ws;
    o.normal_ws   = v.normal_ws;
    o.tangent_ws  = v.tangent_ws;
    o.uv0         = v.uv0;
    o.uv1         = v.uv1;
    o.color       = v.color;
    return o;
}


surface_output make_default_surface(surface_input s) {

    surface_output o;
    o.base_color = float3(1.0, 1.0, 1.0);
    o.opacity    = 1.0;
    o.normal_ws  = s.normal_ws;
    o.metallic   = 0.0;
    o.roughness  = 0.5;
    o.emissive   = float3(0.0, 0.0, 0.0);
    o.ao         = 1.0;
    return o;
}


float4 sample_material(Texture2D t, SamplerState s, float2 uv) {

    return t.Sample(s, uv);
}


float4 sample_material_lod(Texture2D t, SamplerState s, float2 uv, float lod) {

    return t.SampleLevel(s, uv, lod);
}
```

material_contract_v0.slang:
```slang
module material_contract_v0;

__exported import types;
__exported import globals;
__exported import helpers;

static public const int MATERIAL_CONTRACT_VERSION = 0;
```

types.slang:
```slang

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
```
