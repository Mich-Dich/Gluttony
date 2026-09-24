
#pragma once

#include "util/data_structures/AABB.h"
#include "asset/header.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::mesh {

    // CONSTANTS =======================================================================================================

    // Chunk IDs (any u32; keep them stable once shipped).
    inline constexpr GLT::asset::chunk_id               CHUNK_VERTICES = 0x0001;

    inline constexpr GLT::asset::chunk_id               CHUNK_INDICES = 0x0002;

    inline constexpr GLT::asset::chunk_id               CHUNK_SUBMESHES = 0x0003;   // submesh + material index ranges

    inline constexpr GLT::asset::chunk_id               CHUNK_BOUNDS = 0x0004;   // glm::vec3 min / max

    inline constexpr GLT::asset::chunk_id               CHUNK_SKELETON = 0x0005;   // skeletal_mesh only

    inline constexpr GLT::asset::chunk_id               CHUNK_ANIMATIONS = 0x0006;   // skeletal_mesh only

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

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // The runtime representation of a loaded mesh asset. Handlers own this
    // type; the registry only ever sees it as `i_runtime_asset*`.
    //
    // IMPORTANT: this struct OWNS the decoded geometry. The chunk_reader
    // hands out spans into a buffer that dies when the registry's load
    // function returns, so every handler must copy what it wants to keep.
    class mesh_asset final : public GLT::asset::i_runtime_asset {
    public:

        GLT::asset::type                                asset_type{ GLT::asset::core_types::static_mesh };
        std::vector<GLT::asset::mesh::vertex>           vertices;
        std::vector<u32>                                indices;
        std::vector<GLT::asset::mesh::submesh>          submeshes;
        GLT::AABB                                       bounds{};

        // Positional. material_handles[i] corresponds to submesh.material_slot == i.
        // Entries may be INVALID_HANDLE when a material reference couldn't be
        // resolved - the renderer is expected to substitute a fallback.
        std::vector<GLT::asset::handle>                 material_handles;


        FORCE_INLINE_R GLT::asset::type type() const noexcept override { return asset_type; }


        FORCE_INLINE_R u64 memory_usage() const noexcept override {

            return sizeof(*this)
                + vertices.capacity()  * sizeof(GLT::asset::mesh::vertex)
                + indices.capacity()   * sizeof(u32)
                + submeshes.capacity() * sizeof(GLT::asset::mesh::submesh)
                + material_handles.capacity() * sizeof(GLT::asset::handle);
        }
        
    };

}
