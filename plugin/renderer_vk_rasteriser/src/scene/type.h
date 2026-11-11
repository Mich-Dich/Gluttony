#pragma once

#include "util/pch.h"
#include "util/buffer.h"
#include "world/camera_snapshot.h"
#include "world/light_snapshot.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // frame -----------------------------------------------------------------------------------------------------------

    constexpr u32                                       MAX_CONCURRENT_FRAMES = 3;

    // per-scene scene state -------------------------------------------------------------------------------------------

    // Hard sanity cap only. Instance / material-table / draw-command buffers grow on demand (power of two, starting at the
    // INITIAL_* sizes below); old buffers are retired, so growing never stalls the GPU
    constexpr u32                                       MAX_INSTANCES = 1u << 20;

    // firstInstance of every mesh draw = instance index | LOD info (cull.comp.glsl, material vertex shader)
    constexpr u32                                       INSTANCE_INDEX_MASK = (1u << 20) - 1u;
    constexpr u32                                       INSTANCE_LOD_SHIFT = 20;            // 4 bits
    constexpr u32                                       INSTANCE_HAS_LODS = 1u << 24;       // mesh has generated levels
    constexpr u32                                       INSTANCE_PROCEDURAL = 1u << 25;     // procedural mesh (debug colouring)
    static_assert(MAX_INSTANCES - 1u <= INSTANCE_INDEX_MASK, "instance index must fit the firstInstance encoding");

    constexpr u32                                       INITIAL_INSTANCE_CAPACITY = 4096;

    constexpr u32                                       INITIAL_DRAW_CAPACITY = 4096;       // draw commands (instance x submesh)

    constexpr u32                                       MAX_LIGHTS = 1024;


    // clustered lighting ----------------------------------------------------------------------------------------------

    constexpr u32                                       CLUSTER_TILE_SIZE_X = 16;

    constexpr u32                                       CLUSTER_TILE_SIZE_Y = 16;

    // Environment (sky + image based lighting)
    constexpr u32                                       ENV_SKY_SIZE = 128;         // texels per face of the raymarched sky cube
    constexpr u32                                       ENV_SPECULAR_SIZE = 64;     // mip 0 of the prefiltered cube
    constexpr u32                                       ENV_SPECULAR_MIPS = 5;      // 64 .. 4, roughness = mip / (mips - 1)
    constexpr u32                                       ENV_PREFILTER_SAMPLES = 128;

    constexpr u32                                       CLUSTER_DEPTH_SLICES = 24;

    constexpr u32                                       MAX_LIGHTS_PER_CLUSTER = 256;

    constexpr u32                                       MAX_CLUSTER_INDEX_ENTRIES = 4 * 1024 * 1024;

    // shadow ----------------------------------------------------------------------------------------------------------

    constexpr u32                                       MAX_SHADOW_VIEWS = 48;

    constexpr u32                                       SHADOW_VIEW_STRIDE = 256;

    constexpr u32                                       SHADOW_TILE_MIN = 128;

    constexpr u32                                       SHADOW_SPOT_TILE_MAX = 1024;

    constexpr u32                                       SHADOW_POINT_TILE_MAX = 512;

    constexpr u32                                       SHADOW_TILE_GUARD = 2;

    // debug draw ------------------------------------------------------------------------------------------------------

    constexpr u32                                       MAX_DEBUG_VERTICES = 131072;

    constexpr u32                                       MAX_PREVIEW_DEBUG_VERTICES = 16384;

    // procedural meshes -----------------------------------------------------------------------------------------------

    inline constexpr u32                                MAX_PROCEDURAL_MESHES = 64;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Per-scene UBO that carries the camera + shadow state into the vertex/fragment stages
    // Layout must match [RendererCamera] in shared/shading_strings.h
    struct camera_ubo_data {

        glm::mat4                                       view_proj{1.0f};
        glm::mat4                                       view{1.0f};
        glm::vec4                                       camera_pos{0.0f};
        glm::vec4                                       frustum_planes[6]{};
        glm::uvec4                                      flags{0u};

        glm::mat4                                       projection{1.0f};
        glm::mat4                                       inv_projection{1.0f};
        glm::uvec4                                      cluster_dims{0u};    // x = grid_x, y = grid_y, z = grid_z, w = tile x | tile y << 16
        glm::vec4                                       cluster_depth{0.0f}; // x = near, y = far, z = render_w, w = render_h

        glm::mat4                                       light_view_proj{1.0f};
        glm::vec4                                       shadow_params{0.0f}; // x = world per texel, y = normal bias, z = depth bias, w = 1/res

        // Ambient. a = 0: hemisphere (rgb = radiance from above / below). ambient_sky.a = 1: image based lighting from the
        // sky (ambient_sky.rgb = intensity scale, ambient_ground.a = highest prefiltered specular mip)
        glm::vec4                                       ambient_sky{0.0f};
        glm::vec4                                       ambient_ground{0.0f};
    };


    // CPU side view of the environment (sky / IBL) for this frame
    struct environment_state {

        glm::vec3                                       sun_dir{0.f, 1.f, 0.f};     // towards the sun
        glm::vec3                                       sun_color{1.f};             // colour * intensity of the sun light
        bool                                            enabled = false;            // sky background + IBL active
        bool                                            dirty = true;               // rebuild cubemaps + SH this frame
        u64                                             hash = 0;                   // parameters the cubemaps were built with
        vk::Image                                       sky_image = nullptr;        // detects graph recreation of the targets
        vk::Image                                       specular_image = nullptr;
    };
    static_assert(sizeof(camera_ubo_data) == 528, "camera_ubo_data layout mismatch (RendererCamera in shading_strings.h)");


    // Per-instance SSBO entry. Layout matches [RendererInstance] in shared/shading_strings.h
    struct gpu_instance_data {

        glm::mat4                                       model{1.0f};
        glm::mat4                                       normal_matrix{1.0f};
        u32                                             mesh_index = 0;
        u32                                             material_index = 0;
        u32                                             flags = 0;      // bits 0..3: forced LOD + 1 (asset::mesh::INSTANCE_FLAG_FORCE_LOD_MASK), 0 = LOD 0
        u32                                             _pad1 = 0;
    };
    static_assert(sizeof(gpu_instance_data) == 144, "gpu_instance_data layout mismatch");


    // Written by cull.comp (main view only), read back on the host after the frame's fence. Layout matches DrawStats in
    // cull.comp.glsl
    struct gpu_draw_stats {

        u32                                             visible_draws = 0;          // indirect draw commands emitted
        u32                                             triangles = 0;
        u32                                             visible_instances = 0;
        u32                                             _pad = 0;
    };
    static_assert(sizeof(gpu_draw_stats) == 16, "gpu_draw_stats layout mismatch");


    // Per-light SSBO entry
    struct gpu_light {

        glm::vec4                                       position_radius;
        glm::vec4                                       color_intensity;
        glm::vec4                                       direction_inner_cos;
        glm::vec4                                       params;                 // x = cos(outer), y = type, z = shadow view idx, w = reserved
    };
    static_assert(sizeof(gpu_light) == 64, "gpu_light layout mismatch");


    // Local-shadow SSBO + UBO-per-view. Size is fixed by SHADOW_VIEW_STRIDE
    struct gpu_shadow_view {

        glm::mat4                                       view_proj{1.0f};
        glm::vec4                                       atlas_rect{0.0f};
        glm::vec4                                       params{0.0f};
        glm::vec4                                       sphere{0.0f};
        glm::vec4                                       planes[6]{};
        glm::vec4                                       _pad[3]{};
    };
    static_assert(sizeof(gpu_shadow_view) == SHADOW_VIEW_STRIDE, "gpu_shadow_view layout mismatch");


    // Global per-scene UBO (time, camera basis, matrices)
    struct gpu_globals {

        f32                                             time = 0.0f;
        f32                                             delta_time = 0.0f;
        u32                                             frame_index = 0u;
        f32                                             _pad0 = 0.0f;

        glm::vec2                                       viewport_size{0.0f};
        glm::vec2                                       render_size{0.0f};

        glm::vec3                                       camera_position{0.0f};
        f32                                             near_plane = 0.0f;
        glm::vec3                                       camera_forward{0.0f, 0.0f, -1.0f};
        f32                                             far_plane = 0.0f;
        glm::vec3                                       camera_right{1.0f, 0.0f, 0.0f};
        f32                                             _pad1 = 0.0f;
        glm::vec3                                       camera_up{0.0f, 1.0f, 0.0f};
        f32                                             _pad2 = 0.0f;

        glm::mat4                                       view{1.0f};
        glm::mat4                                       projection{1.0f};
        glm::mat4                                       view_projection{1.0f};
        glm::mat4                                       inv_view{1.0f};
        glm::mat4                                       inv_projection{1.0f};
        glm::mat4                                       inv_view_projection{1.0f};
    };
    static_assert(sizeof(gpu_globals) == 480, "gpu_globals layout mismatch");


    // One atlas tile for the local-shadow draw
    struct local_shadow_tile {

        u32                                             view = 0;
        u32                                             x = 0;
        u32                                             y = 0;
        u32                                             size = 0;
    };


    // Debug draw vertex. 16-byte aligned so `vec4 color` is std430-friendly
    struct debug_vertex {

        glm::vec3                                       position{0.0f};
        f32                                             _pad = 0.0f;
        glm::vec4                                       color{1.0f};
    };
    static_assert(sizeof(debug_vertex) == 32, "debug_vertex layout mismatch");


    // Scene-level shadow configuration. Populated by the scene renderer ctor (defaults below),
    // exposed for editor tuning later
    struct shadow_settings {

        bool                                            enabled = true;
        u32                                             resolution = 2048;
        f32                                             distance = 60.0f;
        f32                                             caster_extension = 100.0f;
        f32                                             normal_bias = 1.5f;
        f32                                             depth_bias = 0.0005f;
        u32                                             atlas_size = 4096;
        u32                                             max_local_lights = 8;
        f32                                             local_distance = 80.0f;
        f32                                             local_normal_bias = 1.5f;
        f32                                             local_bias = 1.0f;
    };


    // State fitted each frame by update_shadow() (Phase 4)
    struct shadow_state {

        bool                                            active = false;
        glm::vec3                                       direction{0.f, -1.f, 0.f};
        glm::mat4                                       view_proj{1.0f};
        glm::vec4                                       params{0.0f};
    };


    // Debug: per-primitive record (queued)
    struct debug_primitive_record {

        enum class kind : u8 { line, triangle }         type = kind::line;
        std::array<debug_vertex, 3>                     verts{};
        u8                                              vertex_count = 0;
        f32                                             remaining_lifetime = 0.f;
        bool                                            depth_test = true;
    };


    // Debug: per-mesh AABB watcher (set_draw_mesh_aabb)
    struct watched_mesh {

        GLT::asset::handle                              handle{};
        glm::vec4                                       color{1.f};
        f32                                             remaining_lifetime = 0.f;
        bool                                            depth_test = true;
    };


    // One resident procedural mesh. Buffers are owned by this slot; each slot has its own vertex/index allocation so different
    // procedural meshes can have wildly different sizes without churning a shared arena
    struct procedural_mesh_slot {

        u8                                              update_class = 0;       // GLT::render::mesh_update_class
        bool                                            alive = false;
        bool                                            finalized = false;      // true after the first upload; locks immutable slots
        u32                                             material_slot = 0;      // DEFAULT_MATERIAL_SLOT until acquire_material succeeds
        u32                                             vertex_count = 0;
        u32                                             index_count = 0;
        util::allocated_buffer                          vertex_buffer{};
        util::allocated_buffer                          index_buffer{};
        glm::vec3                                       local_min{0.0f};
        glm::vec3                                       local_max{0.0f};
    };


    // Debug: per-entity watcher (highlight_entity)
    struct watched_entity {

        GLT::world::entity_id                           id{};
        glm::vec4                                       color{1.f};
        f32                                             remaining_lifetime = 0.f;
        bool                                            depth_test = true;
    };


    // Debug: per-frame bucketing, produced by process_debug_primitives()
    struct frame_debug_layout {

        u32                                             line_depth_offset = 0;
        u32                                             line_depth_count = 0;
        u32                                             line_overlay_offset = 0;
        u32                                             line_overlay_count = 0;
        u32                                             tri_depth_offset = 0;
        u32                                             tri_depth_count = 0;
        u32                                             tri_overlay_offset = 0;
        u32                                             tri_overlay_count = 0;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
