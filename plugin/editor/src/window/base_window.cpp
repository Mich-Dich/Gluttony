
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

    // CLASS PROTECTED =================================================================================================

    void base_window::make_window_name(const char* base_name) {

        ASSERT(base_name, "", "make_window_name() base_name must not be null");

        m_window_title = base_name;

        // // ImGui displays everything before "##" and uses everything after for identity.
        // // Combining the human-readable name with the object's address guarantees a
        // // stable, unique ID per instance and gives ImGui's docking/position persistence
        // // a key that survives the window being closed and reopened.
        // m_window_id = fmt::format("{}##{:X}", base_name, reinterpret_cast<uintptr_t>(this));
    }

    // CLASS PRIVATE ===================================================================================================

}
