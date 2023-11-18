#pragma once

#include "core.h"
#include "Events/Event.h"
#include "LayerStack.h"
#include "Core/Events/ApplicationEvent.h"
#include "Window.h"

namespace Gluttony {

	class GLUTTONY_API Application {

	public:
		Application();
		virtual ~Application();

		void Run();
		void OnEvent(Event& event);

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* layer);

		inline static Application& Get() { return *s_Instance; }
		inline Window& GetWindow() const { return *m_Window; }
	private:
		bool OnWindowClose(WindowCloseEvent& event);

		std::unique_ptr<Window> m_Window;
		bool m_Running = true;
		LayerStack m_LayerStack;
		static Application* s_Instance;
	};

	// to be defined in Client
	Application* CreateApplication();

}
