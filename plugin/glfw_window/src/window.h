
#pragma once

#include <GLFW/glfw3.h>

#include <plugin_system/i_window_plugin.h>
#include <event/event.h>

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::glfw_platform {

	// CONSTANTS =======================================================================================================

	// MACROS ==========================================================================================================

	// TYPES ===========================================================================================================

	// STATIC VARIABLES ================================================================================================

	// FUNCTION DECLARATION ============================================================================================

	// TEMPLATE DECLARATION ============================================================================================

	// CLASS DECLARATION ===============================================================================================

	class window {
	public:

		// Constructs a new window with the specified attributes.
		// Initializes GLFW if needed, creates the window, sets up icons, monitors,
		// and event callbacks.
		// @param attributes Initial window attributes (title, size, vsync, etc.).
		window(GLT::platform::window_attributes attributes = {});

		// Destroys the window, saves its last position, and shuts down GLFW if needed.
		~window();

		// Delete copy constructor to avoid accidental window duplication.
		DELETE_COPY_CONSTRUCTOR(window);

		// Enables or disables VSync.
		// @param vsync True to enable VSync, false to disable.
		GETTER_SETTER_C(bool, vsync, m_data.vsync)

		// Returns the current width of the window.
		// @return Window width in pixels.
		GETTER_C(u32, width, m_data.width)

		// Returns the current height of the window.
		// @return Window height in pixels.
		GETTER_C(u32, height, m_data.height)

		// Returns the current window attributes.
		// @return A copy of the window_attributes struct.
		FORCE_INLINE GLT::platform::window_attributes get_attributes() const { return m_data; }

		// Returns the raw GLFW window pointer.
		// @return A pointer to the GLFWwindow object.
		DEFAULT_GETTER(GLFWwindow*, native_window)

		// Returns the current window size state (windowed, fullscreen, etc.).
		// @return The current window_size_state.
		FORCE_INLINE GLT::platform::window_size_state get_window_size_state() const { return m_data.size_state; }

		// TODO: add comment
		glm::vec2 get_cursor_pos() const;

		// Retrieves the size of the window’s framebuffer in pixels.
		// @param width Output reference for framebuffer width.
		// @param height Output reference for framebuffer height.
		void get_framebuffer_size(int& width, int& height);

		// Shows or hides the window.
		// @param show True to show, false to hide.
		void show_window(bool show);

		// Checks whether the window is currently maximized.
		// @return True if the window is maximized, false otherwise.
		bool is_maximized();

		// Minimizes the window to the taskbar/dock.
		// @return None.
		void minimize_window();

		// Restores the window from minimized or maximized state.
		// @return None.
		void restore_window();

		// Maximizes the window to fill the screen.
		// @return None.
		void maximize_window();

		// Retrieves the size of the monitor the window is displayed on.
		// @param width Output pointer for monitor width.
		// @param height Output pointer for monitor height.
		void get_monitor_size(int* width, int* height);

		// Retrieves the current mouse position relative to the client area.
		// @param pos Output reference storing mouse position as a glm::vec2.
		void get_mouse_position(glm::vec2& pos);

		// Returns the size of the window as a glm::ivec2.
		// @return Window size in pixels.
		glm::ivec2 get_extend();

		// Checks if the window should close.
		// @return True if the user requested the window to close, false otherwise.
		bool should_close();

		// Processes window events by polling GLFW and executing queued custom events.
		// @return None.
		void poll_events();

		// Captures the mouse cursor, locking it to the window.
		// @return None.
		void capture_cursor();

		// Releases the mouse cursor, making it free to move outside the window.
		// @return None.
		void release_cursor();

        
		void imgui_init(GLT::render::backend_api used_render_api);

	private:

		// Binds all GLFW event callbacks to the window.
		// This function sets up handling for resize, focus, close, mouse, and key events.
		// @return None.
		void bind_event_callbacks();

		std::filesystem::path 				m_icon_path;            // Path to the window icon.
		GLT::platform::window_attributes	m_data{};     			// Stores all attributes of the window.
		GLFWwindow* 						m_native_window{};		// Pointer to the GLFW window object.
	};

}
