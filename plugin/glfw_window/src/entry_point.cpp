
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#if defined(PLATFORM_WINDOWS)
    #include <Windows.h>
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3native.h>
#endif

#include <plugin_system/i_window_plugin.h>  

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::glfw_window {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    static const char* dependencies_names[] = {

        nullptr
    };
    
    static GLT::plugin_manager::interface dependencies_interfaces[] = {
        
        GLT::plugin_manager::interface::none,
    };

    static GLT::plugin_manager::plugin_descriptor descriptor = {
        .name                               = GLT_MODULE_NAME,
        .phase                              = GLT::plugin_manager::load_phase::pre_application,
        .target                             = GLT::plugin_manager::interface::window,
        .dependency_names_count             = ARRAY_SIZE(dependencies_names),
        .dependency_names                   = dependencies_names,
        .dependency_interface_count         = ARRAY_SIZE(dependencies_interfaces),
        .dependency_interfaces              = dependencies_interfaces,
    };

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    // GLFW window plugin – implements i_window_plugin for Vulkan/GLFW backend.
    class plugin : public GLT::platform::i_window_plugin {
    public:

        // Lifecycle
            
        // Called by the plugin manager when the plugin is loaded.
        // Performs one-time setup that does not depend on a specific window.
        void on_load() override;


        // Called before the plugin is unloaded.
        // Releases any global resources allocated in on_load().
        void on_unload() override;


        // Creates a native GLFW window with the given attributes.
        // Initializes GLFW if not already done, sets window hints, creates the window,
        // applies initial position, size, vsync, and fullscreen state.
        // @param attributes – Desired title, dimensions, position, vsync, and window state.
        void create(const GLT::platform::window_attributes& attributes) override;


        // Destroys the native GLFW window.
        // Saves the current window position to the internal attributes before destruction.
        // Does NOT call glfwTerminate() – GLFW is kept initialized for potential future windows.
        void destroy() override;

        // ----- queries -----------------------------------------------------------------------------------------------

        // Returns true if the user has requested the window to close (e.g., by pressing the close button).
        bool should_close() const override;


        // Returns the current size of the client area of the window (in screen coordinates).
        glm::ivec2 get_window_size() const override;


        // Returns the current size of the framebuffer (in pixels, may differ on high‑DPI displays).
        glm::ivec2 get_framebuffer_size() const override;


        // Returns the current position of the window's top‑left corner (in screen coordinates).
        glm::ivec2 get_position() const override;


        // Returns the current window state: windowed, minimized, or fullscreen‑windowed.
        GLT::platform::window_size_state get_state() const override;


        // Returns true if vertical synchronization (VSync) is currently enabled for this window.        
        bool get_vsync() const override;

        // ----- modifiers ---------------------------------------------------------------------------------------------

        // Shows or hides the window.
        // @param visible – If true, the window becomes visible; otherwise it is hidden.
        void show(bool visible) override;


        // Changes the window state (minimized, fullscreen, windowed, etc.).
        // Internally calls glfwIconifyWindow, glfwMaximizeWindow, or glfwRestoreWindow.
        void set_state(const GLT::platform::window_size_state new_state) override;


        // Sets the window title.
        // @param title – New title string displayed in the window's title bar.
        void set_title(const std::string& title) override;


        // Sets the client area size of the window (ignoring window decorations).
        // @param width, height – New dimensions in screen coordinates.
        void set_size(u32 width, u32 height) override;


        // Enables or disables vertical synchronization (VSync) for this window.
        // Note: Current implementation is a placeholder – actual VSync control is deferred.
        void set_vsync(bool vsync) override;


        // Sets the cursor input mode (normal, hidden, disabled/captured).
        // @param mode – Desired cursor behaviour.
        void set_cursor_mode(GLT::platform::cursor_mode mode) override;

        // ----- events / backend --------------------------------------------------------------------------------------

        // Polls and dispatches all pending window, keyboard, mouse, and other input events.
        // Should be called once per main loop iteration.
        void poll_events() override;


        // Returns the backend API identifier used for window creation (always glfw).
        GLT::platform::backend_api get_backend_api() override;


        // Retrieves the list of Vulkan instance extensions required by GLFW for surface creation.
        // @param count – Output parameter receiving the number of extensions.
        // @return – Null‑terminated array of extension name strings.
        const char** get_required_render_extensions(u32* count) override;


        // Initializes ImGui’s GLFW backend for the current render API.
        // @param used_render_api – Render API (Vulkan, DirectX, Metal, or OpenGL).
        // Currently only Vulkan is fully implemented.
        void imgui_init(GLT::render::backend_api used_render_api);


        // Shuts down ImGui’s GLFW backend, releasing associated resources.
        void imgui_shutdown();


        // Begins a new ImGui frame by calling ImGui_ImplGlfw_NewFrame().
        // Must be called before any ImGui rendering commands for the current frame.
        void begin_imgui_frame();


        // Returns a pointer to the native GLFWwindow* handle.
        // Can be cast back to GLFWwindow* when needed by low‑level GLFW functions.
        void* get_native_window_handle() override;


        // Returns a copy of the window attributes (title, size, position, vsync, state) last used.
        GLT::platform::window_attributes get_window_attributes() override;


        // Creates a Vulkan surface for this window using the provided VkInstance.
        // @param instance – Valid Vulkan instance handle.
        // @return – vk::SurfaceKHR object that can be used for presentation.
        vk::SurfaceKHR create_vulkan_surface(vk::Instance instance);

    private:

        void bind_event_callbacks();

        GLFWwindow*                                 m_native_window = nullptr;
        GLT::platform::window_attributes            m_data{};
        static bool                                 s_GLFWinitialized;

    };

}

#include "window.inl"

EXPORT_PLUGIN_CLASS(GLT::glfw_window::plugin, GLT::glfw_window::descriptor)
