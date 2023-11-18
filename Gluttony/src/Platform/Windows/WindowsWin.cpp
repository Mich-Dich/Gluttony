#include "glpch.h"

#include "WindowsWin.h"
#include "Core/Events/ApplicationEvent.h"
#include "Core/Events/MouseEvent.h"
#include "Core/Events/KeyEvent.h"

#include "glad/glad.h"

namespace Gluttony {

	static bool s_GLFWinitialized = false;

	static void GLFWErrorCallback(int errorCode, const char* description) { 
		
		GL_LOG_CORE(Error, "[GLFW Error: %d]: %s", errorCode, description);
	}

	void WindowsWin::Init(const WindowAttributes& attributes) {

		m_Data.Title = attributes.Title;
		m_Data.Width = attributes.Width;
		m_Data.Height = attributes.Height;

		GL_LOG_CORE(Trace, "Creating Window [%s width:%d, height: %d]", m_Data.Title, m_Data.Width, m_Data.Height);

		if (!s_GLFWinitialized) {

			GL_ASSERT(glfwInit(), "GLFW initialized", "Could not initialize GLFW");

			glfwSetErrorCallback(GLFWErrorCallback);
			s_GLFWinitialized = true;
		}

		m_Window = glfwCreateWindow((int)attributes.Width, (int)attributes.Height, m_Data.Title.c_str(), nullptr, nullptr);
		glfwMakeContextCurrent(m_Window);
		GL_ASSERT(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress), "", "Failed to Initialize [glad]")
		glfwSetWindowUserPointer(m_Window, &m_Data);
		SetVSync(true);
		
		glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height) {

			WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(window);
			Data.Width = width;
			Data.Height = height;
			WindowResizeEvent event(width, height);
			Data.EventCallback(event);
		});

		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window) {

			WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(window);
			WindowCloseEvent event;
			Data.EventCallback(event);
		});

		glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xOffset, double yOffset) {

			WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(window);
			MouseScrolledEvent event((float)xOffset, (float)yOffset);
			Data.EventCallback(event);
		});

		glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double xPos, double yPos) {

			WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(window);
			MouseMovedEvent event(static_cast<float>(xPos), static_cast<float>(yPos));
			Data.EventCallback(event);
		});
		
		glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods) {

			WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(window);

			switch (action) {

				case GLFW_PRESS: {

					KeyPressedEvent event(key, 0);
					Data.EventCallback(event);
					break;
				}

				case GLFW_RELEASE: {

					KeyReleasedEvent event(key);
					Data.EventCallback(event);
					break;
				}

				case GLFW_REPEAT: {

					KeyPressedEvent event(key, 1);
					Data.EventCallback(event);
					break;
				}
				default:
					break;
			}
		});
		
		glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods) {

			WindowData& Data = *(WindowData*)glfwGetWindowUserPointer(window);

			switch (action) {

				case GLFW_PRESS: {

					MouseButtonPressedEvent event(button);
					Data.EventCallback(event);
					break;
				}

				case GLFW_RELEASE: {

					MouseButtonReleasedEvent event(button);
					Data.EventCallback(event);
					break;
				}

				default:
					break;
			}
		});

	}

	void WindowsWin::Shutdown() {

		GL_LOG_CORE(Trace, "Destroying Window");
		glfwDestroyWindow(m_Window);
	}

	void WindowsWin::OnUpdate() {

		glfwPollEvents();
		glfwSwapBuffers(m_Window);
	}

	void WindowsWin::SetVSync(bool enable) {

		if (enable)	glfwSwapInterval(1);
		else			glfwSwapInterval(0);
		m_Data.VSync = enable;
	}

	Window* Window::Create(const WindowAttributes& attributes) { return new WindowsWin(attributes); }

	WindowsWin::WindowsWin(const WindowAttributes& attributes) { Init(attributes); }

	WindowsWin::~WindowsWin() { Shutdown(); }

	bool WindowsWin::IsVSync() const { return m_Data.VSync; }

}