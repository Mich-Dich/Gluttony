
#pragma once

#include <functional>
#include <typeindex>
#include <unordered_map>
#include <list>
#include <vector>
#include <atomic>
#include <concepts>

#include "event/event.h"

// FORWARD DECLARATIONS ================================================================================================

namespace GLT::event_bus {

    // CONCEPTS ========================================================================================================

    template<typename T>
    concept event_class = std::derived_from<T, GLT::event>;

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    template<typename T>
    using event_handler_fn = std::function<void(const T&)>;

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // Subscribe to a specific event type.
    // @return A handle that can be used to unsubscribe later.
    template<event_class T>
    FORCE_INLINE_R handle subscribe(event_handler_fn<T> handler);


    // Remove a previously added subscription.
    // @param id The handle returned from subscribe<T>().
    FORCE_INLINE void unsubscribe(handle& id);


    // Dispatch an event to all subscribers of its exact type.
    // Subscribers that are added/removed during dispatch do not affect
    // the current iteration (snapshot taken).
    template<event_class T>
    FORCE_INLINE void post(const T event);

    // CLASS DECLARATION ===============================================================================================

}

#include "event_bus.inl"
