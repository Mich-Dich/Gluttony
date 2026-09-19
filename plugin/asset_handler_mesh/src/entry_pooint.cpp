
#include "util/pch.h"

#include <asset/type.h>
#include <asset/mesh.h>
#include <plugin_system/i_project_manager.h>
#include <plugin_system/i_asset_handler_plugin.h>
#include <plugin_system/i_asset_registry_plugin.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::handler::mesh {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // The runtime representation of a loaded mesh asset. Handlers own this
    // type; the registry only ever sees it as `i_runtime_asset*`.
    //
    // IMPORTANT: this struct OWNS the decoded geometry. The chunk_reader
    // hands out spans into a buffer that dies when the registry's load
    // function returns, so every handler must copy what it wants to keep.
    class mesh_asset final : public GLT::asset::i_runtime_asset {
    public:

        GLT::asset::type                                asset_type{ GLT::asset::core_types::static_mesh };

        std::vector<GLT::asset::mesh::vertex>           vertices;
        std::vector<u32>                                indices;
        std::vector<GLT::asset::mesh::submesh>          submeshes;
        GLT::asset::mesh::bounds                        bounds{};

        // Positional. material_handles[i] corresponds to submesh.material_slot == i.
        // Entries may be INVALID_HANDLE when a material reference couldn't be
        // resolved — the renderer is expected to substitute a fallback.
        std::vector<GLT::asset::handle>                 material_handles;


        [[nodiscard]] GLT::asset::type type() const noexcept override;


        [[nodiscard]] u64 memory_usage() const noexcept override;

    };

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
        .load_phase                                             = GLT::plugin_manager::phase::application_ready,
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

    // CLASS PUBLIC ====================================================================================================

    class plugin final : public GLT::asset::i_asset_handler {
    public:

        void on_load()   override;


        void on_unload() override;

        // ---- i_asset_handler ----

        [[nodiscard]] std::span<const GLT::asset::type> types() const noexcept override;


        [[nodiscard]] std::expected<std::unique_ptr<GLT::asset::i_runtime_asset>, GLT::asset::load_error>
            deserialize(const GLT::asset::info& info, GLT::asset::chunk_reader& reader) override;

    private:

        GLT::ref<GLT::asset::i_asset_registry_plugin>           m_registry{};
    };

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

#include "mesh_asset.inl"
#include "plugin.inl"

EXPORT_PLUGIN_CLASS(GLT::asset::handler::mesh::plugin, GLT::asset::handler::mesh::descriptor)
