
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
    
    static GLT::plugin_manager::targeted_interface dependencies_interfaces[] = {
        
        GLT::plugin_manager::targeted_interface::none,
    };

    static GLT::plugin_manager::plugin_descriptor descriptor = {
        .name                               = GLT_MODULE_NAME,
        .phase                              = GLT::plugin_manager::load_phase::pre_application,
        .target                             = GLT::plugin_manager::targeted_interface::window,
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

    class plugin : public GLT::platform::i_window_plugin {
    public:

        // Lifecycle
        void on_load() override;
        void on_unload() override;
        void create(const GLT::platform::window_attributes& attributes) override;
        void destroy() override;

        // Queries
        bool should_close() const override;
        glm::ivec2 get_window_size() const override;
        glm::ivec2 get_framebuffer_size() const override;
        glm::ivec2 get_position() const override;
        GLT::platform::window_size_state get_state() const override;
        bool is_vsync() const override;

        // Modifiers
        void show(bool visible) override;
        void set_state(const GLT::platform::window_size_state new_state) override;
        void set_title(const std::string& title) override;
        void set_size(u32 width, u32 height) override;
        void set_vsync(bool vsync) override;
        void set_cursor_mode(GLT::platform::cursor_mode mode) override;

        // Events / backend
        void poll_events() override;
        GLT::platform::backend_api get_backend_api() override;
        const char** get_required_render_extensions(u32* count) override;
        void imgui_init(GLT::render::backend_api used_render_api) override;
        void* get_native_window_handle() override;
        GLT::platform::window_attributes get_window_attributes() override;

        // Vulkan surface creation (renderer plugin will call this)
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
