
#pragma once

#include <entt/entt.hpp>
#include <world/component.h>
#include <world/entity.h>

#include "ecs_world.h"
#include "multi_edit.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Backs one component type for one ecs_world. Every I/O goes through the world's entt registry and entity mapping,
    // so storage lookup from the core (i_world::storage_for) is consistent with the fast paths inside ecs_world.
    template<typename T>
    class component_storage final : public GLT::world::component::i_storage {
    public:

        explicit component_storage(ecs_world& world) noexcept : m_world(world) {}


        [[nodiscard]] bool has(entity_id id) const override {

            const auto e = m_world.entt_of(id);
            return e != entt::null && m_world.registry().all_of<T>(e);
        }


        void add(entity_id id) override {

            const auto e = m_world.entt_of(id);
            if (e != entt::null)
                m_world.registry().emplace_or_replace<T>(e);
        }


        void remove(entity_id id) override {

            const auto e = m_world.entt_of(id);
            if (e != entt::null)
                m_world.registry().remove<T>(e);
        }


        [[nodiscard]] void* get(entity_id id) override {

            const auto e = m_world.entt_of(id);
            return e != entt::null ? m_world.registry().try_get<T>(e) : nullptr;
        }


        [[nodiscard]] const void* get(entity_id id) const override {

            const auto e = m_world.entt_of(id);
            return e != entt::null ? m_world.registry().try_get<T>(e) : nullptr;
        }


        void copy(entity_id from, entity_id to) override {

            const auto fe = m_world.entt_of(from);
            const auto te = m_world.entt_of(to);
            if (fe == entt::null || te == entt::null)
                return;
            if (const auto* c = m_world.registry().try_get<T>(fe))
                m_world.registry().emplace_or_replace<T>(te, *c);
        }


        [[nodiscard]] std::vector<entity_id> entities() const override {

            std::vector<entity_id> out;
            for (const auto e : m_world.registry().view<const T>()) {
                if (const entity_id id = m_world.id_of(e); id.is_valid())
                    out.push_back(id);
            }
            return out;
        }


        // Persistence is handled by entity_codec (see register.h). These methods exist to satisfy the interface for
        // worlds where no codec is wired; a future backend without a codec can fill them in here.
        void save(entity_id, std::vector<std::byte>&) const override {}

        void load(entity_id, std::span<const std::byte>) override {}


        // ---- editing several entities at once (see component::i_storage, multi_edit.h) ----

        [[nodiscard]] std::shared_ptr<void> capture(entity_id id) const override {

            if constexpr (multi_edit<T>::supported && std::is_copy_constructible_v<T>) {
                const auto e = m_world.entt_of(id);
                if (const T* value = e != entt::null ? m_world.registry().template try_get<T>(e) : nullptr)
                    return std::make_shared<T>(*value);
            }
            return {};
        }


        bool apply_changes(entity_id source, const void* before, entity_id target) override {

            if constexpr (multi_edit<T>::supported && std::is_copy_constructible_v<T>) {
                const auto se = m_world.entt_of(source);
                const auto te = m_world.entt_of(target);
                if (!before || se == entt::null || te == entt::null || se == te)
                    return false;
                const T* now = m_world.registry().template try_get<T>(se);
                T* other = m_world.registry().template try_get<T>(te);
                if (!now || !other)
                    return false;
                if (!multi_edit<T>::apply(m_world, target, *static_cast<const T*>(before), *now, *other))
                    return false;
                m_world.mark_changed(target);                   // written through the pointer: the render cache has to hear of it
                return true;
            }
            return false;
        }

    private:

        ecs_world&                                  m_world;

    };

}
