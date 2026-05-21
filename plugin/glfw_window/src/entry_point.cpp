
#include <plugin_system/i_window_plugin.h>  

#include "window.h"
// #include <util/util.h>

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::glfw_platform {

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

        void on_load() override { LOG_LOADED }
        

        void on_unload() override { LOG_UNLOADED }


        // --- lifecycle ---
        void create(const GLT::platform::window_attributes& attributes) {

            mp_window = std::make_unique<window>(attributes);
        }

        
        void destroy() {
            mp_window.reset();
        }

        // --- queries ---
        bool should_close() const { 
            
            return mp_window ? mp_window->should_close() : true; 
        }


        glm::ivec2 get_window_size() const override {

            return mp_window ? glm::ivec2(mp_window->get_width(), mp_window->get_height()) : glm::ivec2(0);
        }

        
        glm::ivec2 get_framebuffer_size() const override {

            int w, h;
            if (mp_window) 
                mp_window->get_framebuffer_size(w, h);

            else
                w = h = 0;

            return {w, h};
        }


        glm::ivec2 get_position() const override {
            // The window class doesn't have a direct getter, but glfwGetWindowPos can be added,
            // or you can store pos_x/pos_y from the attributes.
            // For now, return the last known position stored in mp_window->m_data.
            if (mp_window) 
                return { mp_window->get_attributes().pos_x, mp_window->get_attributes().pos_y };

            return {0, 0};
        }


        GLT::platform::window_size_state get_state() const override {

            if (mp_window) 
                return static_cast<GLT::platform::window_size_state>(mp_window->get_window_size_state());

            return GLT::platform::window_size_state::windowed;
        }


        bool is_vsync() const override {

            return mp_window ? mp_window->get_vsync() : false;
        }


        // --- modifiers ---
        void show(bool visible) override {

            if (mp_window)
                mp_window->show_window(visible);
        }


        void set_state(const GLT::platform::window_size_state new_state) override {

            if (!mp_window) return;
            switch (new_state) {
                case GLT::platform::window_size_state::minimized:               mp_window->minimize_window(); break;
                case GLT::platform::window_size_state::fullscreen:              [[fallthrough]];
                case GLT::platform::window_size_state::fullscreen_windowed:     mp_window->maximize_window(); break;
                default:                                                        mp_window->restore_window();  break;
            }
        }


        void set_title(const std::string& title) override {

            // The old window class doesn't have set_title – use glfwSetWindowTitle directly.
            // You can add a helper to window.h or call it here via mp_window->native_window().
            if (mp_window)
                glfwSetWindowTitle(mp_window->get_native_window(), title.c_str());
        }


        void set_size(u32 width, u32 height) override {

            if (mp_window)
                glfwSetWindowSize(mp_window->get_native_window(), width, height);
        }


        void set_vsync(bool vsync) override {

            if (mp_window)
                mp_window->set_vsync(vsync);   // already exists in old window
        }


        void set_cursor_mode(GLT::platform::cursor_mode mode) override {

            if (!mp_window) return;
            switch (mode) {
                case GLT::platform::cursor_mode::cursor_normal:          mp_window->release_cursor(); break;
                case GLT::platform::cursor_mode::cursor_disabled:        [[fallthrough]];
                case GLT::platform::cursor_mode::cursor_captured:        mp_window->capture_cursor(); break;
                default: break;
            }
        }


        void poll_events() override { mp_window->poll_events(); }


        GLT::platform::backend_api get_backend_api() { return GLT::platform::backend_api::glfw; }


        const char** get_required_render_extensions(u32* count) override {

            return glfwGetRequiredInstanceExtensions(count);
        }


        void imgui_init(GLT::render::backend_api used_render_api) override {

            mp_window->imgui_init(used_render_api);
        }


        void* get_native_window_handle() override {

            return mp_window ? static_cast<void*>(mp_window->get_native_window()) : nullptr;
        }

        
        GLT::platform::window_attributes get_window_attributes() {

            return mp_window->get_attributes();
        }


    private:

        std::unique_ptr<GLT::glfw_platform::window>         mp_window = nullptr;

    };

}

EXPORT_PLUGIN_CLASS(GLT::glfw_platform::plugin, GLT::glfw_platform::descriptor)
