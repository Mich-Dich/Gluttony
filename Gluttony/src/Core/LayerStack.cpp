#include "glpch.h"
#include "LayerStack.h"

namespace Gluttony {

	LayerStack::LayerStack() {

		m_LayerInsert = m_Layers.begin();
	}

	// Delete all Layers
	LayerStack::~LayerStack() {

		for (Layer* layer : m_Layers)
			delete layer;
	}

	// Emplaye a Layer* to start of LayerStack
	void LayerStack::PushLayer(Layer* layer) {

		m_LayerInsert = m_Layers.emplace(m_LayerInsert, layer);
	}

	// Emplace a Layer* to end of LayerStack
	void LayerStack::PushOverlay(Layer* overlay) {

		m_Layers.emplace_back(overlay);
	}

	// Pop a Layer from stack if found
	void LayerStack::PopLayer(Layer* layer) {

		auto target = std::find(m_Layers.begin(), m_Layers.end(), layer);
		if (target != m_Layers.end()) {

			m_Layers.erase(target);
			m_LayerInsert--;
		}
	}

	// 
	void LayerStack::PopOverlay(Layer* overlay) {

		auto target = std::find(m_Layers.begin(), m_Layers.end(), overlay);
		if (target != m_Layers.end())
			m_Layers.erase(target);
	}

}