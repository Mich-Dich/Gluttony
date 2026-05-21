
#include <util/pch.h>

// #include <stb_image.h>
#include <GLFW/glfw3.h>
#if defined(PLATFORM_WINDOWS)
	#include <Windows.h>
	#define GLFW_EXPOSE_NATIVE_WIN32
	#include <GLFW/glfw3native.h>
#endif

#include <backends/imgui_impl_glfw.h>
#include <event/event_bus.h>
#include <event/application_event.h>
#include <event/input_event.h>

#include "window.h"


// FORWARD DECLARATIONS ================================================================================================


namespace GLT::glfw_platform {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

	static bool s_GLFWinitialized = false;

    // INTERNAL FUNCTION ===============================================================================================

    static FORCE_INLINE void GLFW_error_callback(int errorCode, const char* description) {
		LOG(error, "GLFW Error: [{}] [{}]", errorCode, description); 
	}

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

	window::window(GLT::platform::window_attributes attributes)
        : m_data(attributes) {

		if (!s_GLFWinitialized) {

			glfwSetErrorCallback(GLFW_error_callback);
			ASSERT(glfwInit(), "GLFW initialized", "Could not initialize GLFW");
			s_GLFWinitialized = true;
		}

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);		// No Graphics API for now TODO: update later
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
		glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
		glfwWindowHint(GLFW_MAXIMIZED, (m_data.size_state == GLT::platform::window_size_state::fullscreen
			|| m_data.size_state == GLT::platform::window_size_state::fullscreen_windowed) ? GLFW_TRUE : GLFW_FALSE);

		int max_possible_height = 0;
		int max_possible_width = 0;
		int monitor_count;
		auto monitors = glfwGetMonitors(&monitor_count);

		for (int x = 0; x < monitor_count; x++) {

			int xpos, ypos, width, height;
			glfwGetMonitorWorkarea(monitors[x], &xpos, &ypos, &width, &height);
			max_possible_height = math::max(max_possible_height, height);
			max_possible_width = math::max(max_possible_width, width);

			LOG(trace, "Monitor: [{}] position [{}, {}] size [{}, {}]", x, xpos, ypos, width, height);
		}

		// ensure window is not bigger than possible OR smaller then logical
		LOG(trace, "Creating window");
		m_data.height = math::clamp((int)m_data.height, 200, max_possible_height);
		m_data.width = math::clamp((int)m_data.width, 300, max_possible_width);
		m_data.height -= 35;
		m_data.width -= 8;
		m_native_window = glfwCreateWindow(static_cast<int>(m_data.width), static_cast<int>(m_data.height), m_data.title.c_str(), nullptr, nullptr);

		ASSERT(glfwVulkanSupported(), "GLFW supports Vulkan", "GLFW does not support Vulkan");

		glfwSetWindowPos(m_native_window, m_data.pos_x, m_data.pos_y);
		LOG(trace, "window [{}] pos [{}, {}]", m_data.title, m_data.pos_x, m_data.pos_y);

		glfwSetWindowUserPointer(m_native_window, &m_data);
		set_vsync(m_data.vsync);

		GLFWmonitor* primary = glfwGetPrimaryMonitor();
		const GLFWvidmode* mode = glfwGetVideoMode(primary);
		glfwSetWindowMonitor(m_native_window, NULL, m_data.pos_x, m_data.pos_y, m_data.width, m_data.height, mode->refreshRate);

		if (m_data.size_state == GLT::platform::window_size_state::fullscreen
			|| m_data.size_state == GLT::platform::window_size_state::fullscreen_windowed)
			glfwMaximizeWindow(m_native_window);

		bind_event_callbacks();

		LOG_INIT
	}


	window::~window() {

		const bool is_maximized = this->is_maximized();
		int titlebar_vertical_offset = is_maximized ? 12 : 6;

		int loc_pos_x;
		int loc_pos_y;
		glfwGetWindowPos(m_native_window, &loc_pos_x, &loc_pos_y);
		m_data.pos_x = loc_pos_x + 4;								// window padding
		m_data.pos_y = loc_pos_y + 25 + titlebar_vertical_offset;	// [custom titlebar height offset] + [maximize offset]

		glfwDestroyWindow(m_native_window);
		glfwTerminate();
		LOG_SHUTDOWN
	}

    // CLASS PUBLIC ====================================================================================================

	glm::vec2 window::get_cursor_pos() const { 
		
		f64 pos_x, pos_y;
		glfwGetCursorPos(m_native_window, &pos_x, &pos_y);
		return {pos_x, pos_y};
	}


	void window::get_framebuffer_size(int& width, int& height) { glfwGetFramebufferSize(m_native_window, &width, &height); }


	void window::show_window(bool show) { show ? glfwShowWindow(m_native_window) : glfwHideWindow(m_native_window); }


	bool window::is_maximized() { return static_cast<bool>(glfwGetWindowAttrib(m_native_window, GLFW_MAXIMIZED)); }


	void window::minimize_window() {

		LOG(trace, "minimizing window");
		glfwIconifyWindow(m_native_window);
		m_data.size_state = GLT::platform::window_size_state::minimized;
	}


	void window::restore_window() {

		LOG(trace, "restoring window");
		glfwRestoreWindow(m_native_window);
	}


	void window::maximize_window() {

		LOG(trace, "maximizing window");
		glfwMaximizeWindow(m_native_window);
		m_data.size_state = GLT::platform::window_size_state::fullscreen_windowed;
	}


	void window::get_monitor_size(int* width, int* height) {

		auto monitor = glfwGetWindowMonitor(m_native_window);
		if (!monitor)
			monitor = glfwGetPrimaryMonitor();

		auto video_mode = glfwGetVideoMode(monitor);

		*width = video_mode->width;
		*height = video_mode->height;
	}


	void window::get_mouse_position(glm::vec2& pos) {

		// Get window position and size
		int winX, winY, winWidth, winHeight;
		glfwGetWindowPos(m_native_window, &winX, &winY);
		glfwGetWindowSize(m_native_window, &winWidth, &winHeight);

		double screenX, screenY;
		glfwGetCursorPos(m_native_window, &screenX, &screenY);

		screenX += winX;
		screenY += winY;

		if (screenX >= winX && screenX <= winX + winWidth &&
			screenY >= winY && screenY <= winY + winHeight) {

			pos.x = static_cast<f32>(screenX - winX);
			pos.y = static_cast<f32>(screenY - winY);
		}
	}


	glm::ivec2 window::get_extend() { return { static_cast<u32>(m_data.width), static_cast<u32>(m_data.height) }; }


	bool window::should_close() { return glfwWindowShouldClose(m_native_window); }


    void window::poll_events() { glfwPollEvents(); }


	void window::capture_cursor() { glfwSetInputMode(m_native_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED); }


	void window::release_cursor() { glfwSetInputMode(m_native_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL); }


	void window::imgui_init(GLT::render::backend_api used_render_api) {

		switch (used_render_api) {
			
			case GLT::render::backend_api::vulkan:
				ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow*>(m_native_window), true);
				break;

			case GLT::render::backend_api::direct_x:		LOG(warn, "not implemented yet"); break;
			case GLT::render::backend_api::metal:			LOG(warn, "not implemented yet"); break;
			case GLT::render::backend_api::open_gl:			LOG(warn, "not implemented yet"); break;
		}
	}

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

	void window::bind_event_callbacks() {

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

			mouse_event event(static_cast<GLT::key_code>(button), static_cast<GLT::key_state>(action));
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
