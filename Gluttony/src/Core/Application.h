#pragma once

#include "core.h"

namespace Gluttony {

	class GLUTTONY_API Application {

	public:
		Application();
		virtual ~Application();

		void Run();
	};

	// to be defined in Client
	Application* CreateApplication();

}
