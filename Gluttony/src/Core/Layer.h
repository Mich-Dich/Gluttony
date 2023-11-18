#pragma once

#include "Core/core.h"
#include "Core/Events/Event.h"

namespace Gluttony {


	class GLUTTONY_API Layer {

	public:
		Layer(const std::string& name = "Layer");
		virtual ~Layer();

		virtual void OnAttach() {}
		virtual void OnDetach() {}
		virtual void OnUpdate() {}
		virtual void OnEvent(Event& event) {}

		inline const std::string& GetName() const { return m_Name; }

	private:
		std::string m_Name;
	};

	typedef std::vector<Layer*> LayerArray;
}
