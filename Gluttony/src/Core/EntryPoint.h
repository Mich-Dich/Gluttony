#pragma once

#ifdef GL_PLATFORM_WINDOWS

extern Gluttony::Application* Gluttony::CreateApplication();

int main(int argc, char** argv) {

	auto app = Gluttony::CreateApplication();
	app->Run();
	delete app;
}

#endif // GL_PLATFORM_WINDOWS
