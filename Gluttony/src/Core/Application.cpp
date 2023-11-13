#include "Application.h"
#include "Core/Events/ApplicationEvent.h"
#include "Core/Logger.h"

namespace Gluttony {

	Application::Application() {

	}

	Application::~Application() {

	}

	void Application::Run() {

		WindowResizeEvent e(1280, 720);
		if (e.IsInCategory(EventCategoryApplication))
			GL_LOG(Trace, e.ToString().c_str());
		

		if (e.IsInCategory(EventCategoryInput))
			GL_LOG_CORE(Trace, e.ToString().c_str());

		while (true);
	}
}
