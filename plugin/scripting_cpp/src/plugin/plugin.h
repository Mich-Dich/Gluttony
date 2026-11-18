
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

    class script_host;


    // The C++ scripting plugin: the class registry (filled by game modules through register_module), the script hosts
    // of the sessions, the script_bindings component and its inspector, and the editor's Tools > Scripts menu
    // (editor actions)
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
        // Releases the module's instances in every host first (their persist state comes back when it registers again)
        void unregister_by_registrant(std::string_view registrant) override;

        // ---- i_script_plugin: host factory ----------------------------------
        [[nodiscard]] GLT::unique_ref<GLT::script::i_script_host> create_host() override;

        // ---- i_script_plugin: default globals -------------------------------
        void set_default_globals(std::string_view registrant, std::span<const GLT::script::default_global> globals) override;
        void clear_default_globals(std::string_view registrant) override;
        [[nodiscard]] std::span<const GLT::script::default_global> default_globals() const override;

        // ---- i_script_plugin: introspection ---------------------------------
        [[nodiscard]] std::span<const GLT::script::script_class_info> registered_classes() const override;

        // ---- plugin side ----------------------------------------------------
        // Pointer valid until the registry changes: copy what you keep
        [[nodiscard]] const GLT::script::script_class_info* find_class(std::string_view name) const { return m_classes.find(name); }

        void add_host(script_host* host);
        void remove_host(script_host* host);
        [[nodiscard]] std::span<script_host* const> hosts() const noexcept { return m_hosts; }
        // The running host of [world] (null if no session runs scripts on it)
        [[nodiscard]] script_host* running_host_for(const GLT::world::i_world* world) const;
        // The host of [world], running or not (editor actions run in the edited world)
        [[nodiscard]] script_host* host_for(const GLT::world::i_world* world) const;

        [[nodiscard]] static plugin* get() noexcept { return s_instance; }

    private:

        void classes_changed();

        std::vector<script_host*>                           m_hosts{};
        bool                                                m_menu_dirty = false;
        static inline plugin*                               s_instance = nullptr;

    public:

        class_registry                                      m_classes;
        global_registry                                     m_globals;

    };

}
