#pragma once

#ifdef GL_PLATFORM_WINDOWS

#include <stdio.h>

#include <algorithm>
#include <iostream>
#include <list>
#include <map>
#include <string>
#include <type_traits>
#include <utility>
#include <windows.h>


extern Gluttony::Application* Gluttony::CreateApplication();

int main(int argc, char** argv) {

	auto app = Gluttony::CreateApplication();
	Gluttony::Log_Init("logFileCORE.txt", "logFile.txt", "[$B$T:$J$E] [$B$L$X - $A - $F:$G$E] $C");

	int testInt = 5;
	float TestFloat = 3.1415;

	GL_ASSERT(10 < 42, "", "FAILURE");
	//GL_VALIDATE(10 > 42, "SUCCESS", "FAILURE", 0);

	GL_LOG(Trace, "Testing Logger int: %d", testInt);
	GL_LOG(Trace, "SECOND check");

	GL_LOG_CORE(Trace, "TEST Logger int: %d", testInt);
	GL_LOG_CORE(Debug, "TEST Logger int: %f", TestFloat);
	GL_LOG_CORE(Info, "TEST Logger int: %d", testInt);
	GL_LOG_CORE(Warn, "TEST Logger int: %f", TestFloat);
	GL_LOG_CORE(Error, "TEST Logger int: %d", testInt);
	GL_LOG_CORE(Fatal, "TEST Logger int: %f", TestFloat);

	app->Run();
	delete app;
}

#endif // GL_PLATFORM_WINDOWS
