
#pragma once

#include <world/i_world_plugin.h>
#include <world/entity_preset.h>

#include "world_asset_handler.h"
#include "entity_codec.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {
    class entity_builder;
    class ecs_world;
}

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

        // A preset component factory converts a [component_spec] into one or more ECS components on the target entity.
        // World plugin registers the core ones ("light", "camera", "audio_source", ...). User plugins extend the system
        // by calling register_preset_component() with their own factories - no core change needed
        using preset_factory_fn = std::function<void(entity_builder&, const GLT::world::component_spec&)>;
        void on_load() override;
        void on_unload() override;

        [[nodiscard]] std::expected<unique_ref<i_world>, asset::load_error> instantiate(asset::asset_ref asset) override;


        [[nodiscard]] std::expected<unique_ref<i_world>, asset::load_error> instantiate_from_path(const std::filesystem::path& path) override;


        [[nodiscard]] std::expected<void, asset::load_error> save_world(i_world& world) override;


        [[nodiscard]] std::expected<void, asset::load_error> save_world_as(i_world& world, const std::filesystem::path& path) override;


        void register_component_type(const GLT::world::component::type_descriptor& td) override;


        bool unregister_component_type(u64 hash) override;


        // Live-world tracking — registration after instantiation needs to push into already-created worlds.
        void register_live_world(ecs_world* world) { m_live_worlds.push_back(world); }


        void unregister_live_world(ecs_world* world) { std::erase(m_live_worlds, world); }

        // Template helper used by register.h. Only useful for types the codec can serialize as-is.
        template<typename T>
        void register_codec_component(std::string_view name) { m_codec.register_component<T>(name); }


        // Shared blueprint accessors used by ecs_world
        [[nodiscard]] const entity_codec& codec() const noexcept { return m_codec; }

        // preset factories --------------------------------------------------------------------------------------------


        // Replaces an existing factory of the same name. Typically called from a plugin's on_load
        void register_preset_component(std::string_view name, preset_factory_fn fn);


        void unregister_preset_component(std::string_view name) noexcept;


        [[nodiscard]] const preset_factory_fn* find_preset_component(std::string_view name) const noexcept;

    private:

        world_asset_handler                                     m_asset_handler;
        entity_codec                                            m_codec;
        std::unordered_map<std::string, preset_factory_fn>      m_preset_factories{};
        std::vector<GLT::world::component::type_descriptor>     m_component_types{};
        std::vector<ecs_world*>                                 m_live_worlds{};

    };

}
