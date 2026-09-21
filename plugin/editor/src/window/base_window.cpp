
#include "util/pch.h"
#include "base_window.h"

#include <imgui.h>
#include <imgui_internal.h> 



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


    void base_window::close_window() { m_show_window = false; }


    void base_window::dock_to(ImGuiID dock_id) { m_pending_dock_id = dock_id; }

    // CLASS PROTECTED =================================================================================================

    void base_window::apply_pending_dock() {

        // One-shot dock request from dock_to().
        if (m_pending_dock_id != 0) {
            ImGui::SetNextWindowDockID(m_pending_dock_id, ImGuiCond_Always);
            // m_pending_dock_id = 0;
        }

        // Restore state cached before a rename. Order doesn't matter for these
        // SetNext* calls - ImGui consumes them all inside Begin() - but dock
        // and collapsed are applied after pos/size so that a docked+collapsed
        // window doesn't briefly pop to a free-floating position first.
        if (m_window_state_cache.valid) {

            ImGui::SetNextWindowPos (m_window_state_cache.pos,  ImGuiCond_Always);
            ImGui::SetNextWindowSize(m_window_state_cache.size, ImGuiCond_Always);

            if (m_window_state_cache.dock_id != 0)
                ImGui::SetNextWindowDockID(m_window_state_cache.dock_id, ImGuiCond_Always);

            if (m_window_state_cache.collapsed)
                ImGui::SetNextWindowCollapsed(true, ImGuiCond_Always);

            m_window_state_cache.valid = false;   // one-shot
        }
    }


    void base_window::cache_window_state() {

        ImGuiWindow* win = ImGui::FindWindowByName(m_window_id.c_str());
        if (!win)
            return;     // not shown yet - nothing to cache

        m_window_state_cache.valid = true;
        m_window_state_cache.pos = win->Pos;
        m_window_state_cache.size = win->Size;
        m_window_state_cache.dock_id = win->DockId;
        m_window_state_cache.collapsed = win->Collapsed;
    }


    void base_window::make_window_name(const char* base_name) {

        ASSERT(base_name, "", "make_window_name() base_name must not be null");

        m_window_title = base_name;
        m_window_id = std::string(base_name) + "##" + std::to_string(reinterpret_cast<uintptr_t>(this));
    }

    // CLASS PRIVATE ===================================================================================================

}
