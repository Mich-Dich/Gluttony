#include "Gluttony.h"

class Sandbox : public Gluttony::Application {

public:

	Sandbox() {}
	~Sandbox() {}
};

Gluttony::Application* Gluttony::CreateApplication() {

	//GL_LOG(Trace, "CreateApplication()");
	//GL_LOG_CORE(Trace, "CreateApplication()");

	return new Sandbox();
}