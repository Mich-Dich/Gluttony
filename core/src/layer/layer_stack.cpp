
#include "util/pch.h"
#include "layer_stack.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    layer_stack::layer_stack() {

    }


    layer_stack::~layer_stack() {

        clear();
    }

    // CLASS PUBLIC ====================================================================================================

    void layer_stack::pop_layer() {

        if (m_layer_count > 0) {
            auto it = m_layers.begin() + (m_layer_count - 1);           // Get the last layer
            (*it)->on_detach();                                         // Call on_detach for the layer
            m_layers.erase(it);
            m_layer_count--;
        }
    }


    void layer_stack::pop_overlay() {

        if (m_overlay_count > 0) {
            auto it = m_layers.end() - 1;                               // Get the last overlay (end of vector)
            (*it)->on_detach();                                         // Call on_detach for the overlay
            m_layers.erase(it);
            m_overlay_count--;
        }
    }


    void layer_stack::clear() {

        for (auto& layer : m_layers) {                                  // Call on_detach for all layers and overlays
            layer->on_detach();
        }

        m_layers.clear();
        m_layer_count = 0;
        m_overlay_count = 0;
    }


    bool layer_stack::is_empty() const {
        
        return m_layer_count == 0 && m_overlay_count == 0; 
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
