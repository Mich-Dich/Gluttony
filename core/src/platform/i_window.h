
#pragma once

#include <glm/vec2.hpp>

#include "event/event.h"
#include "plugin_system/i_plugin.h"
#include "plugin_system/plugin_manager.h"
#include "render/type.h"
#include "platform/type.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::platform {
    class i_window_plugin;
}

namespace GLT::platform {

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // CONSTANTS =======================================================================================================

    inline constexpr window_handle          INVALID_WINDOW_HANDLE{};

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // @brief Loads or saves the primary window's attributes to the project's config file
    //
    // Free function, kept for the single‑primary‑window persistence path. Secondary windows are persisted by the
    // application (or a future multi-window overload). Behaviour and signature match the pre‑multi‑window version
    void serialize_window_attributes(const std::filesystem::path& path, GLT::platform::window_attributes& attributes,
        const serializer::option option);


    namespace window {

        // @brief Hot-path accessor: cached raw pointer, thread-safe, no refcount traffic. Cache the result and compare
        // [plugin_manager::get_generation()] for invalidation
        // @return nullptr if the window plugin is not loaded
        FORCE_INLINE_R GLT::platform::i_window_plugin* get_ptr() noexcept {

            return GLT::plugin_manager::get_plugin_raw<GLT::platform::i_window_plugin>(GLT::plugin_manager::interface::window);
        }

        // @brief Owning reference for cases where you need to keep the plugin alive across unload phases
        FORCE_INLINE_R ref<GLT::platform::i_window_plugin> get_ref() {

            return GLT::plugin_manager::get_plugin_ref_unsafe<GLT::platform::i_window_plugin>(GLT::plugin_manager::interface::window);
        }

    }

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Platform plugin: owns every OS window created by the engine
    //
    // One window plugin is loaded at a time (interface::window). It is multi‑window capable: the caller uses
    // [create()] to obtain a [window_handle] per OS window, and passes that handle into every subsequent method
    //
    // Threading: all methods must be called from the thread that owns the platform event loop (the main thread)
    // GLFW does not support cross‑thread window operations. If a value is needed on a worker thread, cache it
    // on the main thread and read the cached copy elsewhere
    class i_window_plugin : public GLT::plugin_manager::i_plugin {
    public:

        virtual ~i_window_plugin() = default;

        // --- lifecycle ---------------------------------------------------------------------------------------------

        // @brief Creates a native window and returns its handle
        //
        // The first call lazily initialises the platform library (GLFW). Subsequent calls create additional
        // windows on the same library instance. On failure, logs the reason and returns [INVALID_WINDOW_HANDLE];
        // this is not a fatal state — the caller may retry or fall back to a headless path
        //
        // @param attrs  Initial window attributes (size, position, title, vsync, state)
        // @return A valid handle, or [INVALID_WINDOW_HANDLE] on failure
        [[nodiscard]] virtual window_handle create(const window_attributes& attrs) = 0;


        // @brief Destroys the window identified by [handle]
        //
        // Safe to call with an invalid or already‑destroyed handle (no‑op with a diagnostic log). The slot is
        // returned to the free list and its generation is bumped on the next [create()], invalidating every
        // outstanding copy of the old handle. Any ImGui context bound to this window is torn down first
        virtual void destroy(window_handle handle) = 0;


        // @brief Destroys every live window (shutdown convenience)
        //
        // Equivalent to calling [destroy()] on each live handle; safe to call while other handles are outstanding
        virtual void destroy_all() = 0;

        // --- queries -----------------------------------------------------------------------------------------------

        // @brief Cheap validity check (bounds + generation compare, no GLFW call)
        [[nodiscard]] virtual bool is_valid(window_handle handle) const noexcept = 0;


        // @brief Reports whether the OS has requested that this window close
        // @return true once a close request has been received; true (fail‑safe) for an invalid handle
        [[nodiscard]] virtual bool should_close(window_handle handle) const = 0;


        // @brief Client‑area size in logical pixels (DPI‑independent)
        [[nodiscard]] virtual glm::ivec2 get_window_size(window_handle handle) const = 0;


        // @brief Framebuffer size in physical pixels
        //
        // Can differ from [get_window_size()] on high‑DPI displays. Renderers size their framebuffers off this
        [[nodiscard]] virtual glm::ivec2 get_framebuffer_size(window_handle handle) const = 0;


        // @brief Screen‑space position of the window's top‑left corner, in screen pixels
        [[nodiscard]] virtual glm::ivec2 get_position(window_handle handle) const = 0;


        // @brief Current window size category
        [[nodiscard]] virtual window_size_state get_state(window_handle handle) const = 0;


        // @brief Reports whether vertical sync is currently enabled for this window
        [[nodiscard]] virtual bool get_vsync(window_handle handle) const = 0;


        // @brief Snapshots the window's current attributes
        //
        // Values reflect live window state (position, size, vsync, size category), not the attributes originally
        // passed to [create()]. Use this when persisting window state to the project config
        [[nodiscard]] virtual window_attributes get_window_attributes(window_handle handle) const = 0;


        // @brief Number of currently live windows (O(1))
        [[nodiscard]] virtual u32 get_window_count() const noexcept = 0;

        // --- modifiers ---------------------------------------------------------------------------------------------

        virtual void show(window_handle handle, bool visible) = 0;
        virtual void set_state(window_handle handle, window_size_state new_state) = 0;
        virtual void set_title(window_handle handle, const std::string& title) = 0;
        virtual void set_size(window_handle handle, u32 width, u32 height) = 0;
        virtual void set_vsync(window_handle handle, bool vsync) = 0;
        virtual void set_cursor_mode(window_handle handle, cursor_mode mode) = 0;

        // --- event loop --------------------------------------------------------------------------------------------

        // @brief Pumps the platform event queue for *all* windows and dispatches events onto the bus
        //
        // Must be called once per frame on the main thread. Per‑window callbacks recover the originating handle
        // from the user pointer, so every window‑originated event carries the correct [window_handle]
        virtual void poll_events() = 0;

        // --- backend info (process‑global) -------------------------------------------------------------------------

        [[nodiscard]] virtual backend_api  get_backend_api() const = 0;
        [[nodiscard]] virtual const char** get_required_render_extensions(u32* count) const = 0;

        // --- renderer interop --------------------------------------------------------------------------------------

        // @brief Returns a void* that render backends can cast (GLFWwindow*, HWND, …)
        [[nodiscard]] virtual void* get_native_window_handle(window_handle handle) = 0;

        // --- ImGui (per‑window) ------------------------------------------------------------------------------------

        // @brief Initialises the GLFW backend against the CURRENT ImGui context, binding this window to it
        //
        // The caller MUST create (or own) an ImGui context and make it current before calling. This is
        // the standard ImGui backend init pattern: backends register against the caller's context; they
        // do not create their own.
        //
        // The plugin records the current context so it can:
        //   - forward GLFW events (char / mouse / scroll) to ImGui from the correct context
        //   - make that same context current again in begin_imgui_frame()
        //
        // Note: a single ImGui context can only bind to one native window at a time — the GLFW backend
        // stores one GLFWwindow* per context. For multi-window ImGui, create a context per window and
        // call this once per (window, context) pair. Multi-window 3D rendering (no ImGui on the second
        // window) works with a single shared context.
        virtual void imgui_init(window_handle handle, GLT::render::backend_api used_render_api) = 0;


        // @brief Shuts the GLFW backend down for this window. Does NOT destroy the ImGui context.
        virtual void imgui_shutdown(window_handle handle) = 0;


        // @brief Makes this window's recorded ImGui context current and begins a new backend frame.
        //
        // Call once per window per frame. The context remains current after this call so the caller can
        // immediately call ImGui::NewFrame().
        virtual void begin_imgui_frame(window_handle handle) = 0;

    };

}
