
#pragma once

#include "world/i_world.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::component_registry {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    template<typename T>
    [[nodiscard]] T* get(i_world& world, entity_id id) {

        if (g_type_hash<T> == 0)
            return nullptr;

        auto* s = world.storage_for(g_type_hash<T>);
        return s ? static_cast<T*>(s->get(id)) : nullptr;
    }


    template<typename T>
    [[nodiscard]] std::vector<std::pair<entity_id, T*>> entities_with(i_world& world) {

        std::vector<std::pair<entity_id, T*>> out;
        if (g_type_hash<T> == 0)
            return out;

        auto* s = world.storage_for(g_type_hash<T>);
        if (!s)
            return out;

        const auto ids = s->entities();
        out.reserve(ids.size());
        for (entity_id id : ids)
            if (auto* p = static_cast<T*>(s->get(id)))
                out.emplace_back(id, p);
        return out;
    }

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
