#pragma once


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::event_bus {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct subscription_entry {
        handle                                                                                  id{};
        std::function<void(GLT::event&)>                                                        callback{};
        i32                                                                                     priority = 0;
        bool                                                                                    active = true;
    };

    using subscription_entry_ptr = std::shared_ptr<subscription_entry>;

    // STATIC VARIABLES ================================================================================================

    // Per-type lists of subscribers, sorted by priority (higher first).
    inline std::unordered_map<std::type_index, std::vector<subscription_entry_ptr>>             s_subscribers{};

    inline std::atomic<handle>                                                                  s_next_handle{1};

    inline std::unordered_map<handle, std::type_index>                                          s_handle_to_type{};

    namespace detail {

        // Reentrancy-safe snapshot pool. One buffer per dispatch depth.
        inline thread_local std::vector<std::unique_ptr<std::vector<subscription_entry_ptr>>>   s_snapshot_pool{};

        inline thread_local size_t                                                              s_snapshot_depth = 0;

    }

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    template<event_class T>
    FORCE_INLINE std::function<void(GLT::event&)> make_wrapper(event_handler_fn<T> handler);

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    template<event_class T>
    FORCE_INLINE std::function<void(GLT::event&)> make_wrapper(event_handler_fn<T> handler) {

        return [handler = std::move(handler)](GLT::event& event) { handler(static_cast<T&>(event)); };
    }

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    FORCE_INLINE void unsubscribe(handle& id) {

        if (id == INVALID_HANDLE)
            return;

        auto type_it = s_handle_to_type.find(id);
        if (type_it == s_handle_to_type.end()) {
            id = INVALID_HANDLE;
            return;
        }

        auto sub_it = s_subscribers.find(type_it->second);
        if (sub_it != s_subscribers.end()) {

            auto& vec = sub_it->second;
            auto entry_it = std::find_if(
                vec.begin(), vec.end(),
                [id](const subscription_entry_ptr& entry) {
                    return entry->id == id;
                });

            if (entry_it != vec.end()) {
                (*entry_it)->active = false;
                (*entry_it)->callback = nullptr;
                vec.erase(entry_it); // safe because post() holds shared_ptr snapshots
            }
        }

        s_handle_to_type.erase(type_it);
        id = INVALID_HANDLE;
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    template<event_class T>
    FORCE_INLINE_R handle subscribe(event_handler_fn<T> handler, i32 priority) {

        auto id = s_next_handle.fetch_add(1, std::memory_order_relaxed);

        auto entry = std::make_shared<subscription_entry>();
        entry->id = id;
        entry->callback = make_wrapper<T>(std::move(handler));
        entry->priority = priority;
        entry->active = true;

        auto type_idx = std::type_index(typeid(T));
        auto& vec = s_subscribers[type_idx];

        // Insert before the first entry with lower priority. Equal priorities keep FIFO order.
        auto pos = std::find_if(vec.begin(), vec.end(), [priority](const subscription_entry_ptr& e) { return e->priority < priority; });
        vec.insert(pos, std::move(entry));
        s_handle_to_type.insert_or_assign(id, type_idx);
        return id;
    }


    template<event_class T>
    FORCE_INLINE void post(T event) {

        auto it = s_subscribers.find(std::type_index(typeid(T)));
        if (it == s_subscribers.end())
            return;

        // Get a reentrancy-safe snapshot buffer for this depth.
        if (detail::s_snapshot_depth == detail::s_snapshot_pool.size())
            detail::s_snapshot_pool.push_back(std::make_unique<std::vector<subscription_entry_ptr>>());

        const size_t depth = detail::s_snapshot_depth++;

        struct depth_guard {
            size_t&                     d;
            ~depth_guard() { --d; }
        } guard{ detail::s_snapshot_depth };

        auto& snapshot = *detail::s_snapshot_pool[depth];
        snapshot.clear();
        snapshot.reserve(it->second.size());

        for (const auto& entry : it->second)
            if (entry->active)
                snapshot.push_back(entry);

        for (const auto& entry : snapshot) {

            if (!entry->active)
                continue;

            entry->callback(event);

            if (event.get_handled())
                break;
        }
    }


    template<event_class T>
    FORCE_INLINE_R subscription_guard subscribe_scoped(event_handler_fn<T> handler, i32 priority) {

        return subscription_guard{ subscribe<T>(std::move(handler), priority), detail::unsubscribe_fn{} };
    }

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
