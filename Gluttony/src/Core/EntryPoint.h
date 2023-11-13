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


	int testInt = 5;


	std::ostringstream Format_Filled;
	Format_Filled << "This is some content.";

	std::string result = "Already Full";

	// Append the content of Format_Filled to result
	result += Format_Filled.str();

	// Now result contains the combined content
	std::cout << "Combined content: " << result << std::endl;


	GL_LOG_CORE(Trace, "Testing Logger int: %d", testInt);
	GL_LOG_CORE(Trace, "SECOND check");

	GL_LOG_CORE(Trace, "TEST Logger int: %d", testInt);
	GL_LOG_CORE(Debug, "TEST Logger int: %d", testInt);
	GL_LOG_CORE(Info, "TEST Logger int: %d", testInt);
	GL_LOG_CORE(Warn, "TEST Logger int: %d", testInt);
	GL_LOG_CORE(Error, "TEST Logger int: %d", testInt);
	GL_LOG_CORE(Fatal, "TEST Logger int: %d", testInt);
	
	/*
	while (true) {

		Sleep(500);
		GL_LOG_CORE(Trace, "TEST Logger int: %d", testInt);
		GL_LOG_CORE(Debug, "TEST Logger int: %d", testInt);
		GL_LOG_CORE(Info, "TEST Logger int: %d", testInt);
		GL_LOG_CORE(Warn, "TEST Logger int: %d", testInt);
		GL_LOG_CORE(Error, "TEST Logger int: %d", testInt);
		GL_LOG_CORE(Fatal, "TEST Logger int: %d", testInt);
	}*/

    












	app->Run();
	delete app;
}

#endif // GL_PLATFORM_WINDOWS
