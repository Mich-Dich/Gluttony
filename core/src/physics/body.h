
#pragma once

#include "physics/type.h"
#include "physics/material.h"
#include "world/entity.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct body_desc {

        motion_type                 motion{ motion_type::static_body };
        shape_handle                shape{ INVALID_SHAPE };

        glm::vec3                   position{ 0.f };
        glm::quat                   rotation{ 1.f, 0.f, 0.f, 0.f };

        glm::vec3                   linear_velocity{ 0.f };
        glm::vec3                   angular_velocity{ 0.f };

        collision_profile           filter{};

        f32                         mass_override{ -1.f };      // <0 = derive from density
        f32                         linear_damping{ 0.05f };
        f32                         angular_damping{ 0.05f };
        f32                         gravity_factor{ 1.f };
        f32                         sleep_threshold{ -1.f };    // <0 = plugin default

        bool                        allow_sleep{ true };
        bool                        is_sensor{ false };         // trigger volume; no response
        bool                        continuous{ false };        // CCD; opt-in, expensive

        world::entity_id            entity{ world::INVALID_ENTITY };
        u64                         user_tag{ 0 };              // opaque, engine-agnostic
    };

    // ---- read-only snapshot -----------------------------------------------------

    // POD. What gameplay and the render sync actually consume
    struct body_state {

        glm::vec3                   position{ 0.f };
        glm::quat                   rotation{ 1.f, 0.f, 0.f, 0.f };
        glm::vec3                   linear_velocity{ 0.f };
        glm::vec3                   angular_velocity{ 0.f };
        f32                         mass{ 0.f };
        bool                        is_active{ false };
        u8                          _pad[3]{};
    };
    static_assert(std::is_trivially_copyable_v<body_state>);

    // ---- step stats ------------------------------------------------------------

    struct physics_stats {

        u32                         body_count{ 0 };
        u32                         active_body_count{ 0 };
        u32                         shape_count{ 0 };
        u32                         contact_pair_count{ 0 };
        u32                         substeps_last_frame{ 0 };
        f32                         step_ms_last{ 0.f };
        f32                         step_ms_avg{ 0.f };
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
