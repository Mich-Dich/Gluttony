#pragma once

#include <string>
#include <functional>

#include "Core/core.h"

namespace Gluttony {

	// FIXME: Event are currently blocking => Fix that!

	enum class EventType {
		None = 0,
		WindowClose, WindowResize, WindowFocus, WindowLostFocus, WindowMoved,
		AppTick, AppUpdate, AppRender,
		KeyPressed, KeyReleased,
		MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled
	};

	enum EventCategory {
		None = 0,
		EventCategoryApplication	= BIT(0),
		EventCategoryInput			= BIT(1),
		EventCategoryKeyboard		= BIT(2),
		EventCategoryMouse			= BIT(3),
		EventCategoryMouseButton	= BIT(4)
	};

#define EVENT_CLASS_TYPE(type)				static EventType GetStaticType() { return EventType::##type; }					\
											virtual EventType GetEventType() const override { return GetStaticType(); }		\
											virtual const char* GetName() const override { return #type; }

#define	EVENT_CLASS_CATEGORY(category)		virtual int GetCategoryFlag() const override { return category; }

	class GLUTTONY_API Event {

		friend class EventDispacher;

	public:
		virtual EventType GetEventType() const = 0;
		virtual const char* GetName() const = 0;
		virtual int GetCategoryFlag() const = 0;
		virtual std::string ToString() const { return GetName(); }

		inline bool IsInCategory(EventCategory category) { return (GetCategoryFlag() & category); }

	protected:
		bool Handel_m = false;
	};

	class EventDispacher {

		template<typename T>
		using EventFn = std::function<bool(T&)>;

	public:
		EventDispacher(Event& event) : Event_m(event) {}

		template<typename T>
		bool Dispacher(EventFn<T> func) {

			if (Evemt_m.GetEventType() == T::GetStaticType()) {

				Event_m.Handel_m = func(*(T*)&Event_m);
				return true;
			}
			return false;
		}

	private:

		Event& Event_m;
	};

	// inline std::ostream& operator<<(std::ostream & os, const Event & e) { return os << e.ToString(); }
}