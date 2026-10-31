#pragma once

#include "util/pch.h"
#include "plugin_manager.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::plugin_manager {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // EXTERN VARIABLES ================================================================================================

    namespace detail {

        extern std::array<std::atomic<i_plugin*>, INTERFACE_CACHE_SIZE>             g_interface_raw_cache;

    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    FORCE_INLINE void enter_phase(const phase current_phase) {
        plugin_manager::load_plugins(current_phase);
        plugin_manager::unload_plugins(current_phase);
    }


    template<typename T>
    FORCE_INLINE_R ref<T> get_plugin_ref(const std::string& name) { return std::dynamic_pointer_cast<T>(get_plugin_base(name)); }


    template<typename T>
    FORCE_INLINE_R ref<T> get_plugin_ref(const interface targeted) { return std::dynamic_pointer_cast<T>(get_plugin_base(targeted)); }


    template<typename T>
    FORCE_INLINE_R ref<T> get_plugin_ref_unsafe(interface targeted) { return std::static_pointer_cast<T>(get_plugin_base(targeted)); }


    // One shared_ptr hand-off, then implicit conversion to weak — no extra inc/dec
    template<typename T>
    FORCE_INLINE_R weak_ref<T> get_plugin(const std::string& name) { return std::dynamic_pointer_cast<T>(get_plugin_base(name)); }


    template<typename T>
    FORCE_INLINE_R weak_ref<T> get_plugin(const interface targeted) { return std::dynamic_pointer_cast<T>(get_plugin_base(targeted)); }


    // Single acquire load + bounds-check; no atomics on the refcount, no RTTI.
    template<typename T>
    FORCE_INLINE T* get_plugin_raw(interface targeted) noexcept { return static_cast<T*>(detail::get_cached_interface_ptr(targeted)); }

    namespace detail {

        FORCE_INLINE_R i_plugin* get_cached_interface_ptr(interface targeted) noexcept {

            if (targeted == interface::custom)
                return nullptr;                                     // custom = shared slot, no 1:1 plugin
            return g_interface_raw_cache[static_cast<size_t>(targeted)].load(std::memory_order_acquire);
        }

    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
