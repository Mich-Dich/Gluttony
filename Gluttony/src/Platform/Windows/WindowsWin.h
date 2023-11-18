#pragma once

#include "Core/Window.h"

#include <GLFW/glfw3.h>

namespace Gluttony {

	class WindowsWin : public Window {

	public:
		WindowsWin(const WindowAttributes& attributes);
		virtual ~WindowsWin();

		void OnUpdate() override;

		inline unsigned int GetWidth() const override { return m_Data.Width; }
		inline unsigned int GetHeight() const override { return m_Data.Height; }

		inline void SetEventCallback(const EventCallbackFn& callback) override { m_Data.EventCallback = callback; }
		void SetVSync(bool enable) override;
		bool IsVSync() const override;

	private:
		virtual void Init(const WindowAttributes& attributes);
		virtual void Shutdown();
		
		struct WindowData {

			std::string Title;
			unsigned int Width;
			unsigned int Height;
			bool VSync;
			EventCallbackFn EventCallback;
		};

		GLFWwindow* m_Window;
		WindowData m_Data;
	};

}