
#pragma once

#include "plugin_interface.h"
#include "event/event.h"               // core event base class
#include <glm/vec2.hpp>

#include <plugin_system/i_renderer_plugin.h>

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::platform {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    enum class backend_api : u8 {
        glfw = 0,
        sdl,
    };


    enum class window_size_state : u8 {
        windowed,
        minimized,
        fullscreen,
        fullscreen_windowed
    };


    enum class cursor_mode : u8 {
        cursor_normal = 0,
        cursor_hidden,
        cursor_disabled,
        cursor_captured,
    };


    struct window_attributes {
        std::string             title = "Gluttony";
        u32                     width = 1600;
        u32                     height = 900;
        u32                     pos_x = 100;
        u32                     pos_y = 100;
        bool                    vsync = false;
        window_size_state       size_state = window_size_state::windowed;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class i_window_plugin : public GLT::plugin_manager::i_plugin {
    public:

        virtual ~i_window_plugin() = default;

        // --- lifecycle ---
        virtual void create(const window_attributes& attrs) = 0;
        virtual void destroy() = 0;

        // --- queries ---
        virtual bool should_close() const = 0;
        virtual glm::ivec2 get_window_size() const = 0;
        virtual glm::ivec2 get_framebuffer_size() const = 0;
        virtual glm::ivec2 get_position() const = 0;
        virtual window_size_state get_state() const = 0;
        virtual bool is_vsync() const = 0;

        // --- modifiers ---
        virtual void show(bool visible) = 0;
        virtual void set_state(const window_size_state new_state) = 0;
        virtual void set_title(const std::string& title) = 0;
        virtual void set_size(u32 width, u32 height) = 0;
        virtual void set_vsync(bool vsync) = 0;
        virtual void set_cursor_mode(cursor_mode mode) = 0;   // normal, hidden, captured

        virtual void poll_events() = 0;

        virtual backend_api get_backend_api() = 0;
        virtual const char** get_required_render_extensions(u32* count) = 0;
        virtual void imgui_init(GLT::render::backend_api used_render_api) = 0;
        
        // Returns a void* that render backends can cast (GLFWwindow*, HWND, etc.).
        virtual void* get_native_window_handle() = 0;
        virtual window_attributes get_window_attributes() = 0;

    };
}
