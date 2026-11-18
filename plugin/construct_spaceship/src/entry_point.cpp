#include "util/pch.h"

#include <plugin_system/i_plugin.h>

#include <procedural/construct/engine/service.h>

#include "pack/ship_pack.h"



namespace GLT::construct::ship {

    // STATIC VARIABLES ================================================================================================

    static constexpr const char*                                dependencies_names[] = {

        nullptr
    };

    static constexpr GLT::plugin_manager::interface             dependencies_interfaces[] = {

        GLT::plugin_manager::interface::asset_registry,
    };

    static constexpr GLT::plugin_manager::plugin_descriptor     descriptor = {

        .name                                                   = GLT_MODULE_NAME,
        .display_name                                           = "Spaceship Construction",
        .description                                            = "Spaceship pack: frames from a hull and blocking components (Tools > Construct > Construct editor)",
        .category                                               = "World|Procedural",
        .author                                                 = "Mich-Dich",
        .plugin_version                                         = {0, 1, 0},
        .api_version                                            = {1, 0, 0},
        .load_phase                                             = GLT::plugin_manager::phase::application_ready,
        .unload_phase                                           = GLT::plugin_manager::phase::pre_application_shutdown,
        .target                                                 = GLT::plugin_manager::interface::custom,
        .dependency_names_count                                 = ARRAY_SIZE(dependencies_names),
        .dependency_names                                       = dependencies_names,
        .dependency_interface_count                             = ARRAY_SIZE(dependencies_interfaces),
        .dependency_interfaces                                  = dependencies_interfaces,
    };

    // CLASS DECLARATION ===============================================================================================

    // Adds the ship pack to the core's construct registry. The Construct editor (core) builds its UI from what the
    // pack registers: no editor code here
    class plugin final : public GLT::plugin_manager::i_plugin {
    public:

        void on_load() override {

            register_pack(construct::packs(), GLT_MODULE_NAME);
            LOG(info, "spaceship construction pack registered")
        }


        void on_unload() override {

            GLT::thread_pool::wait_for_all();                   // evaluations in flight run this plugin's pack code
            construct::packs().remove_owner(GLT_MODULE_NAME);
            LOG(info, "spaceship construction pack removed")
        }
    };

}

EXPORT_PLUGIN_CLASS(GLT::construct::ship::plugin, GLT::construct::ship::descriptor)
