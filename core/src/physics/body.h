
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
        u8                          _pad0[3]{};
        shape_handle                shape{ INVALID_SHAPE };

        glm::vec3                   position{ 0.f };
        glm::quat                   rotation{ 1.f, 0.f, 0.f, 0.f };

        glm::vec3                   linear_velocity{ 0.f };
        glm::vec3                   angular_velocity{ 0.f };

        collision_profile           filter{};

        f32                         mass_override{ -1.f };
        f32                         linear_damping{ 0.05f };
        f32                         angular_damping{ 0.05f };
        f32                         gravity_factor{ 1.f };
        f32                         sleep_threshold{ -1.f };

        bool                        allow_sleep{ true };
        bool                        is_sensor{ false };
        bool                        continuous{ false };

        contact_event_mask          contact_mask{ contact_event_mask::none };

        world::entity_id            entity{ world::INVALID_ENTITY };
        u64                         user_tag{ 0 };
    };
    static_assert(std::is_trivially_copyable_v<body_desc>);
    static_assert(sizeof(body_desc) == 112);


    struct body_state {

        glm::vec3                   position{ 0.f };
        glm::quat                   rotation{ 1.f, 0.f, 0.f, 0.f };
        glm::vec3                   linear_velocity{ 0.f };
        glm::vec3                   angular_velocity{ 0.f };
        f32                         mass{ 0.f };
        bool                        is_active{ false };
        bool                        valid{ false };
        u8                          _pad[2]{};
    };
    static_assert(std::is_trivially_copyable_v<body_state>);
    static_assert(sizeof(body_state) == 60);


    struct physics_stats {

        u32                         body_count{ 0 };
        u32                         active_body_count{ 0 };
        u32                         shape_count{ 0 };
        u32                         contact_pair_count{ 0 };
        u32                         substeps_last_frame{ 0 };
        u32                         dropped_contact_events{ 0 }; // cumulative since init()
        f32                         step_ms_last{ 0.f };
        f32                         step_ms_avg{ 0.f };
    };
    static_assert(std::is_trivially_copyable_v<physics_stats>);
    static_assert(sizeof(physics_stats) == 32);

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
