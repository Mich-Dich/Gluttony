#pragma once

#include "glpch.h"

#ifdef GL_PLATFORM_WINDOWS


extern Gluttony::Application* Gluttony::CreateApplication();

int main(int argc, char** argv) {

	Gluttony::Log_Init("logFileCORE.txt", "logFile.txt", "[$B$T:$J$E] [$B$L$X $A - $F:$G$E] $C");
	auto app = Gluttony::CreateApplication();

	app->Run();
	delete app;
}

#endif // GL_PLATFORM_WINDOWS
