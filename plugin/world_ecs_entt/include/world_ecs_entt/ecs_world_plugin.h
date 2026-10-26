
#pragma once

#include <world/i_world_plugin.h>

#include "world_asset_handler.h"
#include "entity_codec.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Thin factory. All world state (ECS registry, slots, regions, streaming) lives on ecs_world. The plugin retains ownership
    // of the shared blueprint:
    //   - the asset handler (one per app)
    //   - the entity codec (stateless: all callbacks take entt::registry&)
    class ecs_world_plugin final : public i_world_plugin {
    public:

        void on_load() override;
        void on_unload() override;

        [[nodiscard]] std::expected<unique_ref<i_world>, asset::load_error> instantiate(asset::asset_ref asset) override;


        [[nodiscard]] std::expected<unique_ref<i_world>, asset::load_error> instantiate_from_path(const std::filesystem::path& path) override;


        [[nodiscard]] std::expected<void, asset::load_error> save_world(i_world& world) override;


        [[nodiscard]] std::expected<void, asset::load_error> save_world_as(i_world& world, const std::filesystem::path& path) override;


        // Shared blueprint accessors used by ecs_world
        [[nodiscard]] const entity_codec& codec() const noexcept { return m_codec; }

    private:

        world_asset_handler                             m_asset_handler;
        entity_codec                                    m_codec;

    };

}
