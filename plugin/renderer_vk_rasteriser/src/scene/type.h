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
    // Local shadow atlas pass only (shadow_cull.comp.glsl): bit 31 marks the pass, bits 20..30 hold the shadow view instead
    // of the LOD info (which only colours debug views)
    constexpr u32                                       INSTANCE_SHADOW_PASS = 1u << 31;
    constexpr u32                                       INSTANCE_SHADOW_VIEW_SHIFT = 20;    // 11 bits
    static_assert(MAX_INSTANCES - 1u <= INSTANCE_INDEX_MASK, "instance index must fit the firstInstance encoding");

    constexpr u32                                       INITIAL_INSTANCE_CAPACITY = 4096;

    constexpr u32                                       INITIAL_DRAW_CAPACITY = 4096;       // draw commands (instance x submesh)

    // virtual geometry (scene_renderer_virtual_geometry.inl, virtual_geometry_cull.comp.glsl) --------------------------

    // gpu_instance_data::flags above the forced-LOD nibble. VIRTUAL: in the main view the instance is drawn cluster by
    // cluster (cull.comp skips it there). CONE_CULL: its transform and materials allow backface cone culling of clusters
    constexpr u32                                       INSTANCE_FLAG_VIRTUAL = 1u << 4;
    constexpr u32                                       INSTANCE_FLAG_CONE_CULL = 1u << 5;

    // One work item = one compute workgroup = this many consecutive clusters of one instance (local_size_x of the shader)
    constexpr u32                                       VIRTUAL_CLUSTERS_PER_ITEM = 64;

    // Work items per dispatch row. Items beyond one row go to the next (maxComputeWorkGroupCount[0] is only guaranteed
    // to be 65535)
    constexpr u32                                       VIRTUAL_DISPATCH_WIDTH = 32768;

    constexpr u32                                       INITIAL_VIRTUAL_DRAW_CAPACITY = 4096;   // cluster draw commands

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

    // Light index list (light_culling.comp.glsl). Entries [0, MAX_LIGHTS) hold the identity list (0, 1, 2, ...): a cluster
    // that every light touches, or whose list does not fit any more, points there. Behind them the clusters' exact lists:
    // CLUSTER_INDEX_BUDGET_PER_CLUSTER entries per cluster on average, MAX_CLUSTER_INDEX_ENTRIES in total at most.
    // This is a budget for speed. There is no limit of lights per cluster: cutting a cluster's list shows the cluster
    // grid in the image wherever more lights overlap than the limit allows
    constexpr u32                                       CLUSTER_INDEX_BUDGET_PER_CLUSTER = 256;

    constexpr u32                                       MAX_CLUSTER_INDEX_ENTRIES = 4 * 1024 * 1024;
    static_assert(MAX_CLUSTER_INDEX_ENTRIES > MAX_LIGHTS, "the light index list must hold the identity list");

    // shadow ----------------------------------------------------------------------------------------------------------

    // Local (point / spot) lights with shadow maps at once, and their views (a point light has six). All of them are
    // sampled; how many are DRAWN per frame is limited separately below
    constexpr u32                                       MAX_SHADOW_LIGHTS = 256;
    constexpr u32                                       MAX_SHADOW_VIEWS = MAX_SHADOW_LIGHTS * 6;

    // Sun shadows: nested maps around the camera (scene/sun_shadow_cascades.h). Their views sit behind the local lights'
    // views in the shadow view buffer, so the cull shader, the atlas vertex shader and the sampling code treat both alike
    constexpr u32                                       MAX_SUN_CASCADES = 4;
    constexpr u32                                       SUN_SHADOW_VIEW_FIRST = MAX_SHADOW_VIEWS;
    constexpr u32                                       SHADOW_VIEW_CAPACITY = MAX_SHADOW_VIEWS + MAX_SUN_CASCADES;
    static_assert(SHADOW_VIEW_CAPACITY <= (1u << (31 - INSTANCE_SHADOW_VIEW_SHIFT)), "shadow view must fit firstInstance bits 20..30");

    // Cascades drawn per frame at most (sizes the sun pass's draw commands: draws in the scene x this)
    constexpr u32                                       SUN_CASCADES_PER_FRAME = 2;

    // Views drawn per frame at most. Shadow maps are kept until their light or a caster in it moves, so this only
    // limits how fast a burst of changes (world loaded, camera teleported) is worked off
    constexpr u32                                       SHADOW_VIEWS_PER_FRAME = 64;

    // Of those: views that are not on screen right now (kept ready for when the camera turns)
    constexpr u32                                       SHADOW_BACKGROUND_VIEWS_PER_FRAME = 8;

    // Draw commands reserved for the local shadow pass: (draws in the scene) x (views drawn this frame) never exceeds
    // it, large scenes draw fewer views per frame instead (at least 6: one point light)
    constexpr u32                                       SHADOW_COMMAND_BUDGET = 1'000'000;

    constexpr u32                                       SHADOW_VIEW_STRIDE = 256;

    constexpr u32                                       SHADOW_TILE_MIN = 64;

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

        glm::mat4                                       light_view_proj{1.0f};  // unused (the sun cascades are shadow views)
        glm::vec4                                       shadow_params{0.0f};    // x = sun cascades (0 = none), y = their first shadow view

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
        u32                                             flags = 0;      // bits 0..3: forced LOD + 1 (asset::mesh::INSTANCE_FLAG_FORCE_LOD_MASK), 0 = LOD 0. Bits 4..5: INSTANCE_FLAG_VIRTUAL / _CONE_CULL
        u32                                             _pad1 = 0;
    };
    static_assert(sizeof(gpu_instance_data) == 144, "gpu_instance_data layout mismatch");


    // Push constants of virtual_geometry_cull.comp.glsl (layout must match). The thresholds are also what the virtual
    // geometry debug views use, so both show the same selection
    struct virtual_geometry_cull_push {

        u32                                             item_count = 0;
        u32                                             dispatch_width = VIRTUAL_DISPATCH_WIDTH;
        f32                                             error_threshold = 0.0f;     // pixel error / viewport height
        f32                                             projection_scale = 0.0f;    // |P11| * 0.5
        f32                                             near_plane = 0.0f;
        u32                                             flags = 0;
    };
    static_assert(sizeof(virtual_geometry_cull_push) == 24, "virtual_geometry_cull_push layout mismatch");


    // Written by cull.comp (main view only), read back on the host after the frame's fence. Layout matches DrawStats in
    // cull.comp.glsl
    struct gpu_draw_stats {

        u32                                             visible_draws = 0;          // indirect draw commands emitted
        u32                                             triangles = 0;
        u32                                             visible_instances = 0;
        u32                                             _pad = 0;

        // Written by virtual_geometry_cull.comp only (cull.comp declares just the four counters above). The cluster
        // draws are also counted in visible_draws / triangles
        u32                                             virtual_clusters = 0;
        u32                                             virtual_triangles = 0;
        u32                                             _pad1 = 0;
        u32                                             _pad2 = 0;
    };
    static_assert(sizeof(gpu_draw_stats) == 32, "gpu_draw_stats layout mismatch");


    // Per-light SSBO entry
    struct gpu_light {

        glm::vec4                                       position_radius;
        glm::vec4                                       color_intensity;
        glm::vec4                                       direction_inner_cos;
        glm::vec4                                       params;                 // x = cos(outer), y = type, z = shadow view idx, w = reserved
    };
    static_assert(sizeof(gpu_light) == 64, "gpu_light layout mismatch");


    // Local-shadow view (SSBO, set 0 binding 6: cull, atlas vertex shader, sampling). Size is fixed by SHADOW_VIEW_STRIDE
    struct gpu_shadow_view {

        glm::mat4                                       view_proj{1.0f};
        glm::vec4                                       atlas_rect{0.0f};
        glm::vec4                                       params{0.0f};
        glm::vec4                                       sphere{0.0f};
        glm::vec4                                       planes[6]{};
        glm::vec4                                       cull{0.0f};             // x != 0: drawn this frame. y: views of this light, on its first view only
        glm::vec4                                       group_sphere{0.0f};     // first view of a light: its sphere as of this frame (cull)
        glm::vec4                                       _pad{0.0f};
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

        // Sun: nested maps around the camera (scene/sun_shadow_cascades.h). The shadow distance is settings::visual
        u32                                             cascades = MAX_SUN_CASCADES;
        u32                                             resolution = 2048;          // texels per side of one cascade (the texture is 2 x 2 of them)
        f32                                             first_cascade = 30.0f;      // range of the first cascade (metres from the camera)
        f32                                             normal_bias = 2.0f;         // texels, along the surface normal (the filter's radius)
        f32                                             bias = 1.0f;                // texels, towards the sun

        // Local lights (scene/local_shadow_cache.h). How many of them: settings::visual
        u32                                             atlas_size = 4096;
        f32                                             local_distance = 80.0f;     // camera to a light's sphere: these lights share the atlas first
        f32                                             local_far_distance = 320.0f;    // ... and up to here, lights use what the near ones leave free
        f32                                             local_normal_bias = 1.5f;
        f32                                             local_bias = 1.0f;
    };


    // State fitted each frame by update_shadow() (Phase 4)
    struct shadow_state {

        bool                                            active = false;
        glm::vec3                                       direction{0.f, -1.f, 0.f};
        u32                                             cascades = 0;               // cascades in the view buffer this frame
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
