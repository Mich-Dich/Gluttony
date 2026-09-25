
#pragma once

#include <imgui.h>

#include <plugin_system/i_world_plugin.h>
#include <world/i_world_inspector.h>

#include "window/base_window.h"
#include "window/content_browser.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::render {
    class i_renderer_plugin;
    class image;
}

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class world_viewport_window : public base_window {
    public:

        world_viewport_window();
        ~world_viewport_window();


        void window(const f32 delta_time) override;


        void update(const f32 delta_time) override;


        bool serialize(const std::filesystem::path& project_file, const GLT::serializer::option option) override;

    private:

        void render_inner_dockspace();

        void build_default_layout(ImGuiID dockspace_id, const ImVec2& size);

        void render_viewport();

        void render_details();

        void render_outliner();

        void set_cursor_captured(const bool captured);

        void render_outliner_node(GLT::world::entity_id id);

        std::vector<content_browser_window>             m_content_browsers{};
        GLT::ref<GLT::render::i_renderer_plugin>        m_renderer{};
        ImVec2                                          m_viewport_size{100, 60};
        bool                                            m_reset_layout = true;
        bool                                            m_cursor_captured = false;

        GLT::ref<GLT::world::i_world_plugin>            m_world{};
        GLT::world::entity_id                           m_selected_entity{ GLT::world::INVALID_ENTITY };
        GLT::world::i_world_inspector*                  m_inspector{ nullptr };
        u64                                             m_clipboard_component{ 0 };
    };

}
