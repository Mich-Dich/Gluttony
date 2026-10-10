
#pragma once

#include <imgui.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <filesystem>
#include <vector>

#include <asset/type.h>
#include <asset/asset_ref.h>

#include "window/base/asset_editor.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {
    class i_world_scene;
}

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Top-down (X/Z) view of a world's contents. Entities with a transform are shown as dots; a dashed grid
    // whose cell size is user-controlled acts as a preview of the region layout the eventual region-builder
    // will use. Cells that contain at least one entity are highlighted.
    //
    // Bound to exactly one .glt_world file, so it derives from asset_editor and the layer will dedupe opens.
    class world_topdown_editor final : public asset_editor {
    public:

        explicit world_topdown_editor(const std::filesystem::path& path);
        ~world_topdown_editor() override = default;

        // asset_editor -------------------------------------------------------------------------------------

        [[nodiscard]] const std::filesystem::path& get_asset_path() const noexcept override { return m_asset_path; }

        // base_window --------------------------------------------------------------------------------------

        void window(f32 delta_time) override;
        void update(f32 delta_time) override;

    private:

        struct editor_entity {
            glm::vec2                   xz{};
            u32                         index{ 0 };
            u32                         generation{ 0 };
        };

        // drawing ------------------------------------------------------------------------------------------

        void draw_sidebar();

        void draw_view();

        void fit_to_entities(const ImVec2& canvas_size);

        void refresh_entities();

        // --- asset ----------------------------------------------------------------------------------------

        std::filesystem::path           m_asset_path{};
        GLT::asset::asset_ref           m_world{};
        bool                            m_load_failed{ false };

        std::vector<editor_entity>      m_entities{};

        // Cached once on open and whenever the world plugin pushes a change.
        glm::vec3                       m_cell_size_from_world{ 256.f };

        // Set to true on construction and after Apply so the sidebar sliders
        // reflect what's actually on disk. Cleared once consumed.
        bool                            m_sync_sliders_pending{ true };

        // --- region cell size, in meters ------------------------------------------------------------------

        // UI-only. Apply pushes these into the world plugin.
        int                             m_cell_x{ 256 };
        int                             m_cell_y{ 256 };
        int                             m_cell_z{ 256 };

        std::string                     m_last_action{};                    // status

        // --- view state -----------------------------------------------------------------------------------

        glm::vec2                       m_view_center{ 0.0f, 0.0f };        // world X/Z the view is centered on
        f32                             m_pixels_per_meter{ 2.0f };
        bool                            m_fit_pending{ true };

        // --- view toggles ---------------------------------------------------------------------------------

        bool                            m_show_grid{ true };
        bool                            m_show_entity_dots{ true };
        bool                            m_show_occupied_cells{ true };
        bool                            m_show_axes{ true };
    };

}
