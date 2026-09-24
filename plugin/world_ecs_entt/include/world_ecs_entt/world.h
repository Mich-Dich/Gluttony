
#pragma once

#include <plugin_system/i_world_plugin.h>

#include "handler.h"
#include "components.h"
#include "entity_codec.h"
#include "entity_builder.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Default world implementation shipped with the engine. Backed by EnTT.
    //
    // Region entity data lives in region_asset::entity_data as an opaque blob
    // produced by entity_codec (CODEC_ENTT_V1).
    //
    // Owns:
    //   - the ECS registry
    //   - the slot table that maps entity_id <-> entt::entity
    //   - the region streaming pass
    //
    // Does NOT own:
    //   - the region/world assets themselves (registry does)
    //   - asset refcounting (see notes at the bottom of the .cpp)
    class ecs_world_plugin final : public i_world_plugin {
    public:

        ecs_world_plugin();
        ~ecs_world_plugin() override;

        // --- i_plugin -------------------------------------------------------------------------------------------------

        void on_load() override;


        void on_unload() override;

        // --- world lifecycle ------------------------------------------------------------------------------------------

        [[nodiscard]] std::expected<void, GLT::asset::load_error> load_world(GLT::asset::handle world) override;


        void unload_world() noexcept override;


        [[nodiscard]] GLT::asset::handle world_handle() const noexcept override;


        [[nodiscard]] std::expected<void, GLT::asset::load_error> save_world();

        // --- entity lifecycle -----------------------------------------------------------------------------------------

        [[nodiscard]] entity_id spawn() override;


        void despawn(entity_id id) noexcept override;


        [[nodiscard]] bool alive(entity_id id) const noexcept override;

        // --- region queries -------------------------------------------------------------------------------------------

        [[nodiscard]] std::span<const GLT::asset::region::region> regions() const noexcept override;


        [[nodiscard]] const GLT::asset::region::region* region_at(const glm::vec3& point) const noexcept override;


        [[nodiscard]] bool is_region_active(GLT::UUID id) const noexcept override;


        void set_region_active(GLT::UUID id, bool active) override;

        // --- streaming ------------------------------------------------------------------------------------------------

        void set_streaming_anchor(const glm::vec3& position) override;


        [[nodiscard]] glm::vec3 get_streaming_anchor() const noexcept override;


        void set_streaming_radius(const f32 radius) override;


        [[nodiscard]] f32 get_streaming_radius() const noexcept override;

        // --- update ---------------------------------------------------------------------------------------------------

        void update(f32 delta_time) override;

        // --- extension API for plugins that couple to this concrete type ----------------------------------------------

        [[nodiscard]] entt::registry& registry() noexcept { return m_registry; }


        [[nodiscard]] const entt::registry& registry() const noexcept { return m_registry; }


        template<typename T>
        void register_component(std::string_view name) { m_codec.register_component<T>(name); }

        // builder API -------------------------------------------------------------------------------------------------

        // Create a fresh entity, optionally named. The entity exists immediately; the builder is just a handle for chaining.
        [[nodiscard]] entity_builder make_entity(std::string_view name = "");


        // Open an existing entity for editing. The returned builder is invalid if `id` is not alive.
        [[nodiscard]] entity_builder edit(entity_id id);

        // hierarchy queries -------------------------------------------------------------------------------------------

        [[nodiscard]] entity_id parent_of(entity_id id) const noexcept;


        // Returns a copy — the underlying vector lives in the ECS and may be invalidated by any subsequent structural change.
        [[nodiscard]] std::vector<entity_id> children_of(entity_id id) const;


        // hierarchy mutation ------------------------------------------------------------------------------------------

        // The only place the parent/child invariant is enforced. Use this instead of touching `hierarchy` directly.
        //
        //   - self-parent: no-op
        //   - cycle-creating: no-op
        //   - parent == INVALID_ENTITY: detach from current parent
        //   - parent not alive: no-op
        void set_parent(entity_id child, entity_id parent);

        // ECS internals (used by entity_builder) -----------------------------------------------------------------------

        [[nodiscard]] entt::registry& registry() noexcept;


        [[nodiscard]] const entt::registry& registry() const noexcept;


        [[nodiscard]] entt::entity entt_of(entity_id id) const noexcept;


        [[nodiscard]] entity_id id_of(entt::entity e) const noexcept;

    private:

        struct slot {

            entt::entity                                    handle{ entt::null };
            u32                                             generation{ 0 };
        };

        struct region_state {

            std::vector<entity_id>                          entities;      // entities spawned by this region
        };

    
        entity_id entity_id_for(entt::entity e) const noexcept;

        // slot management ----------------------------------------------------------------------------------------------

        [[nodiscard]] entity_id alloc_slot();


        void release_slot(entity_id id);


        [[nodiscard]] std::pair<entity_id, entt::entity> allocate_for_load(entity_id preferred);

        // streaming ----------------------------------------------------------------------------------------------------

        void stream_pass();


        [[nodiscard]] bool should_be_active(const GLT::asset::region::region& r) const noexcept;


        void activate(GLT::asset::region::region& r);


        void deactivate(GLT::asset::region::region& r);


        [[nodiscard]] GLT::asset::region::region* find_region(GLT::UUID id) noexcept;


        [[nodiscard]] hierarchy* hierarchy_of(entity_id id) noexcept;


        [[nodiscard]] const hierarchy* hierarchy_of(entity_id id) const noexcept;


        entt::registry                                      m_registry;
        entity_codec                                        m_codec;
        world_asset_handler                                 m_asset_handler;
        std::vector<GLT::asset::region::region>             m_regions{};
        std::unordered_map<GLT::UUID, region_state>         m_region_states{};
        std::vector<slot>                                   m_slots{};
        std::vector<u32>                                    m_free{};
        GLT::asset::handle                                  m_world{};
        glm::vec3                                           m_streaming_anchor{ 0.f };
        f32                                                 m_streaming_radius{ 0.f };
    };

}
