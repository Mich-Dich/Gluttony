
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

    // Level of detail (see asset/mesh_lod.h). All optional: a mesh without them renders with its single LOD 0
    inline constexpr GLT::asset::chunk_id               CHUNK_LOD_SETTINGS = 0x0008;    // lod_settings (one, user authored)

    inline constexpr GLT::asset::chunk_id               CHUNK_LOD_LEVELS = 0x0009;      // lod_level[lod_count] (generated)

    inline constexpr GLT::asset::chunk_id               CHUNK_LOD_RANGES = 0x000A;      // lod_range[lod_count * submesh_count]

    inline constexpr GLT::asset::chunk_id               CHUNK_LOD_INDICES = 0x000B;     // u32[] of LOD 1..N (LOD 0 = CHUNK_INDICES)
    inline constexpr GLT::asset::chunk_id               CHUNK_LOD_VERTICES = 0x000C;    // vertex[] only LOD 1..N use (recomputed normals), optional

    // Hard upper bound of LOD levels per mesh, LOD 0 included. The renderer packs the per-level switch distances of
    // this many levels into its mesh table, so raising it is a GPU layout change
    inline constexpr u32                                MAX_LODS = 8;

    // [instance::flags] bits 0..3: forced LOD + 1 (0 = automatic selection by screen size). Used by the mesh editor's
    // LOD preview; clamped to the mesh's generated level count by the renderer
    inline constexpr u32                                INSTANCE_FLAG_FORCE_LOD_MASK = 0xFu;

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

    // @brief Authoring parameters of one LOD level
    //
    // [screen_size] is the projected size below which this level is used: bounding-sphere diameter as a fraction of the
    // viewport height (1.0 = the mesh spans the full height). Level 0 ignores it. Changing it never needs a regeneration
    //
    // [triangle_ratio] / [max_error] drive the simplifier: it removes triangles until the level has
    // [triangle_ratio] x LOD 0's triangles OR the next collapse would move the surface by more than [max_error]
    // (relative to the mesh extent, 0.01 = 1%), whichever comes first. The error limit is what keeps the image intact
    struct lod_level_settings {

        f32                                             screen_size = 1.0f;
        f32                                             triangle_ratio = 1.0f;
        f32                                             max_error = 0.0f;
        u32                                             _pad = 0;
    };
    static_assert(sizeof(lod_level_settings) == 16);


    enum lod_flag_bits : u32 {

        LOD_FLAG_NONE = 0,
        LOD_FLAG_LOCK_BORDERS = 1u << 0,                // keep open borders (no cracks between separately simplified submeshes)
        LOD_FLAG_AUTO_SCREEN_SIZE = 1u << 1,            // derive screen_size from the achieved error and [pixel_error]
        LOD_FLAG_PRUNE = 1u << 2,                       // allow removing small disconnected parts (bounded by the error limit)
        LOD_FLAG_ALLOW_SUBMESH_REMOVAL = 1u << 3,       // off (default): a submesh never vanishes, it keeps the previous level's geometry

        // Bits 4..5: normals of the generated levels (lod_normals_mode). Simplified triangles reuse LOD 0's vertices, whose
        // normals belong to the original, finer surface; recomputing gives each level normals that match its own faces
        LOD_FLAG_NORMALS_SHIFT = 4,
        LOD_FLAG_NORMALS_MASK = 3u << 4,
    };

    // @brief How the generated levels get their normals (lod_settings::flags bits 4..5)
    enum class lod_normals_mode : u32 {
        keep = 0,                                       // reuse LOD 0's vertex normals (no extra vertices)
        flat = 1,                                       // one normal per face (faceted)
        auto_smooth = 2,                                // smooth across edges below lod_settings::smooth_angle, hard above
        smooth = 3,                                     // smooth everywhere
    };


    // @brief Per-mesh LOD authoring settings (CHUNK_LOD_SETTINGS). Plain data, blitted to disk
    struct lod_settings {

        u32                                             lod_count = 4;                  // requested levels, LOD 0 included (1..MAX_LODS)
        u32                                             flags = LOD_FLAG_LOCK_BORDERS | LOD_FLAG_AUTO_SCREEN_SIZE | LOD_FLAG_PRUNE
                                                              | (static_cast<u32>(lod_normals_mode::auto_smooth) << LOD_FLAG_NORMALS_SHIFT);
        f32                                             cull_screen_size = 0.0f;        // below this the mesh is not drawn at all (0 = never)
        u32                                             shadow_lod_bias = 1;            // shadow passes use this many levels coarser
        f32                                             pixel_error = 1.0f;             // AUTO_SCREEN_SIZE: allowed error in pixels at 1080p
        f32                                             normal_weight = 0.5f;           // simplifier attribute weights
        f32                                             uv_weight = 0.5f;
        f32                                             smooth_angle = 45.0f;           // auto_smooth: max angle (degrees) between smoothed faces
        lod_level_settings                              levels[MAX_LODS]{
            { 1.000f, 1.000f, 0.000f, 0 },
            { 0.300f, 0.500f, 0.005f, 0 },
            { 0.120f, 0.250f, 0.015f, 0 },
            { 0.050f, 0.125f, 0.040f, 0 },
            { 0.020f, 0.060f, 0.080f, 0 },
            { 0.010f, 0.030f, 0.150f, 0 },
            { 0.005f, 0.015f, 0.250f, 0 },
            { 0.002f, 0.008f, 0.400f, 0 },
        };
    };
    static_assert(sizeof(lod_settings) == 32 + 16 * MAX_LODS);
    static_assert(std::is_trivially_copyable_v<lod_settings>);


    // @brief Result of generating one LOD level (CHUNK_LOD_LEVELS)
    struct lod_level {

        u32                                             index_count = 0;                // all submeshes of this level
        f32                                             error = 0.0f;                   // achieved deviation, relative to the mesh extent
        u32                                             _pad0 = 0;
        u32                                             _pad1 = 0;
    };
    static_assert(sizeof(lod_level) == 16);


    // @brief Index range of one submesh in one LOD level (CHUNK_LOD_RANGES)
    //
    // [first_index] addresses the concatenated index stream [indices ++ lod_indices], so LOD 0 ranges equal the submeshes
    // and the renderer can upload both arrays back to back and use the ranges unchanged
    struct lod_range {

        u32                                             first_index = 0;
        u32                                             index_count = 0;
    };
    static_assert(sizeof(lod_range) == 8);

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

        // ---- level of detail (empty lods = single level, LOD 0 is always [indices] / [submeshes]) ----
        lod_settings                                    lod{};
        std::vector<lod_level>                          lods;           // generated levels, [0] describes LOD 0
        std::vector<lod_range>                          lod_ranges;     // [level * submesh_count + submesh]
        std::vector<u32>                                lod_indices;    // index data of LOD 1..N, into [vertices ++ lod_vertices]
        std::vector<vertex>                             lod_vertices;   // extra vertices of LOD 1..N (recomputed normals)

        // Bumped by in-place edits (LOD regeneration, ...) so GPU consumers know to re-upload. Runtime only
        u32                                             revision = 0;


        FORCE_INLINE_R u32 submesh_count() const noexcept { return static_cast<u32>(submeshes.size()); }


        FORCE_INLINE_R GLT::asset::handle default_material_for(u32 submesh_index) const noexcept {

            if (submesh_index >= submeshes.size())
                return INVALID_HANDLE;
            return submeshes[submesh_index].material;
        }


        // @brief Returns the asset-type tag used by the registry
        FORCE_INLINE_R GLT::asset::type type() const noexcept override { return asset_type; }


        FORCE_INLINE_R u32 collider_count() const noexcept { return static_cast<u32>(colliders.size()); }


        // @brief Number of usable LOD levels (>= 1)
        FORCE_INLINE_R u32 lod_count() const noexcept {

            const size_t submesh_n = submeshes.size();
            if (lods.size() <= 1 || lod_ranges.size() != lods.size() * submesh_n)
                return 1;
            return static_cast<u32>(lods.size());
        }


        // @brief Index range of [submesh] in [level], in the concatenated stream [indices ++ lod_indices]
        FORCE_INLINE_R lod_range range(u32 level, u32 submesh) const noexcept {

            if (submesh >= submeshes.size())
                return {};
            if (level == 0 || level >= lod_count())
                return { submeshes[submesh].first_index, submeshes[submesh].index_count };
            return lod_ranges[static_cast<size_t>(level) * submeshes.size() + submesh];
        }


        // @brief Vertex [index] of the concatenated stream [vertices ++ lod_vertices] (what LOD indices address)
        FORCE_INLINE_R const vertex& vertex_at(u32 index) const noexcept {
            return index < vertices.size() ? vertices[index] : lod_vertices[index - vertices.size()];
        }

        // @brief Size of [vertices ++ lod_vertices]
        FORCE_INLINE_R u32 total_vertex_count() const noexcept {
            return static_cast<u32>(vertices.size() + lod_vertices.size());
        }

        // @brief Normals mode of the generated levels
        FORCE_INLINE_R lod_normals_mode normals_mode() const noexcept {
            return static_cast<lod_normals_mode>((lod.flags & LOD_FLAG_NORMALS_MASK) >> LOD_FLAG_NORMALS_SHIFT);
        }

        // @brief Index value at [position] of the concatenated stream [indices ++ lod_indices]
        FORCE_INLINE_R u32 index_at(u32 position) const noexcept {

            return position < indices.size() ? indices[position] : lod_indices[position - indices.size()];
        }


        // @brief Triangles of one level (all submeshes)
        FORCE_INLINE_R u32 lod_triangle_count(u32 level) const noexcept {

            if (level == 0 || level >= lod_count())
                return static_cast<u32>(indices.size() / 3);
            return lods[level].index_count / 3;
        }


        // @brief Approximate resident bytes, used by the profiler
        FORCE_INLINE_R u64 memory_usage() const noexcept override {

            return sizeof(*this)
                + vertices.capacity() * sizeof(GLT::asset::mesh::vertex)
                + indices.capacity() * sizeof(u32)
                + submeshes.capacity() * sizeof(GLT::asset::mesh::submesh)
                + colliders.capacity() * sizeof(GLT::asset::mesh::mesh_collider)
                + lods.capacity() * sizeof(GLT::asset::mesh::lod_level)
                + lod_ranges.capacity() * sizeof(GLT::asset::mesh::lod_range)
                + lod_indices.capacity() * sizeof(u32)
                + lod_vertices.capacity() * sizeof(GLT::asset::mesh::vertex);
        }
        
    };

}
