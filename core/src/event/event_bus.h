
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

    // @brief Constrains a template parameter to concrete subclasses of GLT::event
    template<typename T>
    concept event_class = std::derived_from<T, GLT::event>;

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // @brief Removes a previously-registered subscription
    //
    // Idempotent - passing INVALID_HANDLE, or a handle that was already consumed, is a no-op. The handle is reset to
    // INVALID_HANDLE on return
    //
    // @param id  Handle returned by [subscribe<T>()]. Set to INVALID_HANDLE
    FORCE_INLINE void unsubscribe(handle& id);

    // TYPES ===========================================================================================================

    namespace detail {

        // Stateless, zero-size callable so scoped_resource stays POD-ish
        struct unsubscribe_fn {

            void operator()(handle& h) const noexcept { unsubscribe(h); }
        };

    }


    // @brief RAII wrapper around a subscription handle
    //
    // Calls [unsubscribe()] on destruction, so a subscription can be scoped to a lifetime (a system, a window, a plugin)
    // without manual teardown
    using subscription_guard = util::scoped_resource<handle, detail::unsubscribe_fn>;


    // @brief The callable a subscriber registers
    //
    // Receives a mutable reference to the concrete event, so handlers can set [handled] if they want to stop further dispatch
    // @tparam T The concrete event type
    template<typename T>
    using event_handler_fn = std::function<void(T&)>;

    // STATIC VARIABLES ================================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // @brief Subscribes to a specific event type
    //
    // Higher-priority subscribers run first. Subscribers with equal priority are invoked in registration order
    // Subscribers added or removed during a dispatch don't affect the current iteration (the bus takes a snapshot)
    //
    // @tparam T         The concrete event type to listen for
    // @param handler    Callable invoked with a mutable reference to the event
    // @param priority   Higher values run earlier; equal values keep FIFO order
    // @return A handle that can be passed to [unsubscribe()]
    template<event_class T>
    FORCE_INLINE_R handle subscribe(event_handler_fn<T> handler, i32 priority = 0);


    // @brief Dispatches an event to every subscriber of its exact type
    //
    // Takes the event by value so it owns a mutable copy; subscribers can set [handled] to stop the cascade early
    // Subscribers that are added or removed during dispatch do not affect the current iteration
    //
    // @tparam T      The concrete event type; must match the runtime type of [event]
    // @param event   The event instance to broadcast
    template<event_class T>
    FORCE_INLINE void post(T event);          // by value, so we own a mutable copy


    // @brief Same as [subscribe<T>()], but returns an RAII guard
    //
    // The subscription is automatically torn down when the guard goes out of scope, which is the recommended way to bind
    // a subscription to a system's lifetime
    //
    // @tparam T         The concrete event type to listen for
    // @param handler    Callable invoked with a mutable reference to the event
    // @param priority   Higher values run earlier; equal values keep FIFO order
    // @return An RAII guard that unsubscribes on destruction
    template<event_class T>
    FORCE_INLINE_R subscription_guard subscribe_scoped(event_handler_fn<T> handler, i32 priority = 0);

    // CLASS DECLARATION ===============================================================================================

}

#include "event_bus.inl"
