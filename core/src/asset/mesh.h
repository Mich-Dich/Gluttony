
#pragma once

#include "util/data_structures/AABB.h"
#include "asset/type.h"
#include "asset/header.h"
#include "world/entity.h"
#include "physics/shape.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::mesh {

    // CONSTANTS =======================================================================================================

    // Chunk IDs (any u32; keep them stable once shipped)
    inline constexpr GLT::asset::chunk_id               CHUNK_VERTICES = 0x0001;

    inline constexpr GLT::asset::chunk_id               CHUNK_INDICES = 0x0002;

    inline constexpr GLT::asset::chunk_id               CHUNK_SUBMESHES = 0x0003;       // submesh + material index ranges

    inline constexpr GLT::asset::chunk_id               CHUNK_BOUNDS = 0x0004;          // glm::vec3 min / max

    inline constexpr GLT::asset::chunk_id               CHUNK_SKELETON = 0x0005;        // skeletal_mesh only

    inline constexpr GLT::asset::chunk_id               CHUNK_ANIMATIONS = 0x0006;      // skeletal_mesh only

    inline constexpr GLT::asset::chunk_id               CHUNK_COLLIDERS = 0x0007;       // mesh_collider[]

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // @brief Interleaved vertex, laid out for direct GPU upload
    //
    // Kept POD and 16-byte aligned. [_pad] exists only to round the struct up to 56 bytes so a vertex buffer of [N] of
    // these has a stable stride
    struct vertex {

        glm::vec3                                       position;           // 12
        glm::vec3                                       normal;             // 12
        glm::vec4                                       tangent;            // 16, xyz = tangent, w = bitangent sign
        glm::vec2                                       uv0;                //  8
        glm::vec2                                       _pad{};             //  8  -> total 56 bytes, 16-aligned
    };
    static_assert(sizeof(vertex) == 56);
    static_assert(alignof(vertex) == 4);


    // @brief One draw range inside the index buffer
    struct submesh {

        u32                                             first_index;
        u32                                             index_count;
        GLT::asset::handle                              material = INVALID_HANDLE;
    };
    static_assert(sizeof(submesh) == 16);


    // @brief A single renderable instance of a mesh asset
    //
    // [transform] is a full world-space 4x4
    struct instance {

        GLT::asset::handle                              mesh{ INVALID_HANDLE };
        u32                                             procedural_mesh = 0;
        glm::mat4                                       transform{ 1.0f };
        u32                                             flags{ 0 };                     // reserved (frustum-cull bits, etc.)
        std::vector<GLT::asset::handle>                 material_overrides{};
        GLT::world::entity_id                           entity{};                       // Optional back-reference
    };
    static_assert(sizeof(instance) == 112);


    // @brief Authoring-time collider shape kind
    //
    // v1 supports the simple primitives plus a "whole-mesh" triangle-mesh collider. Convex hull and height field are deliberately deferred
    enum class collider_shape : u8 {

        sphere = 0,
        box,
        capsule,
        cylinder,
        triangle_mesh,      // uses this mesh's own vertices + indices
    };


    // @brief One collider authored on a mesh asset
    //
    // Stored as an array in CHUNK_COLLIDERS. Colliders live in mesh-local space; [local_transform] places them relative to the
    // mesh origin. For triangle_mesh colliders the shape parameters are ignored - the collider consumes
    // mesh_asset::vertices / mesh_asset::indices directly
    //
    // Plain data on purpose: trivially copyable so the chunk reader/writer can blit it byte-for-byte
    struct mesh_collider {

        collider_shape              shape{ collider_shape::box };
        u8                          _pad0[3]{};
        u32                         flags{ 0 };            // reserved for user/gameplay tags

        // Shape parameters — only the field matching [shape] is meaningful (triangle_mesh uses none of them)
        physics::sphere             sphere{};
        physics::box                box{};
        physics::capsule            capsule{};
        physics::cylinder           cylinder{};

        physics::physics_material   material{};
        glm::mat4                   local_transform{ 1.0f };    // collider space -> mesh space
    };
    static_assert(std::is_trivially_copyable_v<mesh_collider>);

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Runtime representation of a loaded mesh asset
    //
    // Handlers own this type; the registry only ever sees it as [i_runtime_asset*]. [material_handles] is positional: entry [i]
    // corresponds to whichever submesh has [material_slot == i]. Entries may be INVALID_HANDLE when a material reference
    // couldn't be resolved - the renderer is expected to substitute a fallback
    //
    // IMPORTANT: this struct OWNS the decoded geometry. The chunk_reader hands out spans into a buffer that dies when
    // the registry's load function returns, so every handler must copy what it wants to keep
    class mesh_asset final : public GLT::asset::i_runtime_asset {
    public:

        GLT::asset::type                                asset_type{ GLT::asset::core_types::static_mesh };
        std::vector<GLT::asset::mesh::vertex>           vertices;
        std::vector<u32>                                indices;
        std::vector<GLT::asset::mesh::submesh>          submeshes;
        std::vector<GLT::asset::mesh::mesh_collider>    colliders;    // 0..N authored colliders
        GLT::AABB                                       bounds{};


        FORCE_INLINE_R u32 submesh_count() const noexcept { return static_cast<u32>(submeshes.size()); }


        FORCE_INLINE_R GLT::asset::handle default_material_for(u32 submesh_index) const noexcept {

            if (submesh_index >= submeshes.size())
                return INVALID_HANDLE;
            return submeshes[submesh_index].material;
        }


        // @brief Returns the asset-type tag used by the registry
        FORCE_INLINE_R GLT::asset::type type() const noexcept override { return asset_type; }


        FORCE_INLINE_R u32 collider_count() const noexcept { return static_cast<u32>(colliders.size()); }


        // @brief Approximate resident bytes, used by the profiler
        FORCE_INLINE_R u64 memory_usage() const noexcept override {

            return sizeof(*this)
                + vertices.capacity() * sizeof(GLT::asset::mesh::vertex)
                + indices.capacity() * sizeof(u32)
                + submeshes.capacity() * sizeof(GLT::asset::mesh::submesh)
                + colliders.capacity() * sizeof(GLT::asset::mesh::mesh_collider);
        }
        
    };

}
