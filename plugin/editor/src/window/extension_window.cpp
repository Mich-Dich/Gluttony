
#include "util/pch.h"
#include "extension_window.h"

#include <imgui.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    extension_window::extension_window(std::string owner, GLT::unique_ref<GLT::editor::i_editor_window> content)
        : m_owner(std::move(owner)), m_content(std::move(content)) {

        const std::string title = m_content ? m_content->title() : std::string("Plugin Window");
        make_window_name(title.empty() ? "Plugin Window" : title.c_str());
        m_window_specs.can_be_deleted = true;
        m_show_window = m_content != nullptr;
    }


    extension_window::~extension_window() = default;

    // CLASS PUBLIC ====================================================================================================

    void extension_window::window(const f32 delta_time) {

        if (!m_show_window || !m_content)
            return;

        apply_pending_dock();
        const bool open = ImGui::Begin(m_window_id.c_str(), &m_show_window, static_cast<ImGuiWindowFlags>(m_content->window_flags()));
        if (open) {
            track_focus();
            m_content->draw_content(delta_time);
        }
        ImGui::End();
    }


    void extension_window::update(const f32 delta_time) {

        if (!m_content) {
            m_show_window = false;
            return;
        }

        m_content->update(delta_time);
        if (m_content->wants_close())
            m_show_window = false;
    }


    std::filesystem::path extension_window::asset_path() const { return m_content ? m_content->asset_path() : std::filesystem::path{}; }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
