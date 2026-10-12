
#pragma once

#include "physics/material.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics {
    struct compound;
}

namespace GLT::physics {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // primitive shapes ------------------------------------------------------------------------------------------------

    struct sphere   {
        f32                                             radius{ 0.5f };
    };


    struct box {

        glm::vec3                                       half_extents{ 0.5f };
    };


    struct capsule  {           // aligned +Y, total height = 2*(half_height + radius)

        f32                                             radius{ 0.25f };
        f32                                             half_height{ 0.5f };
    };


    struct cylinder {           // aligned +Y

        f32                                             radius{ 0.25f };
        f32                                             half_height{ 0.5f };
    };


    struct plane {              // n·x = d, n must be unit length

        glm::vec3                                       normal{ 0.f, 1.f, 0.f };
        f32                                             distance{ 0.f };
    };

    // mesh-like shapes ------------------------------------------------------------------------------------------------

    // Spans are NON-OWNING. The plugin must copy before returning; callers
    // are free to reuse the buffer immediately after create_shape().

    struct convex_hull {

        std::span<const glm::vec3>                      points;
    };


    struct triangle_mesh {

        std::span<const glm::vec3>                      vertices;
        std::span<const u32>                            indices;
        // contract: STATIC bodies only. The plugin is expected to reject this
        // with `unsupported_feature` for dynamic/kinematic bodies.
    };


    struct height_field {

        u32                                             width{ 0 };      // samples along X
        u32                                             height{ 0 };     // samples along Z
        std::span<const f32>                            samples;         // row-major, width*height
        glm::vec3                                       scale{ 1.f };    // per-axis spacing; Y = amplitude
    };

    // compound --------------------------------------------------------------------------------------------------------

    using shape_data = std::variant<sphere, box, capsule, cylinder, plane, convex_hull, triangle_mesh, height_field, 
        GLT::ref<const compound>     // recursive, ref-counted
    >;
    

    struct compound {
    
        struct child {
    
            GLT::ref<const shape_data>                  shape;
            glm::mat4                                   local{ 1.f };   // child -> parent
        };
        std::vector<child>                              children;
    };
    

    // The full descriptor handed to create_shape(). Material lives here (matches
    // Jolt/PhysX/Bullet) so a single body can carry per-shape friction/restitution
    // — useful for e.g. a character with slippery shoes and grippy gloves.
    struct shape_desc {
    
        shape_data                                      data;
        physics_material                                material{};
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
