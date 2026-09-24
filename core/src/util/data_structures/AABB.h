
#pragma once

#include <glm/glm.hpp>
#include <glm/vec3.hpp>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Non-uniform AABB
    struct AABB {

        glm::vec3                                       min{ 0.f };
        f32                                             _pad0{};
        glm::vec3                                       max{ 0.f };
        f32                                             _pad1{};


        [[nodiscard]] bool contains(const glm::vec3& p) const noexcept {

            return p.x >= min.x && p.x <= max.x
                && p.y >= min.y && p.y <= max.y
                && p.z >= min.z && p.z <= max.z;
        }

        [[nodiscard]] bool overlaps(const AABB& o) const noexcept {

            return min.x <= o.max.x && max.x >= o.min.x
                && min.y <= o.max.y && max.y >= o.min.y
                && min.z <= o.max.z && max.z >= o.min.z;
        }

        [[nodiscard]] glm::vec3 center() const noexcept { return (min + max) * 0.5f; }

        [[nodiscard]] glm::vec3 extents() const noexcept { return  max - min; }
    };
    static_assert(sizeof(AABB) == 32);
    static_assert(std::is_trivially_copyable_v<AABB>);

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
