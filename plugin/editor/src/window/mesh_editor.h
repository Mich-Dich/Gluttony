#pragma once

#include <imgui.h>
#include <ImGuizmo.h>

#include <asset/type.h>
#include <asset/asset_ref.h>
#include <asset/mesh.h>
#include <asset/mesh_lod.h>
#include <asset/mesh_cluster.h>
#include <array>
#include <chrono>
#include <future>
#include <render/i_renderer.h>
#include <render/i_scene_renderer.h>
#include <render/i_debug_draw.h>
#include <physics/material.h>
#include <physics/shape.h>

#include "window/base/asset_editor.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::render {
    class i_scene_renderer;
    class i_debug_draw;
}

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    enum class collider_shape_kind : u8 {

        box = 0,
        sphere,
        capsule,
        cylinder,
        convex_hull,
        triangle_mesh,
    };


    // Editor-side description of a single collider entry
    //
    // This is NOT the runtime physics object. It is a flat, trivially-iterable representation the UI can iterate without a
    // std::visit per row. Fields not relevant to [kind] are simply ignored
    struct collider_editor_item {

        u64                                     id{ 0 };
        std::string                             name{};
        collider_shape_kind                     kind = collider_shape_kind::box;
        bool                                    visible = true;
        glm::vec4                               color{ 0.20f, 0.90f, 0.40f, 1.00f };
        glm::vec3                               position{ 0.0f };
        glm::quat                               rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3                               box_half_extents{ 0.5f };
        f32                                     sphere_radius = 0.5f;
        f32                                     capsule_radius = 0.25f;
        f32                                     capsule_half_height = 0.5f;
        f32                                     cylinder_radius = 0.25f;
        f32                                     cylinder_half_height = 0.5f;
        GLT::physics::physics_material          material{};
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    [[nodiscard]] const char* collider_shape_kind_name(collider_shape_kind k) noexcept;

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Dockable editor window for a mesh asset + its collider set
    //
    // Three-panel layout:
    //   - Outliner: mesh hierarchy / submeshes / collider list
    //   - Preview : 3D viewport with camera controls and collider wireframes
    //   - Inspector: properties for the currently selected item (mesh: summary + Level of Detail settings)
    //
    // Geometry detail: a mesh uses discrete LODs or virtual geometry, never both (asset::mesh::detail_mode). The card
    // "Geometry Detail" switches the mode, and the inspector, the outliner and the preview overlay only show the active
    // mode's controls.
    //
    // Discrete LODs: levels are generated on import; the inspector edits lod_settings and regenerates in the background.
    // The preview can lock to one level (overlay selector / outliner) and draw that level's wireframe
    //
    // Virtual geometry: the cluster hierarchy is built in the background when the mode is chosen. The discrete levels
    // stay as a read-only fallback (shadows, translucent materials); locking the preview to one shows it. Debug views:
    // the renderer's "Virtual Geometry" render modes (clusters, levels, triangles, error), cluster bounds, and a
    // wireframe of the triangles the preview camera selects
    //
    // Rendering analysis: the left panel shows either the outliner or "Rendering", which tells
    // what the mesh costs to draw and which of the two detail systems suits it. It samples the preview's GPU statistics
    // (i_scene_renderer::last_frame_stats) while the user looks at the mesh and sorts them by the mesh's size on screen,
    // so discrete LODs and virtual geometry can be compared over distance (mesh_editor_analysis.cpp)
    //
    // Both side panels are resizable via a 6px splitter handle between panels
    class mesh_editor_window : public asset_editor {
    public:

        mesh_editor_window();
        mesh_editor_window(const std::filesystem::path& path);
        ~mesh_editor_window();

        DEFAULT_GETTER(GLT::asset::handle,                      asset_handle)
        DEFAULT_GETTER_C(std::filesystem::path,                 mesh_path)
        DEFAULT_GETTER(bool,                                    dirty)

        FORCE_INLINE_R const std::filesystem::path& get_asset_path() const noexcept override { return m_mesh_path; }


        void open(const std::filesystem::path& path);

        void close_mesh();

        void window(const f32 delta_time) override;

        void update(const f32 delta_time) override;

        bool serialize(const std::filesystem::path& project_file, GLT::serializer::option option) override;

        bool save();

    private:

        enum class inspector_target : u8 {

            mesh = 0,
            submesh,
            collider,
        };

        // What the left panel shows: the outliner (mesh, submeshes, colliders, levels) or the rendering analysis of the
        // whole mesh. The inspector on the right always shows the properties of the selection, so settings can be changed
        // while the analysis is in view
        enum class left_panel_view : u8 {

            outliner = 0,
            rendering,
        };

        // Preview camera range (wheel zoom, distance slider, analysis sweep)
        static constexpr f32                            MIN_CAM_DISTANCE = 0.05f;
        static constexpr f32                            MAX_CAM_DISTANCE = 1500.0f;

        struct submesh_bounds {

            glm::vec3                                   min{ 0.0f };
            glm::vec3                                   max{ 0.0f };
        };

        struct preview_camera_state {
            glm::vec3                                   target{ 0.0f };
            f32                                         distance = 3.0f;
            f32                                         yaw_rad = 0.7f;
            f32                                         pitch_rad = 0.35f;
            f32                                         fov_deg = 45.0f;
        };

        // A cluster selection (what a view picks of a hierarchy) kept between frames. The editor needs it in several places
        // per frame (statistics, preview overlay, wireframe, cluster bounds, rendering analysis); computing it visits every
        // cluster, which in a Debug build costs milliseconds for a large mesh. Recomputed when the view or the clusters
        // change, at most every CUT_REFRESH_SECONDS while the camera moves (the numbers may lag a moving camera by that)
        struct cut_cache {

            const GLT::asset::mesh::cluster*            clusters = nullptr;     // identity of the hierarchy it was made for
            size_t                                      cluster_count = 0;
            u32                                         revision = 0;
            GLT::asset::mesh::cluster_view              view{};
            std::chrono::steady_clock::time_point       time{};
            std::vector<u32>                            selected{};             // collect_cut()
            GLT::asset::mesh::cluster_cut               cut{};                  // measure_cut() of the same
            bool                                        valid = false;
        };

        // Per-hierarchy statistics of the "Virtual Geometry" card (one pass over the clusters per build, not per frame)
        struct cluster_stats_cache {

            const GLT::asset::mesh::cluster*            clusters = nullptr;
            size_t                                      cluster_count = 0;
            u32                                         revision = 0;
            u32                                         source_clusters = 0;
            u32                                         source_triangles = 0;
            u32                                         coarsest_triangles = 0;
            u32                                         levels = 0;
        };

        struct preview_lighting_state {
            f32                                         key_intensity = 0.3f;
            f32                                         fill_intensity = 0.1f;
            f32                                         rim_intensity = 0.15f;
            bool                                        headlight = false;
        };

        // rendering analysis types ------------------------------------------------------------------------------------

        // Samples are sorted by the mesh's size on screen (bounding sphere diameter / view height, the metric both
        // detail systems select by), half an octave per range: range 0 starts at ANALYSIS_SCREEN_MAX, the last one
        // ends at ANALYSIS_SCREEN_MAX / 2^(ANALYSIS_RANGES / 2)
        static constexpr u32                            ANALYSIS_RANGES = 18;
        static constexpr f32                            ANALYSIS_SCREEN_MAX = 4.0f;
        static constexpr u32                            ANALYSIS_CURVE_POINTS = ANALYSIS_RANGES * 2 + 1;   // range borders and centres
        static constexpr u32                            ANALYSIS_YAW_BINS = 8;      // view directions: 8 sectors around the mesh
        static constexpr u32                            ANALYSIS_PITCH_BINS = 3;    // x (from below, level, from above)

        enum analysis_series_id : u32 {

            series_lods = 0,                            // frames drawn through the discrete levels
            series_virtual_geometry,                    // frames drawn as clusters
            series_count,
        };

        // Sums of the samples of one size range
        struct analysis_range {

            u32                                         samples = 0;
            u32                                         timed_samples = 0;      // samples that came with GPU times
            f64                                         geometry_ms = 0.0;
            f64                                         gpu_ms = 0.0;
            f64                                         triangles = 0.0;
            f64                                         draws = 0.0;
        };

        struct analysis_series {

            std::array<analysis_range, ANALYSIS_RANGES> ranges{};
            u32                                         samples = 0;
            u64                                         signature = 0;          // of the data it was measured with (cleared when that changes)
        };

        // What each system selects over distance, computed on the CPU (no culling). Filled a few points per frame
        struct analysis_curves {

            std::array<f32, ANALYSIS_CURVE_POINTS>      distance{};
            std::array<f32, ANALYSIS_CURVE_POINTS>      lod_triangles{};
            std::array<f32, ANALYSIS_CURVE_POINTS>      lod_draws{};
            std::array<f32, ANALYSIS_CURVE_POINTS>      cluster_triangles{};
            std::array<f32, ANALYSIS_CURVE_POINTS>      cluster_draws{};
            u64                                         data_key = 0;           // mesh data, estimate, projection: a change computes every point again
            glm::vec3                                   direction{ 0.0f, 0.0f, 1.0f };  // view direction the cluster points were computed along
            u32                                         filled = 0;             // cluster points [0, filled) are computed (a few per frame, near first)
            bool                                        has_clusters = false;   // the cluster arrays hold data (clusters or an estimate exist)
        };

        // "Measure distances": a continuous camera flight. The camera zooms from near to far at an even pace on the
        // (log) range axis (SWEEP_SECONDS_PER_RANGE) with auto rotate on and gently changing height; with fallback
        // levels to compare it then flies back in drawing the level LOD mode would pick
        struct analysis_sweep {

            bool                                        active = false;
            u32                                         pass = 0;               // 0: as the mesh is drawn (near -> far), 1: fallback levels (far -> near)
            u32                                         pass_count = 1;
            f32                                         position = 0.0f;        // on the range axis (analysis_range_screen)
            f32                                         first = 0.0f;           // reachable part of the axis
            f32                                         last = 0.0f;
            f32                                         elapsed = 0.0f;         // seconds since the start (height swing)
            bool                                        saved_auto_rotate = false;
            preview_camera_state                        saved_camera{};
            i32                                         saved_lod = -1;
        };

        struct analysis_state {

            std::array<analysis_series, series_count>   series{};
            std::array<u32, ANALYSIS_YAW_BINS * ANALYSIS_PITCH_BINS> directions{};     // samples per view direction
            analysis_curves                             curves{};
            analysis_sweep                              sweep{};

            GLT::render::frame_stats                    live{};                 // newest statistics of the preview
            f32                                         live_geometry_ms = 0.0f;// smoothed
            f32                                         live_gpu_ms = 0.0f;
            u64                                         last_serial = 0;
            u32                                         settle = 0;             // statistics to skip after the view setup changed
            i32                                         tracked_lod = -1;
            u32                                         tracked_mode = 0;
            glm::uvec2                                  data_extent{ 0, 0 };    // preview size the samples were taken at
            u64                                         geometry_signature = 0;

            // Virtual geometry estimate for a mesh in LOD mode: a cluster build that only this panel uses
            std::future<GLT::asset::mesh::cluster_build_output> estimate_job{};
            GLT::asset::mesh::cluster_build_output      estimate{};
            bool                                        has_estimate = false;
            u64                                         estimate_serial = 0;

            // Persistence: <project>/metadata/asset_stats/<asset uuid>.txt (absolute path fixed when the mesh opens, so a
            // save on shutdown needs no application). Empty = the asset has no UUID, nothing is saved
            std::filesystem::path                       data_file{};
            bool                                        dirty = false;          // samples not written yet
            f32                                         unsaved_seconds = 0.0f;
        };

        // How much of the mesh the user has looked at so far (drives the "look at it from ..." reminders)
        struct analysis_coverage {

            analysis_series_id                          series = series_lods;   // the system the preview draws with now
            u32                                         ranges_seen = 0;        // of the distance ranges worth looking at
            u32                                         ranges_expected = 0;
            i32                                         nearest_seen = -1;      // range index, -1 = none
            i32                                         farthest_seen = -1;
            i32                                         first_expected = -1;
            i32                                         last_expected = -1;
            u32                                         directions_seen = 0;    // of ANALYSIS_YAW_BINS * ANALYSIS_PITCH_BINS
            bool                                        seen_from_above = false;
            bool                                        seen_from_below = false;
            bool                                        thin = true;            // too little for advice that holds
        };

        // top level panels --------------------------------------------------------------------------------------------

        void draw_toolbar();
        void draw_body();
        void draw_outliner_panel();
        void draw_preview_panel();
        void draw_inspector_panel();

        // inspector sections ------------------------------------------------------------------------------------------

        void draw_left_panel_switch();
        void draw_inspector_mesh();
        void draw_inspector_submesh();
        void draw_inspector_collider();

        void draw_mesh_summary_section();
        void draw_detail_mode_section();
        void draw_lod_section();
        void draw_virtual_geometry_section();
        void draw_virtual_geometry_debug_section();
        void draw_mesh_bounds_section();
        void draw_preview_settings_section();
        void draw_lighting_section();
        void draw_camera_section();

        // outliner helpers --------------------------------------------------------------------------------------------

        void draw_collider_row(i32 index);
        void draw_add_collider_popup();

        // collider state ----------------------------------------------------------------------------------------------

        void add_collider(collider_shape_kind kind);
        void remove_collider(i32 index);
        void reset_collider_state();

        void select_mesh();
        void select_submesh(i32 index);
        void select_collider(i32 index);

        // preview plumbing --------------------------------------------------------------------------------------------

        void ensure_preview();
        void destroy_preview();

        [[nodiscard]] GLT::world::camera_snapshot build_camera_snapshot() const;
        void reset_camera_to_fit();

        void submit_debug_draws();
        void submit_collider_debug_draws();
        void submit_preview_lights();
        void submit_preview_instance();

        // level of detail ---------------------------------------------------------------------------------------------

        void start_lod_generation();
        void poll_lod_generation();
        void set_preview_lod(i32 lod);
        void draw_preview_lod_overlay(const ImVec2& canvas_min, const ImVec2& canvas_max);
        void submit_wireframe_debug_draws(const GLT::asset::mesh::mesh_asset& mesh);
        [[nodiscard]] f32 preview_screen_size(const GLT::asset::mesh::mesh_asset& mesh) const;
        [[nodiscard]] u32 auto_lod_for(const GLT::asset::mesh::mesh_asset& mesh, f32 screen_size) const;

        // virtual geometry --------------------------------------------------------------------------------------------

        void apply_detail_mode(GLT::asset::mesh::detail_mode mode);
        void start_cluster_build();
        void poll_cluster_build();
        [[nodiscard]] bool preview_uses_virtual_geometry(const GLT::asset::mesh::mesh_asset& mesh) const;
        [[nodiscard]] GLT::asset::mesh::cluster_view preview_cluster_view(const GLT::asset::mesh::mesh_asset& mesh) const;
        [[nodiscard]] GLT::asset::mesh::cluster_cut preview_cluster_cut(const GLT::asset::mesh::mesh_asset& mesh);
        [[nodiscard]] const cut_cache& cached_cut(cut_cache& cache, std::span<const GLT::asset::mesh::cluster> clusters,
            const GLT::asset::mesh::cluster_view& view, u32 revision);
        [[nodiscard]] const cluster_stats_cache& cluster_stats(const GLT::asset::mesh::mesh_asset& mesh);
        void submit_cluster_bounds_debug_draws(const GLT::asset::mesh::mesh_asset& mesh);
        void set_preview_render_mode(u32 mode_id);
        [[nodiscard]] u32 default_preview_render_mode() const;

        // rendering analysis (mesh_editor_analysis.cpp) ----------------------------------------------------------------

        void draw_rendering_analysis();
        void draw_analysis_recommendation(GLT::asset::mesh::mesh_asset& mesh);
        void draw_analysis_view_section(const GLT::asset::mesh::mesh_asset& mesh);
        void draw_analysis_charts_section(const GLT::asset::mesh::mesh_asset& mesh);
        void draw_analysis_coverage_section(const GLT::asset::mesh::mesh_asset& mesh);
        void draw_analysis_memory_section(const GLT::asset::mesh::mesh_asset& mesh);

        void update_analysis(f32 seconds);
        void reset_analysis(bool keep_estimate);
        void load_analysis_data(GLT::UUID asset_id);
        void save_analysis_data();
        void update_analysis_curves(const GLT::asset::mesh::mesh_asset& mesh);
        void start_analysis_estimate();
        void poll_analysis_estimate();
        void start_analysis_sweep();
        void stop_analysis_sweep();
        void update_analysis_sweep(const GLT::asset::mesh::mesh_asset& mesh, f32 seconds);
        [[nodiscard]] f32 analysis_sweep_progress() const noexcept;
        void draw_analysis_sweep_overlay(const ImVec2& card_min, const ImVec2& card_max);

        [[nodiscard]] std::span<const GLT::asset::mesh::cluster> analysis_clusters(const GLT::asset::mesh::mesh_asset& mesh) const;
        [[nodiscard]] analysis_coverage analysis_coverage_of(const GLT::asset::mesh::mesh_asset& mesh) const;
        [[nodiscard]] f32 analysis_range_distance(const GLT::asset::mesh::mesh_asset& mesh, f32 position) const;
        [[nodiscard]] bool analysis_range_reachable(const GLT::asset::mesh::mesh_asset& mesh, u32 range) const;
        [[nodiscard]] static f32 analysis_range_screen(f32 position) noexcept;
        [[nodiscard]] static u32 analysis_range_of(f32 screen_size) noexcept;

        // collider <-> asset sync -------------------------------------------------------------------------------------
        void load_colliders_from_mesh(const GLT::asset::mesh::mesh_asset& mesh);
        bool sync_colliders_to_mesh();

        // collider gizmo ----------------------------------------------------------------------------------------------
        void draw_collider_gizmo_toolbar();
        void render_collider_gizmo(const ImVec2& image_min, const ImVec2& image_size);

        // helpers -----------------------------------------------------------------------------------------------------

        void compute_mesh_bounds(const GLT::asset::mesh::mesh_asset& mesh, glm::vec3& out_min, glm::vec3& out_max);
        void rebuild_submesh_bounds_cache(const GLT::asset::mesh::mesh_asset& mesh);
        bool load_from_handle(GLT::asset::handle h);
        void handle_drag_drop();


        std::filesystem::path                                   m_mesh_path{};
        GLT::asset::asset_ref                                   m_asset_handle = INVALID_HANDLE;
        std::string                                             m_mesh_name{};

        bool                                                    m_has_mesh = false;
        bool                                                    m_load_failed = false;
        bool                                                    m_dirty = false;

        std::vector<submesh_bounds>                             m_submesh_bounds_cache{};

        // collider editor state ---------------------------------------------------------------------------------------

        std::vector<collider_editor_item>                       m_colliders{};
        inspector_target                                        m_inspector_target = inspector_target::mesh;
        left_panel_view                                         m_left_view = left_panel_view::outliner;

        i32                                                     m_selected_submesh = -1;
        i32                                                     m_hovered_submesh = -1;
        i32                                                     m_selected_collider = -1;
        i32                                                     m_hovered_collider = -1;
        i32                                                     m_pending_delete_collider = -1;

        u64                                                     m_next_collider_id = 1;
        bool                                                    m_show_colliders_in_preview = true;

        // panel layout ------------------------------------------------------------------------------------------------

        // Widths of the two side panels. The splitter handles between panels edit these directly, so they persist across frames
        f32                                                     m_outliner_width = 240.0f;
        f32                                                     m_rendering_width = 400.0f;     // the left panel while it shows "Rendering" (graphs need room)
        f32                                                     m_inspector_width = 380.0f;

        // preview state -----------------------------------------------------------------------------------------------

        GLT::render::i_scene_renderer*                          m_preview_scene = nullptr;
        preview_camera_state                                    m_preview_camera{};
        preview_lighting_state                                  m_preview_lighting{};
        u32                                                     m_current_mode_id = 0;
        glm::vec4                                               m_background_color{ 0.09f, 0.09f, 0.09f, 1.0f };
        bool                                                    m_show_grid = true;
        bool                                                    m_show_origin = true;
        bool                                                    m_auto_rotate = false;
        f32                                                     m_auto_rotate_speed = 0.5f;
        bool                                                    m_preview_image_drawn = false;
        glm::uvec2                                              m_last_preview_size{ 0, 0 };
        ImGuizmo::OPERATION                                     m_collider_gizmo_operation = ImGuizmo::TRANSLATE;
        ImGuizmo::MODE                                          m_collider_gizmo_mode = ImGuizmo::LOCAL;
        glm::vec3                                               m_gizmo_last_scale{ 1.0f };

        // level of detail ---------------------------------------------------------------------------------------------

        i32                                                     m_preview_lod = -1;         // -1 = automatic (screen size)
        bool                                                    m_show_wireframe = false;
        bool                                                    m_lod_needs_regen = false;  // generation settings changed
        std::future<GLT::asset::mesh::lod_build_output>         m_lod_job{};
        GLT::asset::mesh::lod_settings                          m_lod_job_settings{};
        f64                                                     m_last_lod_build_ms = 0.0;

        // virtual geometry --------------------------------------------------------------------------------------------

        bool                                                    m_clusters_need_rebuild = false;    // build settings changed
        std::future<GLT::asset::mesh::cluster_build_output>     m_cluster_job{};
        f64                                                     m_last_cluster_build_ms = 0.0;
        bool                                                    m_show_cluster_bounds = false;      // debug: bounding spheres of the selected clusters
        bool                                                    m_cluster_bounds_truncated = false; // more selected clusters than spheres drawn
        std::vector<u32>                                        m_cut_scratch{};                    // working copy for the cluster bounds overlay
        cut_cache                                               m_preview_cut{};                    // the preview camera's selection of the mesh's clusters
        cut_cache                                               m_estimate_cut{};                   // ... of the analysis estimate (mesh in LOD mode)
        cluster_stats_cache                                     m_cluster_stats{};

        // compute_mesh_bounds(): the vertex loop runs once per geometry change, not per frame
        const GLT::asset::mesh::vertex*                         m_bounds_vertices = nullptr;
        size_t                                                  m_bounds_vertex_count = 0;
        u32                                                     m_bounds_revision = 0;
        glm::vec3                                               m_bounds_min{ 0.0f };
        glm::vec3                                               m_bounds_max{ 0.0f };
        bool                                                    m_bounds_valid = false;

        std::vector<glm::vec3>                                  m_wire_lines{};             // pairs, cached per (lod, revision) or per cluster selection
        i32                                                     m_wire_lod = -2;            // WIRE_LEVEL_CLUSTERS: the cache holds a cluster selection
        u64                                                     m_wire_cut_signature = 0;   // hash of that selection
        u32                                                     m_wire_revision = 0;
        bool                                                    m_wire_truncated = false;

        // rendering analysis ------------------------------------------------------------------------------------------

        analysis_state                                          m_analysis{};

    };

}
