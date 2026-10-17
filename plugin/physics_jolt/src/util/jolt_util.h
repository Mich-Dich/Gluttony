
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics::jolt {

    // CONSTANTS =======================================================================================================

    inline constexpr u8                         MASK_ALL = 0b111;  // static | dynamic | kinematic

    inline constexpr u8                         MASK_STATIC_ONLY = 0b001;

    inline constexpr u8                         MASK_STATIC_KINEMATIC = 0b101;  // static + kinematic, no dynamic

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // GLM <-> Jolt type bridges

    FORCE_INLINE_R JPH::Vec3 to_jolt(const glm::vec3& v) noexcept { return JPH::Vec3(v.x, v.y, v.z); }


    FORCE_INLINE_R JPH::RVec3 to_jolt_r(const glm::vec3& v) noexcept { return JPH::RVec3(v.x, v.y, v.z); }


    FORCE_INLINE_R JPH::Quat to_jolt(const glm::quat& q) noexcept { return JPH::Quat(q.x, q.y, q.z, q.w); }


    FORCE_INLINE_R glm::vec3 from_jolt(const JPH::Vec3& v) noexcept { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }


    FORCE_INLINE_R glm::quat from_jolt(const JPH::Quat& q) noexcept { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

    #if defined(JPH_DOUBLE_PRECISION)

        inline glm::vec3 from_jolt(const JPH::RVec3& v) noexcept { 

            return glm::vec3(static_cast<f32>(v.GetX()), static_cast<f32>(v.GetY()), static_cast<f32>(v.GetZ()));
        }

    #endif

    // Bit helpers matching §Shape / motion compatibility in the contract. motion_type values: static=0, dynamic=1, kinematic=2
    constexpr u8 motion_bit(motion_type m) noexcept { return static_cast<u8>(1u << static_cast<u8>(m)); }

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
