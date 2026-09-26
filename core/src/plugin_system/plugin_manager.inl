#pragma once

#include "util/pch.h"
#include "plugin_manager.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::plugin_manager {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    FORCE_INLINE void enter_phase(const phase current_phase) {

        plugin_manager::load_plugins(current_phase);
        plugin_manager::unload_plugins(current_phase);
    }


    template<typename T>
    FORCE_INLINE_R ref<T> get_plugin_ref(const std::string& name) {

        return std::dynamic_pointer_cast<T>(get_plugin_base(name));
    }


    template<typename T>
    FORCE_INLINE_R ref<T> get_plugin_ref(const interface targeted) {

        return std::dynamic_pointer_cast<T>(get_plugin_base(targeted));
    }


    template<typename T>
    FORCE_INLINE_R weak_ref<T> get_plugin(const std::string& name) {

        return std::dynamic_pointer_cast<T>(get_plugin_base(name));
    }


    template<typename T>
    FORCE_INLINE_R weak_ref<T> get_plugin(const interface targeted) {

        return std::dynamic_pointer_cast<T>(get_plugin_base(targeted));
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
