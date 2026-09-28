#pragma once

#include <imgui.h>

#include <array>
#include <filesystem>
#include <string>

#include <asset/type.h>
#include <asset/material.h>

#include "window/base_window.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Dockable editor window that displays and edits a material asset.
    //
    // Two construction modes:
    //   - material_viewer_window()             -> opens an unsaved, in-memory
    //                                             material with default params.
    //   - material_viewer_window(path)         -> loads the .glt_material at
    //                                             `path` through the asset registry.
    //
    // The window edits a local copy of material_params (m_edit) plus a local copy
    // of the texture handles. Nothing is written back to the underlying asset until
    // save() is called, which copies the edit buffers into the asset and asks the
    // registry to serialize it back to disk.
    class material_viewer_window : public base_window {
    public:

        material_viewer_window();
        material_viewer_window(const std::filesystem::path& path);
        ~material_viewer_window();

        DEFAULT_GETTER(GLT::asset::handle,          asset_handle)
        DEFAULT_GETTER_C(std::filesystem::path,     material_path)
        DEFAULT_GETTER(bool,                        dirty)


        // Loads a material through the registry. An empty `path` opens a
        // fresh in-memory material with default parameters.
        void open(const std::filesystem::path& path);


        // Releases the currently bound material asset (registry-unload).
        void close_material();


        void window(const f32 delta_time) override;


        void update(const f32 delta_time) override;


        bool serialize(const std::filesystem::path& project_file, GLT::serializer::option option) override;


        // Writes the local edit buffers back through the registry. Returns
        // false if the material has no file backing yet.
        bool save();


        // Restores the local edit buffers to the material's baseline state.
        void revert();


    private:

        static constexpr std::size_t TEXTURE_SLOT_COUNT =
            static_cast<std::size_t>(GLT::asset::material::texture_slot::count);

        using texture_handles = std::array<GLT::asset::handle, TEXTURE_SLOT_COUNT>;


        // drawing helpers ---------------------------------------------------------------------------------------------
        void draw_details_panel();

        void draw_material_panel();

        void draw_identity_section();

        void draw_pbr_section();

        void draw_emission_section();

        void draw_surface_section();

        void draw_flags_section();

        void draw_preview_canvas();

        void mark_dirty();

        void build_new_material();

        bool load_from_handle(GLT::asset::handle h);

        void handle_preview_input();

        void draw_preview_fallback(ImDrawList* draw, const ImVec2& min, const ImVec2& max);


        std::filesystem::path                       m_material_path{};
        GLT::asset::handle                          m_asset_handle = INVALID_HANDLE;
        std::string                                 m_material_name{};

        bool                                        m_has_material = false;
        bool                                        m_load_failed = false;
        bool                                        m_dirty = false;

        // Local edit buffers. The window edits these, never the asset directly.
        GLT::asset::material::material_params       m_edit{};
        GLT::asset::material::material_params       m_baseline{};

        texture_handles                             m_edit_textures{};
        texture_handles                             m_baseline_textures{};

        // Preview camera. Position is free in world space; the renderer is
        // handed `m_preview_camera_pos` and looks at `m_preview_target`.
        glm::vec3                                   m_preview_camera_pos{ 0.0f, 0.0f, 3.0f };
        glm::vec3                                   m_preview_target{ 0.0f, 0.0f, 0.0f };
    };

}
