#pragma once


// FORWARD DECLARATIONS ================================================================================================

namespace GLT {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // Push a new layer
    template<typename T, typename... Args>
    requires std::derived_from<T, layer>
    weak_ref<T> layer_stack::push_layer(Args&&... args) {

        auto new_layer = create_unique_ref<T>(std::forward<Args>(args)...);          // Create the layer
        new_layer->set_is_overlay(false);
        new_layer->on_attach();
        weak_ref<T> weak_layer = new_layer;                                   // Get weak reference before moving ownership

        auto insert_pos = m_layers.begin() + m_layer_count;                         // Insert at the position after existing layers (before overlays)
        m_layers.insert(insert_pos, std::move(new_layer));

        m_layer_count++;
        // weak_layer.lock()->on_attach();
        return weak_layer;
    }


    template<typename T, typename... Args>
    requires std::derived_from<T, layer>
    weak_ref<T> layer_stack::push_overlay(Args&&... args) {

        auto new_overlay = std::make_unique<T>(std::forward<Args>(args)...);        // Create the overlay
        new_overlay->set_is_overlay(true);

        weak_ref<T> weak_overlay = new_overlay.get();                               // Get weak reference before moving ownership
        m_layers.push_back(std::move(new_overlay));                                 // Always push overlay to the end of the vector

        m_overlay_count++;
        weak_overlay->on_attach();
        return weak_overlay;
    }


    FORCE_INLINE layer_iterator layer_stack::begin()             { return m_layers.begin(); }


    FORCE_INLINE layer_iterator layer_stack::end()               { return m_layers.end(); }


    FORCE_INLINE layer_iterator layer_stack::layer_begin()       { return m_layers.begin(); }


    FORCE_INLINE layer_iterator layer_stack::layer_end()         { return m_layers.end() - m_overlay_count; }


    FORCE_INLINE layer_iterator layer_stack::overlay_begin()     { return m_layers.begin() + m_layer_count; }


    FORCE_INLINE layer_iterator layer_stack::overlay_end()       { return m_layers.end(); }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
