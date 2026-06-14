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
        bool                                                                            active = true;
    };

    // STATIC VARIABLES ================================================================================================

    // Per‑type lists of subscribers.
    inline std::unordered_map<std::type_index, std::vector<subscription_entry>>         s_subscribers;

    inline std::atomic<handle>                                                          s_next_handle{1};

    // TEMPLATE IMPLEMENTATION =========================================================================================

    template<event_class T>
    FORCE_INLINE std::function<void(const event&)> make_wrapper(event_handler_fn<T> handler) {

        return [handler = std::move(handler)](const event& event) {
            handler(static_cast<const T&>(event));      // The bus only calls this when the type already matches, so the cast is safe.
        };
    }


    template<event_class T>
    FORCE_INLINE_R handle subscribe(event_handler_fn<T> handler) {

        auto id = s_next_handle.fetch_add(1, std::memory_order_relaxed);
        auto wrapper = make_wrapper<T>(std::move(handler));
        auto& vec = s_subscribers[std::type_index(typeid(T))];
        vec.emplace_back(id, std::move(wrapper));       // amortized O(1)
        return id;
    }


    FORCE_INLINE void unsubscribe(handle& id) {

        if (id == invalid_handle)   return;             // cant invalidate an invalid handle

        for (auto& [type_idx, vec] : s_subscribers) {
            auto it = std::find_if(vec.begin(), vec.end(),
                [id](const subscription_entry& e) { return e.id == id; });
            if (it != vec.end()) {
                it->active = false;
                it->callback = nullptr;                 // release captured state early
                id = invalid_handle;
                return;
            }
        }
        
        id = invalid_handle;                            // couldn't find it so invalidate handle
    }


    template<event_class T>
    FORCE_INLINE void post(const T event) {

        auto it = s_subscribers.find(std::type_index(typeid(T)));
        if (it == s_subscribers.end()) return;

        for (auto& entry : it->second) {
            if (!entry.active) continue;                // skip unsubscribed
            entry.callback(event);
        }
    }


    FORCE_INLINE void purge_dead_subscribers() {

        for (auto& [type_idx, vec] : s_subscribers) {
            vec.erase(
                std::remove_if(vec.begin(), vec.end(),
                    [](const subscription_entry& entry) { return !entry.active; }),
                vec.end()
            );
            // If vector becomes empty, optionally erase the type slot, but not necessary
        }
    }

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
