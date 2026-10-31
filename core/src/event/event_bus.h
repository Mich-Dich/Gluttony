#pragma once

#include <functional>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <concepts>
#include <memory>
#include <algorithm>

#include "event/event.h"

// FORWARD DECLARATIONS ================================================================================================

namespace GLT::event_bus {

    // CONCEPTS ========================================================================================================

    // MACROS ==========================================================================================================

    template<typename T>
    concept event_class = std::derived_from<T, GLT::event>;

    // TYPES ===========================================================================================================

    template<typename T>
    using event_handler_fn = std::function<void(T&)>;

    class subscription_guard;   // fwd – bus::subscribe_scoped returns it

    // STATIC VARIABLES ================================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief One event bus. Owns its own subscription tables, handle counter, and reentrancy-safe scratch buffers
    //
    // A single process-wide instance is available via [global()]. The namespace-level free functions below forward to it,
    // so existing code that calls [event_bus::subscribe<T>()] / [event_bus::post()] keeps working unchanged
    //
    // Independent instances (e.g. one per game_session) are created with [event_bus::bus my_bus;] and use the same API
    // as member functions
    //
    // Not thread-safe. Intended to be driven from a single thread (typically the main thread that steps systems)
    class bus {
    public:

        bus() = default;
        ~bus() = default;

        DELETE_COPY_AND_MOVE_CONSTRUCTOR(bus)

        // @brief Subscribes to a specific event type
        //
        // Higher-priority subscribers run first. Subscribers with equal priority run in registration order. Subscribers
        // added/removed during a dispatch don't affect the current iteration
        template<event_class T>
        handle subscribe(event_handler_fn<T> handler, i32 priority = 0);


        // @brief Dispatches an event to every subscriber of its exact type
        //
        // Takes the event by value so the bus owns a mutable copy; subscribers can set [handled] to stop the cascade
        template<event_class T>
        void post(T event);


        // @brief Removes a previously-registered subscription. Idempotent. Resets [id] to INVALID_HANDLE
        void unsubscribe(handle& id);


        // @brief Like subscribe<T>(), but returns an RAII guard that unsubscribes on destruction
        template<event_class T>
        subscription_guard subscribe_scoped(event_handler_fn<T> handler, i32 priority = 0);

    private:

        struct subscription_entry {
            handle                                                                              id{};
            std::function<void(GLT::event&)>                                                    callback{};
            i32                                                                                 priority = 0;
            bool                                                                                active = true;
        };
        using subscription_entry_ptr = std::shared_ptr<subscription_entry>;

        template<event_class T>
        static std::function<void(GLT::event&)> make_wrapper(event_handler_fn<T> handler);

        std::unordered_map<std::type_index, std::vector<subscription_entry_ptr>>                m_subscribers{};
        std::atomic<handle>                                                                     m_next_handle{1};
        std::unordered_map<handle, std::type_index>                                             m_handle_to_type{};

        // Reentrancy-safe snapshot pool. One buffer per dispatch depth
        std::vector<std::unique_ptr<std::vector<subscription_entry_ptr>>>                       m_snapshot_pool{};
        size_t                                                                                  m_snapshot_depth = 0;

    };


    // @brief RAII wrapper around a subscription handle bound to a specific bus
    //
    // The guard does NOT keep the bus alive. It must not outlive the bus it was created from (the global bus never dies,
    // so this only bites per-session buses – tear the guards down before destroying the session)
    class subscription_guard {
    public:

        subscription_guard() = default;
        subscription_guard(bus* b, handle id) noexcept;
        subscription_guard(subscription_guard&& other) noexcept;
        subscription_guard(const subscription_guard&) = delete;
        ~subscription_guard();


        subscription_guard& operator=(const subscription_guard&) = delete;
        
        
        subscription_guard& operator=(subscription_guard&& other) noexcept;


        void reset() noexcept;


        [[nodiscard]] handle get() const noexcept;


        [[nodiscard]] bool valid() const noexcept;

    private:

        bus*                                                                                    m_bus = nullptr;
        handle                                                                                  m_id = INVALID_HANDLE;
    };


    // @brief The process-wide bus. Used by the free-function forwarders below
    [[nodiscard]] bus& global();


    // FUNCTION DECLARATION ============================================================================================

    // Everything below forwards to [global()]. They exist so existing call sites don't have to change. For a local bus,
    // call the member function on the bus instance instead

    inline void unsubscribe(handle& id);

    // TEMPLATE DECLARATION ============================================================================================

    template<event_class T>
    handle subscribe(event_handler_fn<T> handler, i32 priority = 0);


    template<event_class T>
    void post(T event);


    template<event_class T>
    subscription_guard subscribe_scoped(event_handler_fn<T> handler, i32 priority = 0);

}

#include "event_bus.inl"
