
#pragma once

#include "action.h"
#include "mapping.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::input_action_mapper {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // Register a new action 
    UUID register_action(const action& def);


    // unregister an action
    void register_action(UUID& id);


    // Retrieve an action by name
    weak_ref<action> get_action(const UUID id);


    // Clear all registered actions (useful for unload)
    void clear();

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
