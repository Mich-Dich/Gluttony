
#include "util/pch.h"

#include <plugin_system/i_asset_factory_plugin.h>
#include <plugin_system/i_asset_registry_plugin.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::factory::audio_miniaudio {

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

    class plugin final : public GLT::asset::factory::i_asset_factory_plugin {
    public:

        void on_load() override;


        void on_unload() override;


        [[nodiscard]] std::span<const GLT::asset::factory::binding> bindings() const noexcept override;


        [[nodiscard]] std::expected<GLT::asset::factory::import_result, GLT::asset::import_error> import(
            const std::filesystem::path& source, GLT::asset::type target_type, const GLT::asset::import_options& opts,
            GLT::asset::asset_writer& out) override;

    private:

        GLT::ref<GLT::asset::i_asset_registry_plugin>           m_registry{};

    };

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

#include "plugin.inl"

EXPORT_PLUGIN_CLASS(GLT::asset::factory::audio_miniaudio::plugin, GLT::asset::factory::audio_miniaudio::descriptor)
