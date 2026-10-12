
#pragma once

#include "util/data_structures/UUID.h"
#include "world/entity.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics {

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // index+generation, same shape as world::entity_id. Generation makes a handle to a destroyed body fail [is_alive()]
    // instead of silently aliasing a recycled slot

    struct body_handle {

        u32                                 index{ 0 };
        u32                                 generation{ 0 };

        constexpr bool operator==(const body_handle&) const noexcept = default;
        constexpr auto operator<=>(const body_handle&) const noexcept = default;
        [[nodiscard]] constexpr bool is_valid() const noexcept { return generation != 0; }
    };


    struct shape_handle {

        u32                                 index{ 0 };
        u32                                 generation{ 0 };

        constexpr bool operator==(const shape_handle&) const noexcept = default;
        constexpr auto operator<=>(const shape_handle&) const noexcept = default;
        [[nodiscard]] constexpr bool is_valid() const noexcept { return generation != 0; }
    };


    struct joint_handle {

        u32                                 index{ 0 };
        u32                                 generation{ 0 };

        constexpr bool operator==(const joint_handle&) const noexcept = default;
        constexpr auto operator<=>(const joint_handle&) const noexcept = default;
        [[nodiscard]] constexpr bool is_valid() const noexcept { return generation != 0; }
    };


    // Contact/trigger filtering. Two-axis filtering (object_layer + broadphase_layer)
    // mirrors PhysX, but a single mask is what the plugin actually needs.
    struct collision_profile {

        u32     object_layer{ 0x0001 };      // "who am I"
        u32     collide_mask{ 0xFFFF'FFFF }; // "what I collide with" (bit AND of layers)
    };



    enum class motion_type : u8 {

        static_body  = 0,
        dynamic_body,
        kinematic_body,
    };


    enum class physics_error : u8 {

        none = 0,
        invalid_argument,
        stale_handle,
        out_of_memory,
        unsupported_feature,    // e.g. triangle mesh on a dynamic body
        body_locked,            // mutation requested while step() is running
        io_failure,             // state serialization only
        internal_error,
    };


    // Event filtering — set on a per-body basis.
    enum class contact_event_mask : u8 {

        none      = 0,
        begin     = BIT(0),
        persist   = BIT(1),       // expensive; opt-in only
        end       = BIT(2),
        all       = BIT(0) | BIT(1) | BIT(2),
    };
    // TODO: declare bitwise operators alongside

    // CONSTANTS =======================================================================================================

    inline constexpr body_handle                            INVALID_BODY{ 0, 0 };

    inline constexpr shape_handle                           INVALID_SHAPE{ 0, 0 };

    inline constexpr joint_handle                           INVALID_JOINT{ 0, 0 };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
