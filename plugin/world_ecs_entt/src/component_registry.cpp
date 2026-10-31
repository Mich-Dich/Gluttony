
#include "util/pch.h"
#include "component_registry.h"
#include "ecs_world.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    void ensure_required(const std::vector<GLT::world::component::descriptor>& descriptors, const std::vector<u64>& required, entity_id id);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    void ensure_required(const std::vector<GLT::world::component::descriptor>& descriptors, const std::vector<u64>& required, entity_id id) {

        for (const u64 req_hash : required) {
            const GLT::world::component::descriptor* req = nullptr;
            for (const auto& d : descriptors)
                if (d.hash == req_hash) {
                    req = &d;
                    break;
                }

            if (!req || !req->add || !req->has)
                continue;
            if (!req->has(id))
                req->add(id);
        }
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    component_registry::component_registry(ecs_world& world) noexcept
        : m_world(world) { }

    // CLASS PUBLIC ====================================================================================================

    void component_registry::register_component(GLT::world::component::descriptor desc) {

        for (auto& existing : m_descriptors) {
            if (existing.hash == desc.hash) {
                existing = std::move(desc);
                return;
            }
        }
        m_descriptors.push_back(std::move(desc));
    }


    void component_registry::unregister_component(u64 hash) { std::erase_if(m_descriptors, [hash](const auto& d) { return d.hash == hash; }); }


    std::span<const GLT::world::component::descriptor> component_registry::all() const noexcept { return m_descriptors; }


    const GLT::world::component::descriptor* component_registry::find(u64 hash) const noexcept {

        for (const auto& d : m_descriptors)
            if (d.hash == hash)
                return &d;
        return nullptr;
    }


    std::vector<const GLT::world::component::descriptor*> component_registry::on(entity_id id) const {

        std::vector<const GLT::world::component::descriptor*> out;
        out.reserve(m_descriptors.size());
        for (const auto& d : m_descriptors)
            if (d.has && d.has(id))
                out.push_back(&d);
        return out;
    }


    bool component_registry::add(entity_id id, u64 hash) {

        const auto* d = find(hash);
        if (!d || !d->add || !m_world.alive(id))
            return false;
        if (d->has(id))
            return false;

        ensure_required(m_descriptors, d->required, id);
        d->add(id);
        return true;
    }


    bool component_registry::remove(entity_id id, u64 hash) {

        const auto* d = find(hash);
        if (!d || !d->remove || !m_world.alive(id))
            return false;
        if (d->can_remove && !d->can_remove(id))
            return false;
        d->remove(id);
        return true;
    }


    void component_registry::copy(entity_id from, entity_id to) {

        for (const auto* d : on(from)) {
            if (!d->add || d->has(to))
                continue;
            ensure_required(m_descriptors, d->required, to);
            d->add(to);
        }
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
