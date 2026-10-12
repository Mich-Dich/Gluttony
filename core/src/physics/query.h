
#pragma once

#include "physics/type.h"
#include "world/entity.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct query_filter {

        u32                             layer_mask{ 0xFFFF'FFFF };          // bit AND against body's collide_mask
        body_handle                     ignore{ INVALID_BODY };             // typically the querying body
        bool                            hit_sensors{ false };
    };


    struct ray_cast_hit {

        bool                            hit{ false };
        f32                             distance{ 0.f };
        glm::vec3                       position{ 0.f };
        glm::vec3                       normal{ 0.f };
        body_handle                     body{ INVALID_BODY };
        world::entity_id                entity{ world::INVALID_ENTITY };
        u64                             user_tag{ 0 };
        f32                             _pad{ 0.f };
    };
    static_assert(std::is_trivially_copyable_v<ray_cast_hit>);


    struct overlap_hit {

        body_handle                     body{ INVALID_BODY };
        world::entity_id                entity{ world::INVALID_ENTITY };
        u64                             user_tag{ 0 };
    };
    static_assert(std::is_trivially_copyable_v<overlap_hit>);

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
