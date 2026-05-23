
#pragma once

#include "layer.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    using layer_iterator = std::vector<unique_ref<layer>>::iterator;

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // layer_stack using manual storage
    class layer_stack {
    public:

        layer_stack();
        ~layer_stack();

        DEFAULT_GETTER(size_t,          layer_count)
        DEFAULT_GETTER(size_t,          overlay_count)

        // Layer management
        template<typename T, typename... Args>
        requires std::derived_from<T, layer>
        weak_ref<T> push_layer(Args&&... args);


        template<typename T, typename... Args>
        requires std::derived_from<T, layer>
        weak_ref<T> push_overlay(Args&&... args);


		// Gets an iterator to the beginning of the layer stack.
		// @return Iterator pointing to the first layer in the stack.
		FORCE_INLINE layer_iterator begin();


		// Gets an iterator to the end of the layer stack.
		// @return Iterator pointing to the position after the last layer in the stack.
		FORCE_INLINE layer_iterator end();


		// Gets an iterator to the beginning of the layer stack.
		// @return Iterator pointing to the first layer in the stack.
		FORCE_INLINE layer_iterator layer_begin();


		// Gets an iterator to the end of the layer stack.
		// @return Iterator pointing to the position after the last layer in the stack.
		FORCE_INLINE layer_iterator layer_end();


		// Gets an iterator to the beginning of the layer stack.
		// @return Iterator pointing to the first layer in the stack.
		FORCE_INLINE layer_iterator overlay_begin();


		// Gets an iterator to the end of the layer stack.
		// @return Iterator pointing to the position after the last layer in the stack.
		FORCE_INLINE layer_iterator overlay_end();


        void pop_layer();


        void pop_overlay();


        void clear();


        bool is_empty() const;

    private:

        std::vector<unique_ref<layer>>          m_layers;
        size_t                                  m_layer_count = 0;
        size_t                                  m_overlay_count = 0;
    };

}

#include "layer_stack.inl"
