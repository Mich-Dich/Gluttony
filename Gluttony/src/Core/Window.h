#pragma once

#include "glpch.h"

#include "Core/core.h"
#include "Events/Event.h"

namespace Gluttony {

	struct WindowAttributes {
		
		std::string Title;
		unsigned int Width;
		unsigned int Height;

		WindowAttributes(const std::string title = "Gluttony", unsigned int width = 1280, unsigned int height = 720)
			: Title(title), Width(width), Height(height) {}
	};

	class GLUTTONY_API Window {

	public:
		using EventCallbackFn = std::function<void(Event&)>;

		virtual ~Window() {};
		virtual void OnUpdate() = 0;
		virtual unsigned int GetWidth() const = 0;
		virtual unsigned int GetHeight() const = 0;

		virtual void SetEventCallback(const EventCallbackFn& callback) = 0;
		virtual void SetVSync(bool enable) = 0;
		virtual bool IsVSync() const = 0;

		static Window* Create(const WindowAttributes& attributes = WindowAttributes());
	};

}