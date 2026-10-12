
#pragma once

#include "physics/type.h"
#include "world/entity.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    enum class contact_phase : u8 {

        begin = 0,
        persist,
        end
    };


    struct contact_event {

        body_handle                         a{ INVALID_BODY };
        body_handle                         b{ INVALID_BODY };
        world::entity_id                    entity_a{ world::INVALID_ENTITY };
        world::entity_id                    entity_b{ world::INVALID_ENTITY };
        glm::vec3                           point{ 0.f };           // world space (begin only)
        glm::vec3                           normal{ 0.f };          // from a -> b (begin only)
        f32                                 penetration{ 0.f };     // begin only
        contact_phase                       phase{ contact_phase::begin };
        u8                                  _pad[3]{};
    };
    static_assert(std::is_trivially_copyable_v<contact_event>);


    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

#pragma once
