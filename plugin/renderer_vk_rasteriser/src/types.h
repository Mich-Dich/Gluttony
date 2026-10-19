
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    constexpr u32                                               MAX_CONCURRENT_FRAMES = 3;

    static constexpr u32                                        VERTEX_HEADROOM_MIN = 64 * 1024;
    
    static constexpr u32                                        INDEX_HEADROOM_MIN = 128 * 1024;

    static constexpr u32                                        MAX_INSTANCES = 4096;

    static constexpr u32                                        MAX_MESHES = 4096;

    static constexpr u32                                        MAX_SUBMESHES_PER_MESH = 32;

    static constexpr u32                                        MAX_SUBMESHES = MAX_MESHES * MAX_SUBMESHES_PER_MESH;

    static constexpr u32                                        HIZ_LEVEL_COUNT = 5;

    static constexpr u32                                        MAX_LIGHTS = 1024;

    // Clustered light culling -----------------------------------------------------------------------------------------

    static constexpr u32                                        CLUSTER_TILE_SIZE_X = 16;

    static constexpr u32                                        CLUSTER_TILE_SIZE_Y = 16;

    static constexpr u32                                        CLUSTER_DEPTH_SLICES = 24;

    // Per-cluster ceiling enforced by the culling shader. Dense clusters drop their excess
    static constexpr u32                                        MAX_LIGHTS_PER_CLUSTER = 256;

    // Hard ceiling on the shared light index list (independent of cluster count). 4 M entries = 16 MB
    static constexpr u32                                        MAX_CLUSTER_INDEX_ENTRIES = 4 * 1024 * 1024;

    // shadow ----------------------------------------------------------------------------------------------------------

    static constexpr u32                                        MAX_SHADOW_VIEWS = 48;                 // a point light = 6 views, a spot = 1

    static constexpr u32                                        SHADOW_COMMANDS_PER_VIEW = 4096;

    static constexpr u32                                        SHADOW_VIEW_STRIDE = 256;              // also used as a UBO slice per view, so keep it 256-aligned

    static constexpr u32                                        SHADOW_TILE_MIN = 128;

    static constexpr u32                                        SHADOW_SPOT_TILE_MAX = 1024;

    static constexpr u32                                        SHADOW_POINT_TILE_MAX = 512;

    static constexpr u32                                        SHADOW_TILE_GUARD = 2;                 // texels of guard band per side

    // materials & textures --------------------------------------------------------------------------------------------

    static constexpr u32                                        MAX_MATERIALS = 1024;

    static constexpr u32                                        TEXTURE_SLOT_WHITE = 0;                    // fallback: base colour, metal/rough, emissive, occlusion

    static constexpr u32                                        TEXTURE_SLOT_FLAT_NORMAL = 1;              // fallback: tangent-space normal (0.5, 0.5, 1)

    static constexpr u32                                        FIRST_FREE_TEXTURE_SLOT = 2;

    static constexpr u32                                        DEFAULT_MATERIAL_SLOT = 0;

    static constexpr u32                                        MATERIAL_POLL_INTERVAL = 30;               // frames between "did an asset change?" sweeps

    static constexpr u32                                        MATERIAL_TEXTURE_SLOTS = static_cast<u32>(GLT::asset::material::texture_slot::count);

    // Commands bucketed per material. Sized so a scene with heavy per-material batching still fits; overflow drops commands, never crashes
    static constexpr u32                                        MAX_COMMANDS_PER_MATERIAL = 256;

    // debug draw ------------------------------------------------------------------------------------------------------

    // Hard cap on vertices uploaded per frame. 128K = 4 MB/frame × 3 frames ≈ 12 MB total. Enough for ~5400 AABBs
    static constexpr u32                                        MAX_DEBUG_VERTICES = 131072;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct mesh_slot {
        GLT::asset::handle                                      asset{};
        u32                                                     vertex_offset = 0;      // in vertices
        u32                                                     vertex_count = 0;
        u32                                                     index_offset = 0;       // in indices
        u32                                                     index_count = 0;
        bool                                                    alive = false;
        std::vector<u32>                                        material_slots{};    // per submesh, SSBO slot (default)

        // local-space AABB, cached from mesh_asset::bounds at load time. Used by debug draws
        glm::vec3                                               local_min{0.0f};
        glm::vec3                                               local_max{0.0f};
    };


    // Discriminates the two fragment pipelines a material can carry
    enum class material_pipeline_kind : u8 {

        main = 0,
        depth,
        preview,
    };


    struct camera_ubo_data {
        glm::mat4                                               view_proj{1.0f};
        glm::mat4                                               view{1.0f};
        glm::vec4                                               camera_pos{0.0f};
        glm::vec4                                               frustum_planes[6]{};
        glm::uvec4                                              flags{0u};

        // for light culling. Kept at the end so pre-existing shaders keep their offsets
        glm::mat4                                               projection{1.0f};    
        glm::mat4                                               inv_projection{1.0f};
        glm::uvec4                                              cluster_dims{0u};       // (x = grid_x, y = grid_y, z = grid_z)
        glm::vec4                                               cluster_depth{0.0f};    // (x = near, y = far, z = render_w, w = render_h)

        // shadows. Appended so existing shaders keep their offsets
        glm::mat4   light_view_proj{1.0f};
        glm::vec4   shadow_params{0.0f};    // x = world units per texel, y = normal bias (texels), z = depth bias (NDC), w = 1 / resolution
    };
    static_assert(sizeof(camera_ubo_data) == 496);


    struct shadow_settings {

        bool                                                    enabled = true;
        u32                                                     resolution = 2048;          // fixed for now; changing it at runtime is not handled
        f32                                                     distance = 60.0f;           // how far from the camera shadows are fitted
        f32                                                     caster_extension = 100.0f;  // how far toward the sun casters are still included
        f32                                                     normal_bias = 1.5f;         // in texels
        f32                                                     depth_bias = 0.0005f;       // NDC units
        u32                                                     atlas_size = 4096;          // D32: 64 MB. 2048 = 16 MB
        u32                                                     max_local_lights = 8;
        f32                                                     local_distance = 80.0f;     // lights farther than this (beyond their radius) don't cast
        f32                                                     local_normal_bias = 1.5f;   // texels
        f32                                                     local_bias = 1.0f;          // texels, toward the light
    };


    struct shadow_state {

        bool                                                    active = false;
        glm::vec3                                               direction{0.f, -1.f, 0.f};
        glm::mat4                                               view_proj{1.0f};
        glm::vec4                                               params{0.0f};
    };


    // Per-instance data, written every frame by build_instance_buffer(). std430 aligned; matches InstanceData in the shaders
    struct gpu_instance_data {
        glm::mat4                                               model{1.0f};
        glm::mat4                                               normal_matrix{1.0f};
        u32                                                     mesh_index = 0;
        u32                                                     material_index = 0;
        u32                                                     _pad0 = 0;
        u32                                                     _pad1 = 0;
    };
    static_assert(sizeof(gpu_instance_data) == 144, "gpu_instance_data layout mismatch");


    // Persistent per-mesh data, written when a mesh is loaded. Indexed by the mesh slot index. Read by the cull compute shader
    struct gpu_mesh_data {
        glm::vec3                                               local_min{0.0f};
        u32                                                     first_submesh = 0;
        glm::vec3                                               local_max{0.0f};
        u32                                                     submesh_count = 0;
        u32                                                     vertex_offset = 0;
        u32                                                     index_offset = 0;
        u32                                                     _pad0 = 0;
        u32                                                     _pad1 = 0;
    };
    static_assert(sizeof(gpu_mesh_data) == 48, "gpu_mesh_data layout mismatch");


    // A contiguous run of instances in the packed instance buffer that all reference the same mesh. Produced by build_instance_buffer()
    struct instance_batch {
        GLT::asset::handle                                      mesh{};
        u32                                                     first_instance = 0;
        u32                                                     instance_count = 0;
    };


    // Persistent per-submesh data. `first_index` is relative to the mesh's index_offset. Read by the cull compute shaders
    struct gpu_submesh_data {
        u32                                                     first_index = 0;
        u32                                                     index_count = 0;
        u32                                                     material_index = 0;     // TODO: remove, DEAD
        u32                                                     _pad0 = 0;              // reserved: draw bucket / material template (Step 8b)
    };
    static_assert(sizeof(gpu_submesh_data) == 16, "gpu_submesh_data layout mismatch");


    // Per-light GPU data, uploaded every frame by update_lights(). Packed as 4 x vec4 for clean std430 alignment; no padding is
    // needed inside the struct, and the shader can read it as a straight array of [Light]
    struct gpu_light {
        glm::vec4                                               position_radius;        // xyz = world position, w = radius (point/spot)
        glm::vec4                                               color_intensity;        // rgb = linear color, a = intensity
        glm::vec4                                               direction_inner_cos;    // xyz = world direction, w = cos(inner cone angle)
        glm::vec4                                               params;                 // x = cos(outer cone angle), y = type, zw = reserved
    };
    static_assert(sizeof(gpu_light) == 64, "gpu_light layout mismatch");


    struct gpu_shadow_view {
        glm::mat4                                               view_proj{1.0f};
        glm::vec4                                               atlas_rect{0.0f};   // xy = uv offset in atlas, zw = uv scale. zw == 0 -> no tile (culled / didn't fit) -> treated as lit
        glm::vec4                                               params{0.0f};       // x = texel world size per unit distance, y = normal bias (texels), z = light-ward bias (texels), w = 1 / atlas size
        glm::vec4                                               sphere{0.0f};       // xyz = light position, w = radius
        glm::vec4                                               planes[6]{};        // frustum planes for the shadow cull
        glm::vec4                                               _pad[3]{};
    };
    static_assert(sizeof(gpu_shadow_view) == SHADOW_VIEW_STRIDE);


    struct local_shadow_tile {

        u32                                                     view = 0;
        u32                                                     x = 0;
        u32                                                     y = 0;
        u32                                                     size = 0;
    };

    // materials & textures --------------------------------------------------------------------------------------------

    struct texture_upload_mip {

        u64                                                     offset = 0;     // into the staging pixels
        u32                                                     width = 0;
        u32                                                     height = 0;
    };


    struct gpu_texture {

        util::allocated_image                                   image{};
        vk::ImageView                                           view = nullptr;
        GLT::asset::handle                                      asset{};
        u32                                                     ref_count = 0;
        bool                                                    alive = false;
    };


    struct material_slot {

        GLT::asset::handle                                      asset{};
        u32                                                     ref_count = 0;
        std::array<GLT::asset::handle, MATERIAL_TEXTURE_SLOTS>  texture_assets{};
        std::array<u32, MATERIAL_TEXTURE_SLOTS>                 texture_slots{};
        bool                                                    alive = false;

        // reflection-driven params
        util::allocated_buffer                                  params_ubo{};
        u32                                                     params_size{ 0 };

        // per-material pipelines + shader modules
        vk::ShaderModule                                        vertex_module = nullptr;
        vk::ShaderModule                                        fragment_module = nullptr;      // main pass
        vk::ShaderModule                                        depth_module = nullptr;         // depth prepass + shadow
        vk::Pipeline                                            pipeline = nullptr;
        vk::Pipeline                                            depth_pipeline = nullptr;
        GLT::asset::material::blend_mode                        blend{ GLT::asset::material::blend_mode::opaque };
        u64                                                     bytecode_hash = 0;
    };


    // Mirrors [struct Globals] in types.slang
    struct gpu_globals {
        // Timing
        f32             time = 0.0f;
        f32             delta_time = 0.0f;
        u32             frame_index = 0u;
        f32             _pad0 = 0.0f;

        // Viewport
        glm::vec2       viewport_size{0.0f};
        glm::vec2       render_size{0.0f};

        // Camera
        glm::vec3       camera_position{0.0f};
        f32             near_plane = 0.0f;
        glm::vec3       camera_forward{0.0f, 0.0f, -1.0f};
        f32             far_plane = 0.0f;
        glm::vec3       camera_right{1.0f, 0.0f, 0.0f};
        f32             _pad1 = 0.0f;
        glm::vec3       camera_up{0.0f, 1.0f, 0.0f};
        f32             _pad2 = 0.0f;

        // Matrices
        glm::mat4       view{1.0f};
        glm::mat4       projection{1.0f};
        glm::mat4       view_projection{1.0f};
        glm::mat4       inv_view{1.0f};
        glm::mat4       inv_projection{1.0f};
        glm::mat4       inv_view_projection{1.0f};
    };
    static_assert(sizeof(gpu_globals) == 480, "gpu_globals layout mismatch");

    // debug draw ------------------------------------------------------------------------------------------------------

    // Debug draw: interleaved vertex consumed by the debug shaders. 16-byte aligned so `vec4 color` sits on a
    // std430-friendly boundary (only matters if this ever becomes an SSBO input; for vertex buffers it's just niceness)
    struct debug_vertex {
        glm::vec3                                               position{0.0f};
        f32                                                     _pad = 0.0f;
        glm::vec4                                               color{1.0f};
    };
    static_assert(sizeof(debug_vertex) == 32, "debug_vertex layout mismatch");

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
