#include "glpch.h"

#include "Application.h"
#include "Core/Events/ApplicationEvent.h"
#include "Core/Logger.h"

#include <GLFW/glfw3.h>

namespace Gluttony {

#define BIND_EVENT_FN(x)	std::bind(&x, this, std::placeholders::_1)

	Application::Application() {

		m_Window = std::unique_ptr<Window>(Window::Create());
		m_Window->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));
	}

	Application::~Application() {}

	void Application::PushLeayer(Layer* layer) { m_LayerStack.PushLayer(layer); }

	void Application::PushOverlay(Layer* layer) { m_LayerStack.PushOverlay(layer); }

	void Application::OnEvent(Event& event) {

		GL_LOG_CORE(Trace, event.ToChar());
		EventDispacher dispacher(event);
		dispacher.Dispacher<WindowCloseEvent>(BIND_EVENT_FN(Application::OnWindowClose));

		for (auto layer = m_LayerStack.end(); layer != m_LayerStack.begin(); ) {

			(*--layer)->OnEvent(event);
			if (event.Handeled)
				break;
		}
	}

	void Application::Run() {

		while (m_Running) {

			//GL_LOG(Trace, "Engine Tick");

			glClearColor(1, 0, 1, 1);
			glClear(GL_COLOR_BUFFER_BIT);

			for (Layer* layer : m_LayerStack)
				layer->OnUpdate();

			m_Window->OnUpdate();
		}
	}

	bool Application::OnWindowClose(WindowCloseEvent& event) {

		m_Running = false;
		return true;
	}
}
