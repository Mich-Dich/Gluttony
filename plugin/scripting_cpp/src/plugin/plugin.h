
#pragma once

#include <scripting/i_script_plugin.h>

#include "class_registry.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::scripting_cpp {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Phase 0 skeleton. Every pure virtual from i_script_plugin / i_plugin is stubbed:
    //   - on_load / on_unload  → LOG, no registry work
    //   - create_host          → returns nullptr (Phase 2 fills in the real host)
    //   - registry / globals   → LOG stub, return empty
    //
    // The plugin loads, is discoverable via plugin_manager, and unloads cleanly.
    class plugin final : public GLT::script::i_script_plugin {
    public:

        plugin() = default;

        ~plugin() override = default;

        // ---- i_plugin -------------------------------------------------------

        void on_load() override;


        void on_unload() override;


        void update(f32 delta_time) override;

        // ---- i_script_plugin: registry --------------------------------------

        void register_script_class_impl(GLT::script::script_class_info info) override;


        void unregister_script_class(std::string_view name) override;


        void unregister_by_registrant(std::string_view registrant) override;

        // ---- i_script_plugin: host factory ----------------------------------

        [[nodiscard]] GLT::unique_ref<GLT::script::i_script_host> create_host() override;

        // ---- i_script_plugin: default globals -------------------------------

        void set_default_globals(std::string_view registrant, std::span<const GLT::script::default_global> globals) override;


        void clear_default_globals(std::string_view registrant) override;


        [[nodiscard]] std::span<const GLT::script::default_global> default_globals() const override;

        // ---- i_script_plugin: introspection ---------------------------------

        [[nodiscard]] std::span<const GLT::script::script_class_info> registered_classes() const override;

        class_registry                                      m_classes;
        global_registry                                     m_globals;

    };

}
