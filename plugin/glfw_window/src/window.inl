#pragma once

#include <glm/glm.hpp>
#include <backends/imgui_impl_glfw.h>
#include <vulkan/vulkan.hpp>

#include <util/pch.h>
#include <event/event_bus.h>
#include <event/application_event.h>
#include <event/input_event.h>

// FORWARD DECLARATIONS ================================================================================================

namespace GLT::glfw_window {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    bool                plugin::s_GLFWinitialized = false;

    // INTERNAL FUNCTION DECLARATION ===================================================================================
    
    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static void glfw_error_callback(int errorCode, const char* description) {

        LOG(error, "GLFW Error: [{}] [{}]", errorCode, description);
    }


    static void get_max_workarea_size(int& max_width, int& max_height) {

        int monitor_count;
        auto monitors = glfwGetMonitors(&monitor_count);
        max_width = max_height = 0;
        for (int i = 0; i < monitor_count; ++i) {

            int x, y, w, h;
            glfwGetMonitorWorkarea(monitors[i], &x, &y, &w, &h);
            if (w > max_width) max_width = w;
            if (h > max_height) max_height = h;
        }
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================
    
    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    void plugin::on_load() {

        LOG_LOADED
    }


    void plugin::on_unload() {

        LOG_UNLOADED
    }


    void plugin::create(const GLT::platform::window_attributes& attrs) {

        m_data = attrs;

        if (!s_GLFWinitialized) {

            glfwSetErrorCallback(glfw_error_callback);
            ASSERT(glfwInit(), "GLFW initialized", "Could not initialize GLFW");
            s_GLFWinitialized = true;
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
        glfwWindowHint(GLFW_MAXIMIZED, 
            (m_data.size_state == GLT::platform::window_size_state::fullscreen ||
            m_data.size_state == GLT::platform::window_size_state::fullscreen_windowed) ? GLFW_TRUE : GLFW_FALSE);

        int max_width = 0, max_height = 0;
        get_max_workarea_size(max_width, max_height);

        m_data.width = glm::clamp(m_data.width, 300u, static_cast<u32>(max_width));
        m_data.height = glm::clamp(m_data.height, 200u, static_cast<u32>(max_height));

        // Adjust for window decorations (approximate)
        m_data.width -= 8;
        m_data.height -= 35;

        m_native_window = glfwCreateWindow(static_cast<int>(m_data.width), static_cast<int>(m_data.height), 
            m_data.title.c_str(), nullptr, nullptr);
        ASSERT(m_native_window, "Window creation", "Failed to create GLFW window");

        glfwSetWindowPos(m_native_window, static_cast<int>(m_data.pos_x), static_cast<int>(m_data.pos_y));

        set_vsync(m_data.vsync);

        // Restore monitor refresh rate
        GLFWmonitor* primary = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(primary);
        glfwSetWindowMonitor(m_native_window, nullptr,
            static_cast<int>(m_data.pos_x), static_cast<int>(m_data.pos_y),
            static_cast<int>(m_data.width), static_cast<int>(m_data.height),
            mode->refreshRate);

        if (m_data.size_state == GLT::platform::window_size_state::fullscreen ||
            m_data.size_state == GLT::platform::window_size_state::fullscreen_windowed) {

            glfwMaximizeWindow(m_native_window);
        }

        glfwSetWindowUserPointer(m_native_window, &m_data);
        bind_event_callbacks();
        LOG_INIT
    }


    void plugin::destroy() {

        if (!m_native_window)
            return;

        // Save window position before destroying
        int pos_x, pos_y;
        glfwGetWindowPos(m_native_window, &pos_x, &pos_y);
        m_data.pos_x = static_cast<u32>(pos_x);
        m_data.pos_y = static_cast<u32>(pos_y);

        glfwDestroyWindow(m_native_window);
        m_native_window = nullptr;

        // Note: glfwTerminate() is called only when the last window is destroyed.
        // We rely on GLFW's internal ref counting; or we can call terminate if this is the last window.
        // For simplicity, we don't terminate here – the application may create another window later.
        LOG_SHUTDOWN
    }

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    bool plugin::should_close() const {

        return m_native_window ? glfwWindowShouldClose(m_native_window) : true;
    }


    glm::ivec2 plugin::get_window_size() const {

        if (!m_native_window) return {0, 0};
        
        int w, h;
        glfwGetWindowSize(m_native_window, &w, &h);
        return {w, h};
    }


    glm::ivec2 plugin::get_framebuffer_size() const {

        if (!m_native_window) return {0, 0};
        
        int w, h;
        glfwGetFramebufferSize(m_native_window, &w, &h);
        return {w, h};
    }


    glm::ivec2 plugin::get_position() const {

        if (!m_native_window) return {0, 0};

        int x, y;
        glfwGetWindowPos(m_native_window, &x, &y);
        return {x, y};
    }


    GLT::platform::window_size_state plugin::get_state() const {

        if (!m_native_window) return GLT::platform::window_size_state::windowed;

        if (glfwGetWindowAttrib(m_native_window, GLFW_ICONIFIED))
            return GLT::platform::window_size_state::minimized;

        if (glfwGetWindowAttrib(m_native_window, GLFW_MAXIMIZED))
            return GLT::platform::window_size_state::fullscreen_windowed;

        return GLT::platform::window_size_state::windowed;
    }


    bool plugin::get_vsync() const {

        return m_data.vsync;
    }


    void plugin::show(bool visible) {

        if (m_native_window)
            visible ? glfwShowWindow(m_native_window) : glfwHideWindow(m_native_window);
    }


    void plugin::set_state(const GLT::platform::window_size_state new_state) {

        if (!m_native_window) return;
        switch (new_state) {

            case GLT::platform::window_size_state::minimized:
                glfwIconifyWindow(m_native_window);
                break;
            case GLT::platform::window_size_state::fullscreen:
            case GLT::platform::window_size_state::fullscreen_windowed:
                glfwMaximizeWindow(m_native_window);
                break;
            default:
                glfwRestoreWindow(m_native_window);
                break;
        }
        m_data.size_state = new_state;
    }


    void plugin::set_title(const std::string& title) {

        if (!m_native_window) return;

        glfwSetWindowTitle(m_native_window, title.c_str());
        m_data.title = title;
    }


    void plugin::set_size(u32 width, u32 height) {

        if (!m_native_window) return;
        glfwSetWindowSize(m_native_window, static_cast<int>(width), static_cast<int>(height));
        m_data.width = width;
        m_data.height = height;
    }

    
    IGNORE_UNUSED_PARAMETER_START
    void plugin::set_vsync(bool vsync) {

        // m_data.vsync = vsync;
        // if (!m_native_window) 
        //     return;
        // glfwSwapInterval(vsync ? 1 : 0);
    }
    IGNORE_UNUSED_PARAMETER_STOP


    void plugin::set_cursor_mode(GLT::platform::cursor_mode mode) {

        if (!m_native_window) return;
        
        switch (mode) {
            case GLT::platform::cursor_mode::cursor_normal:     
                glfwSetInputMode(m_native_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                break;
            case GLT::platform::cursor_mode::cursor_hidden:
                glfwSetInputMode(m_native_window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
                break;
            case GLT::platform::cursor_mode::cursor_disabled:
            case GLT::platform::cursor_mode::cursor_captured:
                glfwSetInputMode(m_native_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                break;
        }
    }


    void plugin::poll_events() { glfwPollEvents(); }


    GLT::platform::backend_api plugin::get_backend_api() { return GLT::platform::backend_api::glfw; }


    const char** plugin::get_required_render_extensions(u32* count) { return glfwGetRequiredInstanceExtensions(count); }


    void plugin::imgui_init(GLT::render::backend_api used_render_api) {

        if (!m_native_window) return;

        switch (used_render_api) {

            case GLT::render::backend_api::vulkan:      ImGui_ImplGlfw_InitForVulkan(m_native_window, true); break;
            case GLT::render::backend_api::direct_x:    [[fallthrough]];
            case GLT::render::backend_api::metal:       [[fallthrough]];
            case GLT::render::backend_api::open_gl:
                LOG(warn, "ImGui GLFW initialization for this render API not implemented");
                break;
        }
    }


    void plugin::imgui_shutdown() {

        ImGui_ImplGlfw_Shutdown();
    }


    void plugin::begin_imgui_frame() {

        ImGui_ImplGlfw_NewFrame();   
    }


    void* plugin::get_native_window_handle() { return static_cast<void*>(m_native_window); }


    GLT::platform::window_attributes plugin::get_window_attributes() { return m_data; }


    vk::SurfaceKHR plugin::create_vulkan_surface(vk::Instance instance) {

        ASSERT(glfwVulkanSupported() == GLFW_TRUE, "", "Vulkan not supported by GLFW");
        VkSurfaceKHR raw_surface;
        VkResult result = glfwCreateWindowSurface(static_cast<VkInstance>(instance), m_native_window, nullptr, &raw_surface);
        ASSERT(result == VK_SUCCESS, "", "Failed to create Vulkan surface");
        return vk::SurfaceKHR(raw_surface);
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

	void plugin::bind_event_callbacks() {

		IGNORE_UNUSED_PARAMETER_START

		glfwSetWindowRefreshCallback(m_native_window, [](GLFWwindow* window) {

			window_refresh_event event;
			GLT::event_bus::post(event);
		});


		glfwSetWindowSizeCallback(m_native_window, [](GLFWwindow* window, int width, int height) {

			GLT::platform::window_attributes& data = *(GLT::platform::window_attributes*)glfwGetWindowUserPointer(window);
			if (data.size_state == GLT::platform::window_size_state::windowed) {

				data.width = static_cast<u32>(width);
				data.height = static_cast<u32>(height);
			}
			window_resize_event event(static_cast<u32>(width), static_cast<u32>(height));
			GLT::event_bus::post(event);
		});


		glfwSetFramebufferSizeCallback(m_native_window, [](GLFWwindow* window, int width, int height) {

			window_framebuffer_resize_event event(static_cast<u32>(width), static_cast<u32>(height));
			GLT::event_bus::post(event);
		});


		glfwSetWindowFocusCallback(m_native_window, [](GLFWwindow* window, int focused) {

			window_focus_event event(focused == GLFW_TRUE);
			GLT::event_bus::post(event);
		});


		glfwSetWindowCloseCallback(m_native_window, [](GLFWwindow* window) {

			window_close_event event;
			GLT::event_bus::post(event);
		});


		glfwSetWindowPosCallback(m_native_window, [](GLFWwindow* window, int x, int y) {

			window_move_event event(x, y);
			GLT::event_bus::post(event);
		});


		glfwSetWindowIconifyCallback(m_native_window, [](GLFWwindow* window, int iconified) {

			GLT::platform::window_attributes& data = *(GLT::platform::window_attributes*)glfwGetWindowUserPointer(window);
			window_iconify_event event(iconified == GLFW_TRUE);
			GLT::event_bus::post(event);
			data.size_state = iconified ? GLT::platform::window_size_state::minimized : GLT::platform::window_size_state::windowed;
		});


		glfwSetWindowMaximizeCallback(m_native_window, [](GLFWwindow* window, int maximized) {

			GLT::platform::window_attributes& data = *(GLT::platform::window_attributes*)glfwGetWindowUserPointer(window);
			window_maximize_event event(maximized == GLFW_TRUE);
			GLT::event_bus::post(event);
			data.size_state = maximized ? GLT::platform::window_size_state::fullscreen_windowed : GLT::platform::window_size_state::windowed;
		});


		glfwSetWindowContentScaleCallback(m_native_window, [](GLFWwindow* window, float xscale, float yscale) {

			window_content_scale_event event(xscale, yscale);
			GLT::event_bus::post(event);
		});


		glfwSetDropCallback(m_native_window, [](GLFWwindow* window, int count, const char** paths) {

			std::vector<std::string> path_list;
			for (int i = 0; i < count; ++i)
				path_list.emplace_back(paths[i]);
			
			window_drop_event event(path_list);
			GLT::event_bus::post(event);
		});


		glfwSetKeyCallback(m_native_window, [](GLFWwindow* window, int key, int scancode, int action, int mods) {

			GLT::key_event event(static_cast<GLT::key_code>(key), static_cast<GLT::key_state>(action), mods);
			GLT::event_bus::post(event);
		});


		glfwSetCharCallback(m_native_window, [](GLFWwindow* window, unsigned int codepoint) {

			char_event event(codepoint);
			GLT::event_bus::post(event);
		});


		glfwSetCharModsCallback(m_native_window, [](GLFWwindow* window, unsigned int codepoint, int mods) {

			char_event event(codepoint, mods);
			GLT::event_bus::post(event);
		});


		glfwSetMouseButtonCallback(m_native_window, [](GLFWwindow* window, int button, int action, int mods) {

			GLT::key_event event(static_cast<GLT::key_code>(button), static_cast<GLT::key_state>(action), mods);
			GLT::event_bus::post(event);
		});


		glfwSetCursorPosCallback(m_native_window, [](GLFWwindow* window, double x_pos, double y_pos) {

			mouse_event event(mouse_event::action_type::move, glm::vec2(static_cast<f32>(x_pos), static_cast<f32>(y_pos)));
			GLT::event_bus::post(event);
		});


		glfwSetCursorEnterCallback(m_native_window, [](GLFWwindow* window, int entered) {

			mouse_event event(entered == GLFW_TRUE);
			GLT::event_bus::post(event);
		});


		glfwSetScrollCallback(m_native_window, [](GLFWwindow* window, double x_offset, double y_offset) {

			mouse_event event(mouse_event::action_type::scroll, glm::vec2(static_cast<f32>(x_offset), static_cast<f32>(y_offset)));
			GLT::event_bus::post(event);
		});

		IGNORE_UNUSED_PARAMETER_STOP
	}

}
