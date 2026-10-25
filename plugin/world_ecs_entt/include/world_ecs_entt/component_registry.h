#pragma once

#include <world/i_world_inspector.h>
#include <world/entity.h>

namespace GLT::world::world_ecs_entt {
    class ecs_world;
}

namespace GLT::world::world_ecs_entt {

    // Per-world descriptor table. Each ecs_world owns exactly one of these, so
    // the lambdas registered here close over the correct world (via `this`).
    //
    // User-facing draw/summary lambdas take `ecs_world&` explicitly; the template
    // wraps them with a `[this]` capture so the descriptor field itself stays
    // `std::function<void(entity_id)>` (no i_world_inspector.h change needed).
    class component_registry {
    public:

        explicit component_registry(ecs_world& world) noexcept;

        template<typename T>
        void register_component(std::string_view name, std::string_view category, std::function<void(ecs_world&, entity_id)> draw,
            std::function<std::string(ecs_world&, entity_id)> summary = {}, std::vector<u64> required = {});

        [[nodiscard]] std::span<const GLT::world::component_descriptor> all() const noexcept;
        [[nodiscard]] const GLT::world::component_descriptor* find(u64 hash) const noexcept;
        [[nodiscard]] std::vector<const GLT::world::component_descriptor*> on(entity_id id) const;

        [[nodiscard]] bool add(entity_id id, u64 hash);
        [[nodiscard]] bool remove(entity_id id, u64 hash);
        void copy(entity_id from, entity_id to);

    private:

        ecs_world&                                              m_world;
        std::vector<GLT::world::component_descriptor>           m_descriptors;
    };
}

// [component_registry.inl] is included at the bottom of [ecs_world.h].
