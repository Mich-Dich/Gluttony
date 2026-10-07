
#pragma once

#include "plugin_system/i_plugin.h"
#include "event/event.h"               // core event base class
#include <glm/vec2.hpp>

#include "plugin_system/plugin_manager.h"
#include "render/i_renderer.h"



// FORWARD DECLARATIONS ================================================================================================

namespace vk {
    class SurfaceKHR;
    struct Instance;
}

namespace GLT::platform {
    class i_window_plugin;
}

namespace GLT::platform {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // @brief Which platform library backs this window implementation
    //
    // Every platform plugin declares one; the application queries it to know which native types to expect out of 
    // [get_native_window_handle() and which extension list to feed the renderer
    enum class backend_api : u8 {

        glfw = 0,
        sdl,
    };


    // @brief The window's current size category
    //
    // Distinct from "is the window visible" - [minimized] implies the window is not visible, but a window can also be
    // hidden without being minimized. [fullscreen] and [fullscreen_windowed] differ in whether the platform is allowed to
    // change the display mode
    enum class window_size_state : u8 {

        windowed,
        minimized,
        fullscreen,
        fullscreen_windowed
    };


    // @brief How the OS cursor behaves while the window is focused
    enum class cursor_mode : u8 {

        cursor_normal = 0,      // Free cursor, OS cursor visible
        cursor_hidden,          // OS cursor hidden, but its position still updates
        cursor_disabled,        // Cursor locked to the window center, deltas reported
        cursor_captured,        // Cursor confined to the window client area
    };


    // @brief Everything needed to create (and later persist) a window
    //
    // Fields are plain POD so this can be round-tripped through the config serializer without a custom codec. Defaults
    // describe a 1600x900 windowed window named "Gluttony" at (100, 100) with vsync off
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

    // @brief Loads or saves a window's attributes to the project's config file
    //
    // Writes to [<path>/<CONFIG_DIR>/<app_settings><FILE_EXTENSION_CONFIG>] under a top-level "window" section
    // Creates the file if it doesn't exist; silently returns (with a log) if [path] isn't a directory or the file can't be opened
    //
    // @param path        Project directory to read from / write to
    // @param attributes  Attributes to serialize, or to populate on load
    // @param option      Read, write, or both (see GLT::serializer::option)
	void serialize_window_attributes(const std::filesystem::path& path, GLT::platform::window_attributes& attributes, 
        const serializer::option option);


    // @brief Fetches the process-wide window plugin
    // @return Strong reference to the window plugin, or an empty ref if it isn't loaded
    FORCE_INLINE_R ref<GLT::platform::i_window_plugin> get_window_ref() {

        return GLT::plugin_manager::get_plugin_ref<platform::i_window_plugin>(plugin_manager::interface::window);
    }

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Platform plugin: owns the OS window and everything tied to it
    //
    // One window plugin is loaded at a time and selected by the application before [create()] is called
    // It handles window lifecycle, event polling, and the renderer-side handshakes needed to build a swap chain:
    // @brief - native handle handoff for whichever graphics API is in use,
    // @brief - required-instance-extension list for the renderer,
    // @brief - Vulkan surface creation from the native handle,
    // @brief - ImGui backend init / frame / shutdown
    //
    // Callers should never talk to the platform library directly; they go through this interface (or the events it
    // broadcasts on the bus, see event/application_event.h for the window_* events)
    class i_window_plugin : public GLT::plugin_manager::i_plugin {
    public:

        virtual ~i_window_plugin() = default;

        // --- lifecycle ---

        // @brief Creates the native window and initializes the platform library
        // @param attrs  Initial window attributes (size, position, title, vsync, state)
        virtual void create(const window_attributes& attrs) = 0;


        // @brief Destroys the native window and shuts the platform library down
        //
        // After this returns, no other method on this plugin may be called until [create()] is invoked again
        virtual void destroy() = 0;

        // --- queries ---

        // @brief Reports whether the OS has requested that the window close
        // @return true once a close request has been received
        virtual bool should_close() const = 0;


        // @brief Current client-area size, in logical pixels (DPI-independent)
        // @return Window size as (width, height)
        [[nodiscard]] virtual glm::ivec2 get_window_size() const = 0;


        // @brief Current framebuffer size, in physical pixels
        //
        // Can differ from [get_window_size()] on high-DPI displays. Renderers should size their framebuffers off this,
        // not off the window size
        //
        // @return Framebuffer size as (width, height)
        [[nodiscard]] virtual glm::ivec2 get_framebuffer_size() const = 0;


        // @brief Current screen-space position of the window's top-left corner
        // @return Window position as (x, y) in screen pixels
        [[nodiscard]] virtual glm::ivec2 get_position() const = 0;


        // @brief Current window size category (windowed / minimized / fullscreen)
        [[nodiscard]] virtual window_size_state get_state() const = 0;


        // @brief Reports whether vertical sync is currently enabled
        [[nodiscard]] virtual bool get_vsync() const = 0;

        // --- modifiers ---

        // @brief Shows or hides the window
        // @param visible  true to show, false to hide
        virtual void show(bool visible) = 0;


        // @brief Transitions the window into a new size category
        //
        // Implementations should also emit the appropriate window event(s) so subscribers can react to the change
        // (framebuffer resize, focus, …)
        //
        // @param new_state  Target state
        virtual void set_state(const window_size_state new_state) = 0;


        // @brief Replaces the window title
        // @param title  New title string
        virtual void set_title(const std::string& title) = 0;


        // @brief Resizes the window's client area
        // @param width   New width in logical pixels
        // @param height  New height in logical pixels
        virtual void set_size(u32 width, u32 height) = 0;


        // @brief Enables or disables vertical sync
        //
        // Most backends only apply the new setting when the swap chain is recreated; the renderer is responsible for that rebuild
        //
        // @param vsync  true to enable vsync
        virtual void set_vsync(bool vsync) = 0;


        // @brief Sets the OS cursor behaviour while the window is focused
        // @param mode  One of the [cursor_mode] values
        virtual void set_cursor_mode(cursor_mode mode) = 0;   // normal, hidden, captured


        // @brief Pumps the platform event queue and dispatches events onto the bus
        //
        // Must be called once per frame on the main thread. Also updates any cached state (should_close, framebuffer size,
        // cursor mode) that the queries return
        virtual void poll_events() = 0;


        // @brief Reports which platform library backs this plugin
        // @return The plugin's [backend_api] value
        [[nodiscard]] virtual backend_api get_backend_api() = 0;


        // @brief Returns the Vulkan instance extensions the renderer must enable
        //
        // Populated by the platform library (e.g. [VK_KHR_surface] plus its OS-specific sibling). The returned pointer points
        // into plugin-owned storage and stays valid until the next [create()]
        //
        // @param count  Out-parameter; set to the number of extension names
        // @return A [const char*] array of extension names
        [[nodiscard]] virtual const char** get_required_render_extensions(u32* count) = 0;

        
        // @brief Initializes the ImGui platform backend against this window
        // @param used_render_api  Which graphics API the renderer settled on
        virtual void imgui_init(GLT::render::backend_api used_render_api) = 0;


        // @brief Shuts the ImGui platform backend down
        virtual void imgui_shutdown() = 0;


        // @brief Begins a new ImGui frame
        //
        // Call once per frame, before the renderer starts recording ImGui draw data. Pairs with the renderer's ImGui frame end
        virtual void begin_imgui_frame() = 0;


        // Creates a Vulkan surface from the underlying native window
        // @param instance The Vulkan instance to use
        // @return A fully created vk::SurfaceKHR (the caller must destroy it)
        [[nodiscard]] virtual vk::SurfaceKHR create_vulkan_surface(vk::Instance instance) = 0;


        // Returns a void* that render backends can cast (GLFWwindow*, HWND, etc.)
        [[nodiscard]] virtual void* get_native_window_handle() = 0;


        // @brief Snapshots the window's current attributes
        //
        // Values reflect the live window state (position, size, vsync, size category), not the attributes originally passed
        // to [create()]. Use this when persisting window state to the project config
        //
        // @return A [window_attributes] struct describing the window as it is now
        [[nodiscard]] virtual window_attributes get_window_attributes() = 0;

    };

}
