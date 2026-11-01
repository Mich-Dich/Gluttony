
#include "util/pch.h"
#include "app.h"
#include "../../core/src/config/imgui_config.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::launcher {

    // CONSTANTS =======================================================================================================

    constexpr char                  WINDOW_TITLE[] = "Gluttony Launcher";

    constexpr char                  GLSL_VERSION[] = "#version 130";   // GL 3.0 — safe default on Linux/Windows/macOS

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    app*                            app::s_instance = nullptr;

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // Try a few common locations for the asset folder so the launcher works both from a deployed layout (asset/ next to
    // the executable) and from a dev build (exe in build/<cfg>/..., asset/ at the repo root)
    std::filesystem::path find_asset_dir();

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    std::filesystem::path find_asset_dir() {

        const auto exe_dir = GLT::util::get_executable_path();
        const std::filesystem::path candidates[] = {
            exe_dir / "asset",
            exe_dir / ".." / "asset",
            exe_dir / ".." / ".." / "asset",
            exe_dir / ".." / ".." / ".." / "asset",
        };
        for (const auto& c : candidates) {
            std::error_code error{};
            if (std::filesystem::exists(c / "image", error) && !error)
                return c;
        }
        return exe_dir / "asset";   // fall back; ui::init will just skip missing files
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    app::app() {

        ASSERT(!s_instance, "", "Application already exists");
        s_instance = this;

        const u64 time = (1 / 60) * 1000000;
        m_fps_controller.set_target_interval_duration(std::chrono::microseconds(time));

        plugin_manager::enter_phase(plugin_manager::phase::pre_application);

        imgui_config::init(/*enable_multi_viewport*/false);
        init_window();
        m_ui.init(find_asset_dir());

        plugin_manager::enter_phase(plugin_manager::phase::application_ready);

        LOG_INIT
    }


    app::~app() {

        plugin_manager::enter_phase(plugin_manager::phase::pre_application_shutdown);

        shutdown_window();
        imgui_config::shutdown();

        plugin_manager::enter_phase(plugin_manager::phase::post_application_shutdown);
        s_instance = nullptr;
        LOG_SHUTDOWN
    }

    // CLASS PUBLIC ====================================================================================================

    void app::run() {

        auto* p_window = GLT::platform::window::get_ptr();
        ASSERT(p_window, "", "Failed to load window plugin")
        auto* native = static_cast<GLFWwindow*>(p_window->get_native_window_handle(m_main_window));

        // Pin the context we initialized OpenGL3 against
        ImGuiContext* const imgui_ctx = ImGui::GetCurrentContext();
        ASSERT(imgui_ctx, "", "No ImGui context current")

        auto close_sub = event_bus::subscribe_scoped<GLT::window_close_event>([this](const GLT::window_close_event& ev) {
            if (ev.get_window() == m_main_window) m_running = false;
        });

        while (m_running) {

            p_window->poll_events();
            if (p_window->get_state(m_main_window) == GLT::platform::window_size_state::minimized)
                continue;

            p_window->begin_imgui_frame(m_main_window);
            ImGui::SetCurrentContext(imgui_ctx);
            ImGui_ImplOpenGL3_NewFrame();
            ImGui::NewFrame();

            m_ui.draw(m_running);

            ImGui::Render();

            const glm::ivec2 fb = p_window->get_framebuffer_size(m_main_window);
            glViewport(0, 0, fb.x, fb.y);
            glClearColor(0.09f, 0.09f, 0.09f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(native);
        }
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void app::init_window() {

        auto* p_window = GLT::platform::window::get_ptr();
        ASSERT(p_window, "", "Failed to load window plugin")
        ASSERT(p_window->get_backend_api() == GLT::platform::backend_api::glfw, "", "Launcher only supports GLFW windows")

        platform::window_attributes attributes;
        platform::serialize_window_attributes(GLT::util::get_executable_path(), attributes, serializer::option::load);

        // Launcher renders via OpenGL — tell the plugin up front so it creates a GL-capable window
        m_main_window = p_window->create(attributes, GLT::render::backend_api::open_gl);
        ASSERT(m_main_window.is_valid(), "", "Failed to create main window")
        ASSERT(p_window->create_render_context(m_main_window, GLT::render::backend_api::open_gl), "", "Failed to activate render context")
        ASSERT(ImGui::GetCurrentContext(), "", "imgui_config::init() did not leave a context current")

        LOG(debug, "launcher: ctx={} version={}",
            static_cast<void*>(ImGui::GetCurrentContext()), ImGui::GetVersion())

        p_window->imgui_init(m_main_window, GLT::render::backend_api::open_gl);
        ImGui_ImplOpenGL3_Init(GLSL_VERSION);
    }


    void app::shutdown_window() {

        auto* p_window = GLT::platform::window::get_ptr();
        ASSERT(p_window, "", "Failed to load window plugin")

        auto attributes = p_window->get_window_attributes(m_main_window);
        platform::serialize_window_attributes(GLT::util::get_executable_path(), attributes, serializer::option::save);

        p_window->imgui_shutdown(m_main_window);
        ImGui_ImplOpenGL3_Shutdown();
        p_window->destroy(m_main_window);
    }

}
