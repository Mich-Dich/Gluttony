
#pragma once

#include <imgui.h>

#include "undo_system/stack.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct window_specs {

        bool                            can_be_deleted = true;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Abstract base class for all ImGui-based editor windows.
    //
    // Provides common functionality: window naming (with unique ID), visibility toggling,
    // focus management, and a circular buffer for undo/redo operations.
	class base_window {
	public:

        // Default constructor.
        base_window() {}


        // Virtual destructor.
        virtual ~base_window() {}


        DEFAULT_GETTER_C(std::string,           window_id)      // Returns the unique ImGui window ID (used for window identification).
        DEFAULT_GETTER_C(std::string,           window_title)
        DEFAULT_GETTER(window_specs,            window_specs)

        // Checks whether the window should be closed.
        // @return True if the window is not currently shown (i.e., closed).
        FORCE_INLINE_R bool should_close() const;


        // Renders the ImGui window contents.
        // @param delta_time Time elapsed since the last frame (seconds), may be used for animations.
        //
        // This method is called every frame while the window is visible. It must contain all ImGui widgets and layout for the editor.
        virtual void window(const f32 delta_time) = 0;


        // Performs per‑frame updates that are not directly tied to rendering.
        // @param deltaTime Time elapsed since the last frame (seconds).
        //
        // Typically used for logic updates, data processing, or polling that should happen even when the window is not visible.
        virtual void update(const f32 delta_time) = 0;

        IGNORE_UNUSED_PARAMETER_START

        // Serializes or deserializes the editor's data to/from a project file.
        // @param projectFile Path to the project file.
        // @param option      Operation: load or save.
        // @return bool       True on success, false otherwise.
        //
        // Default implementation returns true (does nothing). Derived classes should override to implement actual serialization.
        virtual bool serialize(const std::filesystem::path& project_file, const GLT::serializer::option option) { return false; }

        IGNORE_UNUSED_PARAMETER_STOP


        // Optional method to show additional sub‑window options (e.g., in a menu).
        // Default implementation does nothing.
        virtual void show_possible_sub_window_options() {}


        // Forces a refresh of the editor's data (e.g., after external changes).
        // Default implementation does nothing.
        virtual void refresh() {}


        // Sets input focus to this ImGui window.
        // Uses ImGui::SetWindowFocus with the unique window ID.
        void focus_window();


        void close_window();


        // Requests that this window dock into `dock_id` the next time it is
        // rendered. No-op if the window has already been shown once; call
        // this before the first frame the window is drawn.
        virtual void dock_to(ImGuiID dock_id);

    protected:

        // Call immediately before ImGui::Begin() to honor a pending dock request.
        void apply_pending_dock(const bool force = false);


        // Snapshot the current ImGui window state (position, size, dock id,
        // collapsed flag). Call this BEFORE renaming the window via
        // make_window_name() so the state can survive the ID change.
        //
        // No-op if the window hasn't been shown yet (first frame).
        void cache_window_state();


        // Creates a unique ImGui window ID by appending the object's address
        // to the base name. The generated ID is stored in m_window_id.
        void make_window_name(const char* base_name);


        // One-shot state restored by the next apply_pending_dock() call.
        // Used to carry position/size/dock across window renames, since
        // ImGui keys its state by the full Begin() string (including the
        // visible part before '##').
        struct window_state_cache {

            bool                            valid = false;
            ImVec2                          pos = ImVec2(0.0f, 0.0f);
            ImVec2                          size = ImVec2(0.0f, 0.0f);
            ImGuiID                         dock_id = 0;
            bool                            panding = false;
            bool                            collapsed = false;
        };

        window_state_cache                  m_window_state_cache{};

        std::string                         m_window_id{};                  // Unique ImGui window identifier (e.g., "Editor##0x7FF...")
        std::string                         m_window_title{};               // Display title shown in the window title bar.
        bool                                m_show_window = true;           // Set to false to close/hide the window.
        bool                                m_window_visible = true;        // Can be used for performance optimizations (e.g., skip rendering when false).

        // Undo/redo system using a fixed‑size circular buffer of serialized states.
        bool                                m_enable_undo_system = false;   // Enables the undo/redo system (set by derived classes).
        undo_system::stack                  m_undo_stack{};
        ImGuiID                             m_pending_dock_id = 0;
        window_specs                        m_window_specs{};

	};

}

#include "base_window.inl"
