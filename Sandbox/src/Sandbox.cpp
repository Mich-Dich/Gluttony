#include "Gluttony.h"

class Sandbox : public Gluttony::Application {

public:

	Sandbox() {}
	~Sandbox() {}
};

Gluttony::Application* Gluttony::CreateApplication() {

	return new Sandbox();
}