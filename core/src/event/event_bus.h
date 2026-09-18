
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

    // FUNCTION DECLARATION ============================================================================================

    // Remove a previously added subscription.
    // @param id The handle returned from subscribe<T>().
    FORCE_INLINE void unsubscribe(handle& id);

    // TYPES ===========================================================================================================

    namespace detail {

        // Stateless, zero-size callable so scoped_resource stays POD-ish.
        struct unsubscribe_fn {

            void operator()(handle& h) const noexcept { unsubscribe(h); }
        };

    }


    // A subscription that unsubscribes itself on destruction.
    using subscription_guard = util::scoped_resource<handle, detail::unsubscribe_fn>;


    template<typename T>
    using event_handler_fn = std::function<void(const T&)>;

    // STATIC VARIABLES ================================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // Subscribe to a specific event type.
    // @return A handle that can be used to unsubscribe later.
    template<event_class T>
    FORCE_INLINE_R handle subscribe(event_handler_fn<T> handler);


    // Dispatch an event to all subscribers of its exact type.
    // Subscribers that are added/removed during dispatch do not affect
    // the current iteration (snapshot taken).
    template<event_class T>
    FORCE_INLINE void post(const T event);


    // Factory: same signature as subscribe<T>(), but returns an RAII guard.
    template<event_class T>
    FORCE_INLINE_R subscription_guard subscribe_scoped(event_handler_fn<T> handler);

    // CLASS DECLARATION ===============================================================================================

}

#include "event_bus.inl"
