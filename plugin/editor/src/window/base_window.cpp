
#include "util/pch.h"
#include "base_window.h"

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

    // CLASS PUBLIC ====================================================================================================

    void base_window::focus_window() {

        if (m_window_id.empty()) {
            LOG(warn, "focus_window() called before make_window_name()");
            return;
        }

        ImGui::SetWindowFocus(m_window_id.c_str());
    }


    void base_window::dock_to(ImGuiID dock_id) { m_pending_dock_id = dock_id; }

    // CLASS PROTECTED =================================================================================================


    void base_window::apply_pending_dock() {

        if (m_pending_dock_id != 0) {
            ImGui::SetNextWindowDockID(m_pending_dock_id, ImGuiCond_Always);
            m_pending_dock_id = 0;      // one-shot: the user may undock afterwards
        }
    }


    void base_window::make_window_name(const char* base_name) {

        ASSERT(base_name, "", "make_window_name() base_name must not be null");

        m_window_title = base_name;
        m_window_id = std::string(base_name) + "##" + std::to_string(reinterpret_cast<uintptr_t>(this));
    }

    // CLASS PRIVATE ===================================================================================================

}
