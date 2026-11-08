
#include <util/pch.h>
#include "plugin/plugin.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::scripting_cpp {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // i_plugin --------------------------------------------------------------------------------------------------------

    void plugin::on_load() {

        LOG(info, "loaded (Phase 0 skeleton)")
    }


    void plugin::on_unload() {

        LOG(info, "unloaded")
    }


    void plugin::update(f32 /*delta_time*/) {

        // Intentionally empty. The scripting plugin has no per-frame work of its own; hosts are driven by their sessions
    }

    // i_script_plugin: registry ---------------------------------------------------------------------------------------

    void plugin::register_script_class_impl(script::script_class_info info) {

        m_classes.add(std::move(info));
    }


    void plugin::unregister_script_class(std::string_view name) {

        m_classes.remove(name);
    }


    void plugin::unregister_by_registrant(std::string_view registrant) {

        m_classes.remove_by_registrant(registrant);
    }

    // i_script_plugin: host factory -----------------------------------------------------------------------------------

    unique_ref<script::i_script_host> plugin::create_host() {

        LOG(warn, "create_host not implemented yet (Phase 2)")
        return nullptr;
    }

    // i_script_plugin: default globals --------------------------------------------------------------------------------

    void plugin::set_default_globals(std::string_view registrant, std::span<const script::default_global> globals) {

        m_globals.set(registrant, globals);
    }


    void plugin::clear_default_globals(std::string_view registrant) {

        m_globals.clear(registrant);
    }


    std::span<const script::default_global> plugin::default_globals() const {

        return m_globals.all();
    }

    // i_script_plugin: introspection ----------------------------------------------------------------------------------

    std::span<const script::script_class_info> plugin::registered_classes() const {

        return m_classes.all();
    }

}
