                                                                    
#pragma once

#include "util/data_structures/UUID.h"
#include "world/entity.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics {

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    enum class motion_type : u8 {

        static_body = 0,
        dynamic_body,
        kinematic_body,
    };


    enum class physics_error : u8 {

        none = 0,
        invalid_argument,
        stale_handle,
        out_of_memory,
        unsupported_feature,
        body_locked,
        io_failure,
        internal_error,
    };


    enum class contact_event_mask : u8 {

        none    = 0,
        begin   = BIT(0),
        persist = BIT(1),
        end     = BIT(2),
        all     = BIT(0) | BIT(1) | BIT(2),
    };


    struct body_handle {

        u32                                     index{0};
        u32                                     generation{0};

        constexpr bool operator==(const body_handle&) const noexcept = default;
        constexpr auto operator<=>(const body_handle&) const noexcept = default;
        [[nodiscard]] constexpr bool is_valid() const noexcept { return generation != 0; }
    };
    static_assert(std::is_trivially_copyable_v<body_handle>);
    static_assert(sizeof(body_handle) == 8);


    struct shape_handle {

        u32                                     index{0};
        u32                                     generation{0};

        constexpr bool operator==(const shape_handle&) const noexcept = default;
        constexpr auto operator<=>(const shape_handle&) const noexcept = default;
        [[nodiscard]] constexpr bool is_valid() const noexcept { return generation != 0; }
    };
    static_assert(std::is_trivially_copyable_v<shape_handle>);
    static_assert(sizeof(shape_handle) == 8);


    struct joint_handle {

        u32                                     index{0};
        u32                                     generation{0};

        constexpr bool operator==(const joint_handle&) const noexcept = default;
        constexpr auto operator<=>(const joint_handle&) const noexcept = default;
        [[nodiscard]] constexpr bool is_valid() const noexcept { return generation != 0; }
    };
    static_assert(std::is_trivially_copyable_v<joint_handle>);
    static_assert(sizeof(joint_handle) == 8);


    struct collision_profile {

        u32                                     object_layer{0x0001};
        u32                                     collide_mask{0xFFFF'FFFF};
    };
    static_assert(std::is_trivially_copyable_v<collision_profile>);
    static_assert(sizeof(collision_profile) == 8);


    constexpr contact_event_mask operator|(contact_event_mask a, contact_event_mask b) noexcept {
        return static_cast<contact_event_mask>(static_cast<u8>(a) | static_cast<u8>(b));
    }


    constexpr contact_event_mask operator&(contact_event_mask a, contact_event_mask b) noexcept {
        return static_cast<contact_event_mask>(static_cast<u8>(a) & static_cast<u8>(b));
    }


    constexpr contact_event_mask& operator|=(contact_event_mask& a, contact_event_mask b) noexcept {
        a = a | b;
        return a;
    }

    // CONSTANTS =======================================================================================================

    inline constexpr body_handle                INVALID_BODY{0, 0};

    inline constexpr shape_handle               INVALID_SHAPE{0, 0};

    inline constexpr joint_handle               INVALID_JOINT{0, 0};

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
