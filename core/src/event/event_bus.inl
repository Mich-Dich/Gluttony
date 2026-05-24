#pragma once

#include "util/pch.h"
#include "event_bus.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::event_bus {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct subscription_entry {
        handle                                                                          id;
        std::function<void(const GLT::event&)>                                          callback;
    };

    // STATIC VARIABLES ================================================================================================

    // Per‑type lists of subscribers.
    inline std::unordered_map<std::type_index, std::list<subscription_entry>>           s_subscribers;

    inline std::atomic<handle>                                                          s_next_handle{1};

    // TEMPLATE IMPLEMENTATION =========================================================================================

    template<event_class T>
    FORCE_INLINE std::function<void(const event&)> make_wrapper(event_handler_fn<T> handler) {

        return [handler = std::move(handler)](const event& event) {
            handler(static_cast<const T&>(event));        // The bus only calls this when the type already matches, so the cast is safe.
        };
    }


    template<event_class T>
    FORCE_INLINE_R handle subscribe(event_handler_fn<T> handler) {

        auto id = s_next_handle.fetch_add(1, std::memory_order_relaxed);
        auto wrapper = make_wrapper<T>(std::move(handler));
        auto& list = s_subscribers[std::type_index(typeid(T))];
        list.emplace_back(id, std::move(wrapper));
        return id;
    }


    FORCE_INLINE void unsubscribe(handle id) {
            
        for (auto& [type_idx, list] : s_subscribers) {
            // Find the subscription with the matching ID and erase it.
            auto it = std::find_if(list.begin(), list.end(), 
                [id](const subscription_entry& entry) { return entry.id == id; });

            if (it != list.end()) {
                list.erase(it);
                if (list.empty())       // If the list became empty, remove the type slot.
                    s_subscribers.erase(type_idx);
                return;   // Handles are unique – we can stop after the first erase.
            }
        }
    }


    template<event_class T>
    FORCE_INLINE void post(const T event) {
        
        auto it = s_subscribers.find(std::type_index(typeid(event)));
        if (it == s_subscribers.end()) return;

        // Copy the callbacks to allow safe mutation during iteration.
        std::vector<std::function<void(const GLT::event&)>> callbacks;
        callbacks.reserve(it->second.size());
        for (const auto& entry : it->second)
            callbacks.push_back(entry.callback);

        // Fire each callback.
        for (auto& loc_callback : callbacks)
            loc_callback(event);
    }

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
