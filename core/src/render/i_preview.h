#pragma once

#include "util/pch.h"
#include "asset/type.h"
#include "asset/material.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::render {

    // CONSTANTS =======================================================================================================

    using preview_id = u64;

    // Returned by create() on failure and by all operations on an invalid id
    inline constexpr preview_id                 INVALID_PREVIEW_ID = 0;

    // TYPES ===========================================================================================================

    // @brief Primitive shape used when [preview_source::primitive] is selected
    enum class preview_primitive : u8 {

        sphere = 0,
        cube,
        plane,
        cylinder,
        torus,
        count
    };


    // @brief What the preview renders
    enum class preview_source : u8 {

        primitive = 0,          // use [preview_desc::shape]
        mesh_asset,             // use [preview_desc::mesh]
        count
    };


    // @brief How the surface is visualised
    // mesh-editor consumers typically pick wireframe_overlay / normals / uvs; the material editor stays on lit
    enum class preview_render_mode : u8 {

        lit = 0,                // material-evaluated, lit (default)
        unlit,                  // material-evaluated, no lighting
        wireframe,              // wireframe only
        wireframe_overlay,      // lit + wireframe overlay
        normals,                // world-space normals
        uvs,                    // UV visualization
        count
    };


    // @brief What to show in a preview
    //
    // Material editor: source = primitive, shape = sphere, material = target, overrides = live edits
    // Mesh editor:     source = mesh_asset, mesh = target, material = INVALID_HANDLE (use mesh's own)
    // Instance editor: source = primitive, shape = sphere, material_instance = target
    struct preview_desc {

        preview_source                          source = preview_source::primitive;
        preview_primitive                       shape = preview_primitive::sphere;
        GLT::asset::handle                      mesh = INVALID_HANDLE;

        // If set: every submesh renders with this material (material-editor use case)
        // If INVALID_HANDLE: each submesh uses its own material; primitives use the default material
        GLT::asset::handle                      material = INVALID_HANDLE;

        // If set: resolves through [material_instance_asset] (base material + baked overrides)
        // Takes precedence over [material] if both are set
        GLT::asset::handle                      material_instance = INVALID_HANDLE;
    };


    // @brief Rendering options for a preview
    struct preview_settings {

        glm::vec4                               background{ 0.1f, 0.1f, 0.12f, 1.f };
        preview_render_mode                     render_mode = preview_render_mode::lit;
        bool                                    show_grid = true;
        bool                                    show_origin = false;
    };


    // @brief Camera controls for a preview (orbit around [target])
    struct preview_camera {

        glm::vec3                               target{ 0.f };
        f32                                     yaw_rad = 0.7f;
        f32                                     pitch_rad = 0.3f;
        f32                                     distance = 3.f;
        f32                                     fov_deg = 45.f;
    };


    // @brief Per-slot material overrides for uncommitted edits
    //
    // Precedence: preview_material_overrides > material_instance > material
    // The renderer copies [packed_params] internally — the caller may free the buffer immediately after set_material_overrides() returns
    struct preview_material_overrides {

        // Replaces the material's params entirely. Must match the material's reflection layout.
        // Empty span = no param override
        std::span<const std::byte>              packed_params{};

        // Per texture-slot override:
        //   nullopt                   -> keep the material's texture
        //   optional{valid_handle}    -> replace with this texture
        //   optional{INVALID_HANDLE}  -> clear (falls back to white / flat normal)
        std::array<std::optional<GLT::asset::handle>, static_cast<size_t>(GLT::asset::material::texture_slot::count)> textures{};
    };

    // CLASS DECLARATION ===============================================================================================

    // @brief Offscreen preview rendering for editors
    //
    // Renderers opt in by inheriting from this interface. Callers reach it via
    // [i_plugin::as<i_preview>()] on the active renderer plugin (same pattern as [i_debug_draw]).
    //
    // Each preview has its own render target, camera, and content. Multiple previews may be active in parallel
    // (material editor, mesh editor, inspector thumbnails, ...). All setters are called from the update thread;
    // changes take effect on the next frame.
    //
    // [get_render_target()] returns an ImGui-compatible texture handle. The first frame after [create()] returns
    // a blank image; subsequent frames show the previous render.
    class i_preview {
    public:

        virtual ~i_preview() = default;

        // lifecycle ---------------------------------------------------------------------------------------------------

        // @brief Allocates a preview and its render target
        // @param desc  Initial content
        // @param size  Render target size in pixels
        // @return A non-zero preview id, or [INVALID_PREVIEW_ID] on failure
        virtual preview_id create(const preview_desc& desc, const glm::uvec2& size) = 0;


        // @brief Releases the preview and its render target
        // The renderer defers the GPU free until no frame in flight references it
        virtual void destroy(preview_id id) = 0;


        // @brief Enables or disables rendering without destroying the preview
        // Inactive previews skip their passes entirely — useful when an editor panel is hidden
        virtual void set_active(preview_id id, bool active) = 0;

        // content -----------------------------------------------------------------------------------------------------

        virtual void set_desc(preview_id id, const preview_desc& desc) = 0;


        virtual void set_settings(preview_id id, const preview_settings& settings) = 0;


        virtual void set_camera(preview_id id, const preview_camera& camera) = 0;


        virtual void set_material_overrides(preview_id id, const preview_material_overrides& overrides) = 0;

        // inspection --------------------------------------------------------------------------------------------------

        // @brief Enables auto-rotation around the camera target
        // The renderer adds [speed_rad_per_sec] * dt to the yaw each frame, on top of [preview_camera::yaw_rad]
        virtual void set_auto_rotate(preview_id id, bool enabled, f32 speed_rad_per_sec = 0.5f) = 0;


        // @brief Highlights the given submeshes (mesh-editor use case)
        // Empty span clears the highlight
        virtual void set_highlight_submeshes(preview_id id, std::span<const u32> submesh_indices) = 0;

        // query -------------------------------------------------------------------------------------------------------

        // @brief ImGui-compatible texture handle for the latest preview render
        // nullptr if the id is invalid. The handle is stable for the preview's lifetime
        [[nodiscard]] virtual void* get_render_target(preview_id id) const = 0;


        [[nodiscard]] virtual glm::uvec2 get_size(preview_id id) const = 0;


        // @brief Resizes the render target; takes effect on the next frame
        virtual void set_size(preview_id id, const glm::uvec2& size) = 0;

    };

}
