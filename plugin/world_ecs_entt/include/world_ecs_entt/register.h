
#pragma once

#include <world/component_registry.h>
#include <world/i_world_plugin.h>

#include "component_storage.h"
#include "ecs_world_plugin.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::component_registry {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    template<typename T>
    component::type_descriptor make_descriptor(std::string_view name, std::string_view category,
        std::function<void(GLT::world::i_world&, entity_id)> draw, std::function<std::string(GLT::world::i_world&, entity_id)> summary,
        std::vector<u64> required);

    template<typename T>
    void add(std::string_view name, std::string_view category, std::function<void(GLT::world::i_world&, entity_id)> draw,
        std::function<std::string(GLT::world::i_world&, entity_id)> summary, std::vector<u64> required);

    template<typename T>
    bool remove_type();

    // CLASS DECLARATION ===============================================================================================

}

#include "register.inl"
