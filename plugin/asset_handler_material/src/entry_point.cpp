#include "util/pch.h"

#include <asset/type.h>
#include <asset/material.h>
#include <plugin_system/i_project_manager.h>
#include <asset/i_asset_handler.h>
#include <asset/i_asset_registry.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::handler::material {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    static constexpr const char*                                dependencies_names[] = {

        nullptr
    };

    static constexpr GLT::plugin_manager::interface             dependencies_interfaces[] = {

        GLT::plugin_manager::interface::virtual_file_system,
        GLT::plugin_manager::interface::asset_registry,
    };

    static constexpr GLT::plugin_manager::plugin_descriptor     descriptor = {

        .name                                                   = GLT_MODULE_NAME,
        .load_phase                                             = GLT::plugin_manager::phase::pre_application,
        .unload_phase                                           = GLT::plugin_manager::phase::post_application_shutdown,
        .target                                                 = GLT::plugin_manager::interface::custom,
        .dependency_names_count                                 = ARRAY_SIZE(dependencies_names),
        .dependency_names                                       = dependencies_names,
        .dependency_interface_count                             = ARRAY_SIZE(dependencies_interfaces),
        .dependency_interfaces                                  = dependencies_interfaces,
    };

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    class plugin final : public GLT::asset::i_asset_handler {
    public:

        void on_load()   override;


        void on_unload() override;

        // ---- i_asset_handler ----

        [[nodiscard]] std::span<const GLT::asset::type> types() const noexcept override;


        [[nodiscard]] std::expected<GLT::unique_ref<GLT::asset::i_runtime_asset>, GLT::asset::load_error>
            deserialize(const GLT::asset::info& info, GLT::asset::chunk_reader& reader) override;


        // Materials created at runtime (register_runtime) are the only writer
        // of material files in this project until an importer exists, so we
        // have to be able to round-trip them back to disk.
        [[nodiscard]] std::expected<void, GLT::asset::load_error>
            serialize(const GLT::asset::info& info, const GLT::asset::i_runtime_asset& asset,
                GLT::asset::asset_writer& out) const override;

    private:

        GLT::ref<GLT::asset::i_asset_registry_plugin>           m_registry{};
    };

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

#include "plugin.inl"

EXPORT_PLUGIN_CLASS(GLT::asset::handler::material::plugin, GLT::asset::handler::material::descriptor)
