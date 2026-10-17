
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics::jolt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Result of compiling a shape_desc. `motion_mask` is the cached, recursive
    // motion-compatibility mask described in §Shape / motion compatibility.
    // `create_body()` checks it in O(1); it never re-walks the tree.
    struct built_shape {
        JPH::ShapeRefC              shape;
        u8                          motion_mask{ 0 };
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // Consumes desc; copies any geometry reachable from spans. Safe for the caller to free the underlying buffers as soon as this returns
    [[nodiscard]] std::expected<built_shape, physics_error> build_shape(const shape_desc& desc);

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
