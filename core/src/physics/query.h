
#pragma once

#include "physics/type.h"
#include "world/entity.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct query_filter {

        u32                             layer_mask{ 0xFFFF'FFFF }; // AND against body.object_layer
        body_handle                     ignore{ INVALID_BODY };
        bool                            hit_sensors{ false };
        u8                              _pad[3]{};
    };
    static_assert(std::is_trivially_copyable_v<query_filter>);
    static_assert(sizeof(query_filter) == 16);


    struct ray_cast_hit {

        bool                            hit{ false };
        u8                              _pad0[3]{};
        f32                             distance{ 0.f };
        glm::vec3                       position{ 0.f };
        glm::vec3                       normal{ 0.f };
        body_handle                     body{ INVALID_BODY };
        world::entity_id                entity{ world::INVALID_ENTITY };
        u64                             user_tag{ 0 };
        u32                             _pad1[2]{};
    };
    static_assert(std::is_trivially_copyable_v<ray_cast_hit>);
    static_assert(sizeof(ray_cast_hit) == 64);


    struct overlap_hit {

        body_handle                     body{ INVALID_BODY };
        world::entity_id                entity{ world::INVALID_ENTITY };
        u64                             user_tag{ 0 };
    };
    static_assert(std::is_trivially_copyable_v<overlap_hit>);
    static_assert(sizeof(overlap_hit) == 24);

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
