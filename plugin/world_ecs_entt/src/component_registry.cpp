
#include "util/pch.h"
#include "component_registry.h"

#include "world.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    component_registry::component_registry(ecs_world_plugin& plugin) noexcept
        : m_plugin(plugin) { }

    // CLASS PUBLIC ====================================================================================================

    std::span<const GLT::world::component_descriptor> component_registry::all() const noexcept { return m_descriptors; }


    const GLT::world::component_descriptor* component_registry::find(u64 hash) const noexcept {

        for (const auto& d : m_descriptors)
            if (d.hash == hash)
                return &d;
        return nullptr;
    }


    std::vector<const GLT::world::component_descriptor*> component_registry::on(entity_id id) const {

        std::vector<const GLT::world::component_descriptor*> out;
        out.reserve(m_descriptors.size());
        for (const auto& d : m_descriptors)
            if (d.has && d.has(id))
                out.push_back(&d);
        return out;
    }


    bool component_registry::add(entity_id id, u64 hash) {

        const auto* d = find(hash);
        if (!d || !d->add || !m_plugin.alive(id))
            return false;
        if (d->has(id))
            return false;                          // already present
        d->add(id);
        return true;
    }


    bool component_registry::remove(entity_id id, u64 hash) {

        const auto* d = find(hash);
        if (!d || !d->remove || !m_plugin.alive(id))
            return false;
        if (d->can_remove && !d->can_remove(id))
            return false;
        d->remove(id);
        return true;
    }


    void component_registry::copy(entity_id from, entity_id to) {

        for (const auto* d : on(from)) {
            if (d->add && !d->has(to))
                d->add(to);
        }
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
