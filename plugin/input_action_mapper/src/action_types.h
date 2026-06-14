
#pragma once

#include <chrono>
#include <glm/vec2.hpp>
#include <util/data_structures/data_types.h>


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::input_action_mapper {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct key_info {

        key_state                                   current{key_state::release};
        key_state                                   previous{key_state::release};
        std::chrono::steady_clock::time_point       press_time;
        std::chrono::steady_clock::time_point       release_time;
    };


    struct mouse_state {

        glm::vec2                                   delta = {0.f, 0.f};
        glm::vec2                                   scroll = {0.f, 0.f};
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
