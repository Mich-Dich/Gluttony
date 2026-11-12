#pragma once

#include <imgui.h>
#include <ImGuizmo.h>

#include <asset/type.h>
#include <asset/asset_ref.h>
#include <asset/mesh.h>
#include <asset/mesh_lod.h>
#include <asset/mesh_cluster.h>
#include <future>
#include <render/i_renderer.h>
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

        struct preview_lighting_state {
            f32                                         key_intensity = 0.3f;
            f32                                         fill_intensity = 0.1f;
            f32                                         rim_intensity = 0.15f;
            bool                                        headlight = false;
        };

        // top level panels --------------------------------------------------------------------------------------------

        void draw_toolbar();
        void draw_body();
        void draw_outliner_panel();
        void draw_preview_panel();
        void draw_inspector_panel();

        // inspector sections ------------------------------------------------------------------------------------------

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
        [[nodiscard]] GLT::asset::mesh::cluster_cut preview_cluster_cut(const GLT::asset::mesh::mesh_asset& mesh) const;
        void submit_cluster_bounds_debug_draws(const GLT::asset::mesh::mesh_asset& mesh);
        void set_preview_render_mode(u32 mode_id);
        [[nodiscard]] u32 default_preview_render_mode() const;

        // collider <-> asset sync -------------------------------------------------------------------------------------
        void load_colliders_from_mesh(const GLT::asset::mesh::mesh_asset& mesh);
        bool sync_colliders_to_mesh();

        // collider gizmo ----------------------------------------------------------------------------------------------
        void draw_collider_gizmo_toolbar();
        void render_collider_gizmo(const ImVec2& image_min, const ImVec2& image_size);

        // helpers -----------------------------------------------------------------------------------------------------

        void compute_mesh_bounds(const GLT::asset::mesh::mesh_asset& mesh, glm::vec3& out_min, glm::vec3& out_max) const;
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
        std::vector<u32>                                        m_cut_scratch{};                    // clusters the preview camera selects (reused per frame)

        std::vector<glm::vec3>                                  m_wire_lines{};             // pairs, cached per (lod, revision) or per cluster selection
        i32                                                     m_wire_lod = -2;            // WIRE_LEVEL_CLUSTERS: the cache holds a cluster selection
        u64                                                     m_wire_cut_signature = 0;   // hash of that selection
        u32                                                     m_wire_revision = 0;
        bool                                                    m_wire_truncated = false;

    };

}
