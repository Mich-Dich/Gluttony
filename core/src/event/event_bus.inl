
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::event_bus {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    inline bus& global() {
        static bus s_global_bus;
        return s_global_bus;
    }

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    inline void unsubscribe(handle& id) { global().unsubscribe(id); }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    template<event_class T>
    handle subscribe(event_handler_fn<T> handler, i32 priority) {

        return global().subscribe<T>(std::move(handler), priority);
    }


    template<event_class T>
    void post(T event) { global().post<T>(std::move(event)); }


    template<event_class T>
    subscription_guard subscribe_scoped(event_handler_fn<T> handler, i32 priority) {

        return global().subscribe_scoped<T>(std::move(handler), priority);
    }

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    template<event_class T>
    handle bus::subscribe(event_handler_fn<T> handler, i32 priority) {

        auto id = m_next_handle.fetch_add(1, std::memory_order_relaxed);

        auto entry = std::make_shared<subscription_entry>();
        entry->id = id;
        entry->callback = make_wrapper<T>(std::move(handler));
        entry->priority = priority;
        entry->active = true;

        auto type_idx = std::type_index(typeid(T));
        auto& vec = m_subscribers[type_idx];

        // Insert before the first entry with lower priority. Equal priorities keep FIFO order
        auto pos = std::find_if(
            vec.begin(), vec.end(),
            [priority](const subscription_entry_ptr& e) { return e->priority < priority; });
        vec.insert(pos, std::move(entry));
        m_handle_to_type.insert_or_assign(id, type_idx);
        return id;
    }


    template<event_class T>
    void bus::post(T event) {

        auto it = m_subscribers.find(std::type_index(typeid(T)));
        if (it == m_subscribers.end())
            return;

        if (m_snapshot_depth == m_snapshot_pool.size())
            m_snapshot_pool.push_back(std::make_unique<std::vector<subscription_entry_ptr>>());

        const size_t depth = m_snapshot_depth++;

        struct depth_guard {
            size_t&                     d;
            ~depth_guard() { --d; }
        } guard{ m_snapshot_depth };

        auto& snapshot = *m_snapshot_pool[depth];
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


    inline void bus::unsubscribe(handle& id) {

        if (id == INVALID_HANDLE)
            return;

        auto type_it = m_handle_to_type.find(id);
        if (type_it == m_handle_to_type.end()) {
            id = INVALID_HANDLE;
            return;
        }

        auto sub_it = m_subscribers.find(type_it->second);
        if (sub_it != m_subscribers.end()) {

            auto& vec = sub_it->second;
            auto entry_it = std::find_if(
                vec.begin(), vec.end(),
                [id](const subscription_entry_ptr& entry) { return entry->id == id; });

            if (entry_it != vec.end()) {
                (*entry_it)->active = false;
                (*entry_it)->callback = nullptr;
                vec.erase(entry_it);    // safe: post() holds shared_ptr snapshots
            }
        }

        m_handle_to_type.erase(type_it);
        id = INVALID_HANDLE;
    }


    template<event_class T>
    subscription_guard bus::subscribe_scoped(event_handler_fn<T> handler, i32 priority) {

        return subscription_guard{ this, subscribe<T>(std::move(handler), priority) };
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    template<event_class T>
    std::function<void(GLT::event&)> bus::make_wrapper(event_handler_fn<T> handler) {

        return [handler = std::move(handler)](GLT::event& event) {
            handler(static_cast<T&>(event));
        };
    }

}
