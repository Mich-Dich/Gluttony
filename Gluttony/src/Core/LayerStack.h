#pragma once

#include "Layer.h"
#include "Core/core.h"

namespace Gluttony {

	class GLUTTONY_API LayerStack {

	public:
		LayerStack();
		~LayerStack();

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* overlay);
		void PopLayer(Layer* layer);
		void PopOverlay(Layer* overlay);

		LayerArray::iterator begin() { return m_Layers.begin(); }
		LayerArray::iterator end() { return m_Layers.end(); }

	private:
		LayerArray m_Layers;
		LayerArray::iterator m_LayerInsert;
	};

}