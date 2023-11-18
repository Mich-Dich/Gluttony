#include "Gluttony.h"

class ExampleLayer : public Gluttony::Layer {

public:
	ExampleLayer() 
		:Layer("ExampleLayer") {}

	void OnUpdate() {

		//GL_LOG(Trace, "ExampleLayer::OnUpdate");
	}

	void OnEvent(Gluttony::Event& event) {

		//GL_LOG(Trace, event.ToString().c_str());
	}
};

class Sandbox : public Gluttony::Application {

public:

	Sandbox() {

		PushLayer(new ExampleLayer());
		PushOverlay(new Gluttony::ImGuiLayer());
	}
	~Sandbox() {}
};

Gluttony::Application* Gluttony::CreateApplication() {

	GL_LOG(Trace, "CreateApplication()");
	return new Sandbox();
}