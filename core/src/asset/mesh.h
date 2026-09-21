
#pragma once

#include "asset/header.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::mesh {

    // CONSTANTS =======================================================================================================

    // Chunk IDs (any u32; keep them stable once shipped).
    inline constexpr chunk_id CHUNK_VERTICES     = 0x0001;

    inline constexpr chunk_id CHUNK_INDICES      = 0x0002;

    inline constexpr chunk_id CHUNK_SUBMESHES    = 0x0003;   // submesh + material index ranges

    inline constexpr chunk_id CHUNK_BOUNDS       = 0x0004;   // glm::vec3 min / max

    inline constexpr chunk_id CHUNK_SKELETON     = 0x0005;   // skeletal_mesh only

    inline constexpr chunk_id CHUNK_ANIMATIONS   = 0x0006;   // skeletal_mesh only

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Interleaved vertex. Keep this POD and 16-byte aligned for GPU upload.
    struct vertex {
        glm::vec3   position;           // 12
        glm::vec3   normal;             // 12
        glm::vec4   tangent;            // 16, xyz = tangent, w = bitangent sign
        glm::vec2   uv0;                //  8
        glm::vec2   _pad{};             //  8  → total 56 bytes, 16-aligned
    };
    static_assert(sizeof(vertex) == 56);
    static_assert(alignof(vertex) == 4);


    struct submesh {

        u32         first_index;
        u32         index_count;
        u32         material_slot;      // index into the (future) material table
    };
    static_assert(sizeof(submesh) == 12);


    struct bounds {

        glm::vec3   min;
        f32         _pad0;
        glm::vec3   max;
        f32         _pad1;
    };
    static_assert(sizeof(bounds) == 32);

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
