
#pragma once

#include <string>
#include <string_view>

#include "world/entity.h"
#include "components.h"

// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {

    class ecs_world_plugin;

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Fluent editor/spawn API. The target entity is created (or looked up) by ecs_world_plugin::make_entity() / edit();
    // every method returns *this so calls can be chained. The builder is a thin wrapper — no internal state beyond the plugin + entity_id.
    //
    // Lifetime: a builder is a transient handle. Do NOT store it. Once the underlying entity is despawned, any live builder for it becomes UB.
    class entity_builder {
    public:

        entity_builder(ecs_world_plugin& plugin, entity_id id) noexcept;

        // ---- generic component ops -------------------------------------------------------------------

        template<typename T, typename... Args>
        entity_builder& add(Args&&... args);


        template<typename T>
        entity_builder& add_or_replace(T&& component);


        template<typename T>
        entity_builder& remove();


        template<typename T>
        [[nodiscard]] bool has() const;


        template<typename T>
        [[nodiscard]] T& get();


        template<typename T>
        [[nodiscard]] const T& get() const;

        // ---- convenience (thin wrappers over the above) ----------------------------------------------

        entity_builder& named(std::string name);


        entity_builder& set_transform(const transform& t);


        entity_builder& set_mesh(GLT::asset::handle mesh, bool visible = true);


        entity_builder& set_audio(GLT::asset::handle clip, const GLT::asset::audio::source_config& cfg = {}, bool autoplay = false);

        // ---- hierarchy -------------------------------------------------------------------------------

        // Reparent this entity. INVALID_ENTITY detaches it (equivalent to detach()).
        // Cycle-safe: parenting an ancestor under its own descendant is a no-op.
        entity_builder& parent(entity_id parent);


        entity_builder& detach();


        // Adopt an existing entity as a child of this one.
        entity_builder& adopt(entity_id child);


        // Release a child without touching the child's own subtree.
        entity_builder& disown(entity_id child);

        // ---- terminal --------------------------------------------------------------------------------

        [[nodiscard]] entity_id id() const noexcept { return m_id; }
        operator entity_id() const noexcept { return m_id; }

    private:

        ecs_world_plugin*   m_plugin;
        entity_id           m_id;
    };

}

// NOTE: entity_builder.inl is included at the bottom of ecs_world_plugin.h, after the plugin class has been fully defined.
