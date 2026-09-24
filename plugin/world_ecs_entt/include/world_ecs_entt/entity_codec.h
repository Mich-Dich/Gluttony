
#pragma once

#include <entt/entt.hpp>

#include <world/entity.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {

    // CONSTANTS =======================================================================================================

    // Codec IDs stored in region_asset::entity_codec. Keep stable.
    inline constexpr u32                        CODEC_NONE = 0;

    inline constexpr u32                        CODEC_ENTT_V1 = 1;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Serialization codec for entity data. Each world implementation registers the components it knows how to (de)serialize.
    // The asset handler only sees bytes; the plugin chooses the format.
    class entity_codec {
    public:

        // Called during save for each entity, so the codec can write the player-visible GLT::world::entity_id rather than the raw entt::entity handle.
        using resolve_fn  = std::function<GLT::world::entity_id(entt::entity)>;

        // Called during load for each saved entity. `preferred` is the GLT::world::entity_id that was written. The plugin returns the actual
        // (GLT::world::entity_id, entt::entity) it allocated - often identical.
        using allocate_fn = std::function<std::pair<GLT::world::entity_id, entt::entity>(GLT::world::entity_id)>;

        // Register a component type. Call once per type at plugin init. T must be trivially copyable - use a helper struct for richer types.
        template<typename T>
        void register_component(std::string_view name);

        // Register a component with a non-trivial payload (std::string, std::vector, ...). The caller provides the byte layout; keep it
        // stable once shipped.
        template<typename T>
        void register_custom_component(std::string_view name, 
            std::function<void(const entt::registry&, entt::entity, std::vector<std::byte>&)> save_fn,
            std::function<void(entt::registry&, entt::entity, std::span<const std::byte>)> load_fn);

        // Serialize a subset of entities.
        void save(const entt::registry& reg, std::span<const entt::entity> entities, const resolve_fn& resolve, 
            std::vector<std::byte>& out) const;

        // Deserialize into `reg`. Returns the newly-created entity_ids in the order they appeared in the blob.
        [[nodiscard]] std::vector<GLT::world::entity_id> load(entt::registry& reg, std::span<const std::byte> data, 
            const allocate_fn& allocate) const;

        [[nodiscard]] bool empty() const noexcept { return m_components.empty(); }

    private:

        struct entry {

            u64                                                                                 type_hash;
            std::string                                                                         name;

            std::function<bool(const entt::registry&, entt::entity)>                            has;
            std::function<void(const entt::registry&, entt::entity, std::vector<std::byte>&)>   save;
            std::function<void(entt::registry&, entt::entity, std::span<const std::byte>)>      load;
        };

        [[nodiscard]] const entry* find(u64 hash) const noexcept;

        std::vector<entry>                                                                      m_components;
    };


    // Stable hash of a component name. FNV-1a 64. Change at your peril: these values end up in .glt_region files.
    [[nodiscard]] constexpr u64 component_name_hash(std::string_view name) noexcept {

        u64 h = 0xcbf29ce484222325ULL;
        for (char c : name) {
            h ^= static_cast<u8>(c);
            h *= 0x100000001b3ULL;
        }
        return h;
    }

}

#include "entity_codec.inl"
