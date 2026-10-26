#pragma once

#include <string>
#include <string_view>

#include "world/entity.h"
#include "components.h"

namespace GLT::world::world_ecs_entt {
    class ecs_world;
}

namespace GLT::world::world_ecs_entt {

    // Fluent editor / spawn API. `m_world` points at the instance that owns the
    // target entity — never a singleton. Lifetime: transient handle; do not store.
    class entity_builder {
    public:

        entity_builder(ecs_world& world, entity_id id) noexcept;

        template<typename T, typename... Args> entity_builder& add(Args&&... args);
        template<typename T> entity_builder& add_or_replace(T&& component);
        template<typename T> entity_builder& remove();
        template<typename T> [[nodiscard]] bool has() const;
        template<typename T> [[nodiscard]] T& get();
        template<typename T> [[nodiscard]] const T& get() const;

        entity_builder& named(std::string name);
        entity_builder& set_transform(const component::transform& t);
        entity_builder& set_mesh(GLT::asset::handle mesh, bool visible = true);
        entity_builder& set_audio(GLT::asset::handle clip, const GLT::asset::audio::source_config& cfg = {},
                                  bool autoplay = false);
        entity_builder& set_light(GLT::world::light_type type = GLT::world::light_type::point,
                                  const glm::vec3& color = { 1.f, 1.f, 1.f }, f32 intensity = 1.f);

        entity_builder& parent(entity_id parent);
        entity_builder& detach();
        entity_builder& adopt(entity_id child);
        entity_builder& disown(entity_id child);

        [[nodiscard]] entity_id id() const noexcept { return m_id; }
        operator entity_id() const noexcept { return m_id; }

    private:

        ecs_world*  m_world;
        entity_id   m_id;
    };
}

// [entity_builder.inl] is included at the bottom of [ecs_world.h].
