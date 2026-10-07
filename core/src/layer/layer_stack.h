
#pragma once

#include "layer.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // @brief Iterator type over the stack's underlying storage
    //
    // Overlays occupy the tail of the vector, so [layer_begin()..layer_end()] and [overlay_begin()..overlay_end()] split
    // the same iterator range
    using layer_iterator = std::vector<unique_ref<layer>>::iterator;

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Ordered container of layers and overlays
    //
    // Storage is a single vector: layers in insertion order, then overlays in insertion order. Two counters ([m_layer_count],
    // [m_overlay_count]) split the vector into the layer range and the overlay range without needing separate containers
    //
    // Layers are identified by [weak_ref<T>]; the stack owns the actual unique_ref's, so a weak reference becomes invalid
    // as soon as the layer is popped or the stack is destroyed
    class layer_stack {
    public:

        layer_stack();
        ~layer_stack();

        DEFAULT_GETTER(size_t,          layer_count)
        DEFAULT_GETTER(size_t,          overlay_count)


        // @brief Constructs a new layer of type [T] and pushes it below every overlay
        //
        // The new layer is inserted between the last existing layer and the first overlay, so overlays always remain on top
        //
        // @tparam T     Concrete layer type; must derive from [layer]
        // @tparam Args  Constructor argument types for [T]
        // @param args   Arguments forwarded to [T]'s constructor
        // @return Weak reference to the newly-pushed layer
        template<typename T, typename... Args>
        requires std::derived_from<T, layer>
        weak_ref<T> push_layer(Args&&... args);


        // @brief Constructs a new overlay of type [T] and pushes it at the end of the stack
        //
        // Overlays always render above every regular layer, so a new overlay is appended after any existing overlays
        //
        // @tparam T     Concrete layer type; must derive from [layer]
        // @tparam Args  Constructor argument types for [T]
        // @param args   Arguments forwarded to [T]'s constructor
        // @return Weak reference to the newly-pushed overlay
        template<typename T, typename... Args>
        requires std::derived_from<T, layer>
        weak_ref<T> push_overlay(Args&&... args);


        // @brief Finds the first layer or overlay of type [T]
        // @tparam T  Concrete layer type to search for
        // @return Pointer to the first match, or nullptr if none exists
        template<typename T>
        requires std::derived_from<T, layer>
        [[nodiscard]] T* get();


        // @brief Const overload of [get<T>()]
        // @tparam T  Concrete layer type to search for
        // @return Const pointer to the first match, or nullptr if none exists
        template<typename T>
        requires std::derived_from<T, layer>

        [[nodiscard]] const T* get() const;

        // @brief Reports whether any layer or overlay of type [T] is present
        // @tparam T  Concrete layer type to search for
        // @return true if a matching layer exists
        template<typename T>
        requires std::derived_from<T, layer>
        [[nodiscard]] bool has() const;


		// Gets an iterator to the beginning of the layer stack
		// @return Iterator pointing to the first layer in the stack
		FORCE_INLINE layer_iterator begin();


		// Gets an iterator to the end of the layer stack
		// @return Iterator pointing to the position after the last layer in the stack
		FORCE_INLINE layer_iterator end();


		// Gets an iterator to the beginning of the layer stack
		// @return Iterator pointing to the first layer in the stack
		FORCE_INLINE layer_iterator layer_begin();


		// Gets an iterator to the end of the layer stack
		// @return Iterator pointing to the position after the last layer in the stack
		FORCE_INLINE layer_iterator layer_end();


		// Gets an iterator to the beginning of the layer stack
		// @return Iterator pointing to the first layer in the stack
		FORCE_INLINE layer_iterator overlay_begin();


		// Gets an iterator to the end of the layer stack
		// @return Iterator pointing to the position after the last layer in the stack
		FORCE_INLINE layer_iterator overlay_end();


        // @brief Pops the top-most overlay
        //
        // No-op if no overlay is present. Calls [on_detach()] on the popped layer before erasing it
        void pop_layer();


        // @brief Pops a specific layer identified by a weak reference
        //
        // Only searches the regular-layer range (overlays are not eligible) The weak reference is locked; if it no
        // longer points at a live layer, this is a no-op
        //
        // @param layer_ref  Weak reference previously returned by [push_layer()]
        void pop_layer(const weak_ref<layer>& layer_ref);


        // @brief Pops the top-most overlay
        //
        // No-op if no overlay is present. Calls [on_detach()] on the popped overlay before erasing it
        void pop_overlay();


        // @brief Detaches and removes every layer and overlay
        //
        // Calls {on_detach()} on each entry in reverse-stack order (top-most first) before clearing the storage
        // After this call, [is_empty()] returns true
        void clear();


        // @brief Reports whether the stack holds any layers or overlays @return true if the stack contains no entries
        bool is_empty() const;

    private:

        std::vector<unique_ref<layer>>          m_layers;
        size_t                                  m_layer_count = 0;
        size_t                                  m_overlay_count = 0;
    };

}

#include "layer_stack.inl"
