
#pragma once

#include "plugin_system/i_plugin.h"
#include "plugin_system/plugin_manager.h"
#include "reflection/registry.h"
#include "scripting/annotations.h"
#include "scripting/i_script.h"
#include "scripting/i_script_host.h"
#include "util/pch.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::script {
    class i_script_plugin;
}

namespace GLT::script {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    enum class script_kind : u8 {

        entity_script = 0,
        global_script,
        world_generator,
        editor_action,
    };

    // Role of a field inside a script class. Produced by register_script_class<T>
    // from the field's script-level annotations. Distinct from reflect::member_descriptor,
    // which describes the data member generically
    enum class script_field_role : u8 {
        runtime = 0,        // unmarked, serializable — never serialized
        persist,            // [[=persist]]
        editor,             // [[=editor]]
        editor_persist,     // [[=editor]] [[=persist]]
        transient,          // [[=transient]] — re-initialized from source defaults
    };


    struct script_field_info {

        const reflect::member_descriptor*           member; // offset + write_fn / read_fn
        script_field_role                           role;
        const char*                                 editor_category; // empty unless role == editor
    };


    // Live-instance method callable via i_script_host::call(...)
    struct script_method_info {

        const char*                                 name;
        const char*                                 label; // empty = use name

        // invoked from the scripting plugin; the pointer lives in the game plugin .so
        void (*invoke)(i_script*, script_context&, std::span<const std::byte>);
    };


    struct script_class_info {

        std::string_view                            name;
        script_kind                                 kind;
        std::string_view                            editor_label; // for editor_action
        std::string_view                            registrant;
        std::span<const script_field_info>          fields;
        std::span<const script_method_info>         callable_methods;

        // construction / destruction pointers live in the registering .so
        i_script* (*create)();
        void (*destroy)(i_script*);
    };


    // One default-global entry, registered by the game plugin (D7)
    struct default_global {

        std::string_view                            script_name;
        std::string_view                            instance_name;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    namespace manager {

        FORCE_INLINE_R i_script_plugin* get_ptr() noexcept {
            return plugin_manager::get_plugin_raw<i_script_plugin>(plugin_manager::interface::scripting);
        }

        FORCE_INLINE_R ref<i_script_plugin> get_ref() {
            return plugin_manager::get_plugin_ref_unsafe<i_script_plugin>(plugin_manager::interface::scripting);
        }

        // Full hot-reload sequence for one game plugin
        //   for each session: prepare_hot_reload -> snapshot_and_release
        //   plugin_manager::unload_plugin(name)
        //   plugin_manager::load_plugin(name)
        //   for each session: restore_state
        //
        // Implemented by the scripting plugin (declared here so core callers can invoke it without linking to the plugin)
        [[nodiscard]] std::expected<void, script_error> reload_game_plugin(std::string_view plugin_name, std::string_view registrant = {});

    }

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class i_script_plugin : public plugin_manager::i_plugin {
    public:

        // registry ----------------------------------------------------------------------------------------------------

        virtual void register_script_class_impl(script_class_info info) = 0;


        virtual void unregister_script_class(std::string_view name) = 0;


        virtual void unregister_by_registrant(std::string_view registrant) = 0;

        // host factory ------------------------------------------------------------------------------------------------

        [[nodiscard]] virtual unique_ref<i_script_host> create_host() = 0;

        // default globals (D7) ----------------------------------------------------------------------------------------

        virtual void set_default_globals(std::string_view registrant, std::span<const default_global> globals) = 0;


        virtual void clear_default_globals(std::string_view registrant) = 0;


        [[nodiscard]] virtual std::span<const default_global> default_globals() const = 0;

        // introspection -----------------------------------------------------------------------------------------------

        [[nodiscard]] virtual std::span<const script_class_info> registered_classes() const = 0;

        // templates (instantiated in the game plugin .so) -------------------------------------------------------------

        template <typename T>
        requires std::derived_from<T, i_script>
        void register_script_class(std::string_view registrant = {});


        template <auto Namespace> void register_scripts_in_namespace(std::string_view registrant = {});

    };

}

#include "scripting/i_script_plugin.inl"
