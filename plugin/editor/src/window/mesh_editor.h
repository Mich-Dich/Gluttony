
// #pragma once

// #include <imgui.h>

// #include <asset/type.h>
// #include <asset/mesh.h>
// #include <render/i_renderer.h>
// #include <render/i_debug_draw.h>

// #include "window/base/asset_editor.h"



// // FORWARD DECLARATIONS ================================================================================================

// namespace GLT::render {
//     class i_preview;
//     class i_debug_draw;
// }

// namespace GLT::editor {

//     // CLASS DECLARATION ===============================================================================================

//     // Dockable editor window for a mesh asset.
//     //
//     // Read-only over the geometry itself (meshes are static — the handler doesn't override [serialize()]), but the editor
//     // still lets the user:
//     //   - preview the mesh through the renderer's [i_preview] path,
//     //   - inspect every submesh (index range, material, computed per-submesh AABB),
//     //   - swap submesh materials in-memory (a preview-only convenience),
//     //   - highlight a submesh inside the preview by hovering its row,
//     //   - drive the preview camera (orbit / distance / FOV / fit),
//     //   - switch render mode, grid, origin, background,
//     //   - toggle auto-rotation for presentations.
//     //
//     // All preview interactions route through [i_preview] obtained via [renderer->as<i_preview>()]. Debug overlays use
//     // the same opt-in pattern ([renderer->as<i_debug_draw>()]) when available.
//     class mesh_editor_window : public asset_editor {
//     public:

//         mesh_editor_window();
//         mesh_editor_window(const std::filesystem::path& path);
//         ~mesh_editor_window();

//         DEFAULT_GETTER(GLT::asset::handle,                      asset_handle)
//         DEFAULT_GETTER_C(std::filesystem::path,                 mesh_path)
//         DEFAULT_GETTER(bool,                                    dirty)

//         FORCE_INLINE_R const std::filesystem::path& get_asset_path() const noexcept override { return m_mesh_path; }


//         void open(const std::filesystem::path& path);


//         void close_mesh();


//         void window(const f32 delta_time) override;


//         void update(const f32 delta_time) override;


//         bool serialize(const std::filesystem::path& project_file, GLT::serializer::option option) override;


//         bool save();            // attempts [registry::save()]; meshes currently return [no_handler]

//     private:

//         struct submesh_bounds {

//             glm::vec3                                   min{ 0.0f };
//             glm::vec3                                   max{ 0.0f };
//         };

//         // panels -----------------------------------------------------------------------------------------------------

//         void draw_details_panel();

//         void draw_preview_panel();

//         // sections ---------------------------------------------------------------------------------------------------

//         void draw_summary_section();

//         void draw_bounds_section();

//         void draw_submesh_section();

//         void draw_preview_settings_section();

//         void draw_camera_section();

//         // preview plumbing -------------------------------------------------------------------------------------------

//         void ensure_preview();

//         void destroy_preview();

//         void push_preview_state();

//         void update_highlight();

//         void reset_camera_to_fit();

//         // Submits the whole-mesh AABB (always) and hovered-submesh AABB (only while a row is hovered) through the preview's scoped
//         // [i_debug_draw]. Called every frame from update() — primitives are lifetime == 0 (single-frame), so the editor is the one
//         // re-submitting them each tick
//         void submit_debug_draws();

//         // helpers ----------------------------------------------------------------------------------------------------

//         void compute_mesh_bounds(const GLT::asset::mesh::mesh_asset& mesh, glm::vec3& out_min, glm::vec3& out_max) const;

//         void rebuild_submesh_bounds_cache(const GLT::asset::mesh::mesh_asset& mesh);

//         bool load_from_handle(GLT::asset::handle h);

//         void handle_drag_drop();


//         std::filesystem::path                                   m_mesh_path{};
//         GLT::asset::handle                                      m_asset_handle = INVALID_HANDLE;
//         std::string                                             m_mesh_name{};

//         bool                                                    m_has_mesh = false;
//         bool                                                    m_load_failed = false;
//         bool                                                    m_dirty = false;

//         std::vector<submesh_bounds>                             m_submesh_bounds_cache{};

//         // ---- preview -----------------------------------------------------------------------------------------------

//         GLT::render::preview_id                                 m_preview_id = 0;
//         GLT::render::preview_settings                           m_preview_settings{};
//         GLT::render::preview_camera                             m_preview_camera{};
//         bool                                                    m_auto_rotate = false;
//         f32                                                     m_auto_rotate_speed = 0.5f;
//         bool                                                    m_preview_dirty = false;
//         glm::uvec2                                              m_last_preview_size{ 0, 0 };

//         // Hover / selection highlight state. Only one id is pushed to the preview, hovered winning over selected.
//         i32                                                     m_hovered_submesh = -1;
//         i32                                                     m_selected_submesh = -1;
//         bool                                                    m_preview_image_drawn = false;
//     };

// }
