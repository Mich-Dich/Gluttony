
#pragma once

#include "physics/type.h"
#include "world/entity.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    enum class contact_phase : u8 {

        phase_begin = 0,
        phase_persist,
        phase_end,
    };


    struct contact_event {

        body_handle                         a{ INVALID_BODY };
        body_handle                         b{ INVALID_BODY };
        world::entity_id                    entity_a{ world::INVALID_ENTITY };
        world::entity_id                    entity_b{ world::INVALID_ENTITY };
        glm::vec3                           point{ 0.f };
        glm::vec3                           normal{ 0.f };
        f32                                 penetration{ 0.f };
        contact_phase                       phase{ contact_phase::phase_begin };
        u8                                  _pad[3]{};
    };
    static_assert(std::is_trivially_copyable_v<contact_event>);
    static_assert(sizeof(contact_event) == 64);

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
