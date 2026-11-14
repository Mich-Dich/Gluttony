
#pragma once

#include <limits>

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

    // Virtual geometry (see asset/mesh_cluster.h). All optional: a mesh without them renders with its discrete LODs
    inline constexpr GLT::asset::chunk_id               CHUNK_CLUSTER_SETTINGS = 0x000D;    // cluster_settings (one, user authored)
    inline constexpr GLT::asset::chunk_id               CHUNK_CLUSTERS = 0x000E;            // cluster[] (generated)
    inline constexpr GLT::asset::chunk_id               CHUNK_CLUSTER_INDICES = 0x000F;     // u32[] triangle lists of all clusters, into [CHUNK_VERTICES ++ CHUNK_CLUSTER_VERTICES]
    inline constexpr GLT::asset::chunk_id               CHUNK_CLUSTER_SUBMESHES = 0x0010;   // cluster_submesh[submesh_count]
    inline constexpr GLT::asset::chunk_id               CHUNK_CLUSTER_VERTICES = 0x0011;    // vertex[] only coarse clusters use (recomputed normals), optional

    // Hard upper bound of LOD levels per mesh, LOD 0 included. The renderer packs the per-level switch distances of
    // this many levels into its mesh table, so raising it is a GPU layout change
    inline constexpr u32                                MAX_LODS = 8;

    // [instance::flags] bits 0..3: forced LOD + 1 (0 = automatic selection by screen size). Used by the mesh editor's
    // LOD preview; clamped to the mesh's generated level count by the renderer
    inline constexpr u32                                INSTANCE_FLAG_FORCE_LOD_MASK = 0xFu;

    // Triangles per cluster of the virtual geometry path (cluster_settings::cluster_triangles). The upper bound is
    // meshoptimizer's meshlet limit (256 vertices per cluster)
    inline constexpr u32                                MIN_CLUSTER_TRIANGLES = 32;
    inline constexpr u32                                MAX_CLUSTER_TRIANGLES = 256;

    // cluster::parent_error of a cluster that has no coarser replacement (the top of the hierarchy)
    inline constexpr f32                                CLUSTER_ERROR_NONE = std::numeric_limits<f32>::max();

    // cluster_settings::version of this build. 0 = written before the normals method existed (see sanitize())
    inline constexpr u32                                CLUSTER_SETTINGS_VERSION = 1;

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


    // @brief How a static mesh reduces its detail with distance. A mesh uses exactly one of the two
    //
    // discrete_lods:    whole-mesh levels (lod_settings), authored in the mesh editor
    // virtual_geometry: clusters picked per frame (cluster_settings). The discrete levels are then only a fallback that
    //                   is not authored any more (shadows, translucent submeshes, renderers / frames without the path)
    enum class detail_mode : u8 {

        discrete_lods = 0,
        virtual_geometry,
    };


    enum cluster_flag_bits : u32 {

        CLUSTER_FLAG_NONE = 0,
        CLUSTER_FLAG_ENABLED = 1u << 0,                 // the mesh's detail mode is virtual geometry (mesh_asset::detail())
        CLUSTER_FLAG_LOCK_SEAMS = 1u << 1,              // never move vertices shared by several submeshes (no cracks between materials)

        // Bits 4..5: normals of the simplified clusters (cluster_normals_mode, same values and bit position as the LOD
        // flag). Simplified triangles reuse the source vertices, whose normals belong to the finer surface and, across
        // hard edges, to a different face; the other methods give coarse clusters vertices of their own
        CLUSTER_FLAG_NORMALS_SHIFT = 4,
        CLUSTER_FLAG_NORMALS_MASK = 3u << 4,
    };

    // @brief How simplified clusters get their normals (cluster_settings::flags bits 4..5). Level 0 clusters always use
    // the source vertices
    //
    // keep:        the source vertices' normals, no extra vertices. The behaviour before the methods below existed:
    //              those normals belong to the finer surface and, where a hard edge was collapsed, to the neighbouring
    //              face, so coarse clusters can look blotchy
    // flat:        one normal per face
    // auto_smooth: recomputed from the simplified faces, smooth below cluster_settings::smooth_angle and hard above
    // smooth:      recomputed, smooth everywhere
    using cluster_normals_mode = lod_normals_mode;


    // @brief Per-mesh virtual geometry authoring settings (CHUNK_CLUSTER_SETTINGS). Plain data, blitted to disk
    //
    // Virtual geometry replaces the discrete LOD levels in the main view: the mesh is split into clusters of
    // [cluster_triangles] triangles, groups of neighbouring clusters are simplified and re-split again and again, and the
    // renderer picks, per cluster and per frame, the coarsest version whose error stays below a pixel. The discrete LODs
    // stay in the asset as a fallback: shadows, translucent submeshes and the forced-LOD preview keep using them. Cluster
    // data only exists while CLUSTER_FLAG_ENABLED is set (mesh_cluster.h: set_detail_mode())
    struct cluster_settings {

        u32                                             flags = CLUSTER_FLAG_LOCK_SEAMS
                                                              | (static_cast<u32>(cluster_normals_mode::auto_smooth) << CLUSTER_FLAG_NORMALS_SHIFT);
        u32                                             cluster_triangles = 128;        // MIN..MAX_CLUSTER_TRIANGLES (needs a rebuild)
        f32                                             normal_weight = 0.5f;           // simplifier attribute weights (need a rebuild)
        f32                                             uv_weight = 0.5f;
        f32                                             error_scale = 1.0f;             // live: > 1 keeps more detail, < 1 less (multiplies every cluster error)
        f32                                             smooth_angle = 45.0f;           // auto_smooth: max angle (degrees) between smoothed faces (needs a rebuild)
        u32                                             version = CLUSTER_SETTINGS_VERSION;
        u32                                             _reserved = 0;
    };
    static_assert(sizeof(cluster_settings) == 32);
    static_assert(std::is_trivially_copyable_v<cluster_settings>);


    // @brief One cluster of the virtual geometry hierarchy (CHUNK_CLUSTERS). Uploaded to the GPU as is
    //
    // The clusters of a mesh form a DAG: a group of neighbouring clusters is simplified into fewer, coarser clusters that
    // cover the same surface with the same outline. A cluster is drawn when its own error is small enough on screen and the
    // error of its coarser replacement is not:
    //
    //      projected(lod_bounds, lod_error) <= threshold  &&  projected(parent_bounds, parent_error) > threshold
    //
    // Both tests of a group use the same sphere + error, and errors only grow towards the root, so for any camera exactly
    // one version of every piece of surface passes: no holes, no overlaps, no cracks between neighbouring clusters
    struct cluster {

        glm::vec4                                       bounds{ 0.0f };         // xyz centre, w radius of this cluster's triangles (frustum / cone culling)
        glm::vec4                                       lod_bounds{ 0.0f };     // sphere of the group this cluster was simplified from (source triangles: = bounds)
        glm::vec4                                       parent_bounds{ 0.0f };  // sphere of the group that replaces this cluster with coarser geometry
        f32                                             lod_error = 0.0f;       // deviation from the source surface in mesh units (source triangles: 0)
        f32                                             parent_error = CLUSTER_ERROR_NONE;     // deviation of the coarser replacement
        u32                                             first_index = 0;        // into mesh_asset::cluster_indices
        u32                                             index_count = 0;
        u32                                             submesh = 0;
        u32                                             depth = 0;              // hierarchy level, 0 = source triangles
        i8                                              cone_axis[3]{};         // backface cone: axis (x / 127)
        i8                                              cone_cutoff = 127;      // cos of the cone half angle (x / 127), 127 = cannot be culled
        u32                                             _pad = 0;
    };
    static_assert(sizeof(cluster) == 80);
    static_assert(std::is_trivially_copyable_v<cluster>);


    // @brief Clusters of one submesh (CHUNK_CLUSTER_SUBMESHES). Clusters never span submeshes (one material per cluster)
    struct cluster_submesh {

        u32                                             first_cluster = 0;      // into mesh_asset::clusters
        u32                                             cluster_count = 0;
        u32                                             max_visible = 0;        // upper bound of clusters drawn at the same time (sizes the indirect buffers)
        u32                                             level_count = 0;        // hierarchy levels of this submesh
    };
    static_assert(sizeof(cluster_submesh) == 16);

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

        // ---- virtual geometry (empty clusters = not built; the mesh renders with the levels above) ----
        cluster_settings                                virtual_geometry{};
        std::vector<cluster>                            clusters;           // all submeshes back to back, see cluster_submeshes
        std::vector<u32>                                cluster_indices;    // triangle lists of every cluster, into [vertices ++ cluster_vertices]
        std::vector<cluster_submesh>                    cluster_submeshes;  // one per submesh
        std::vector<vertex>                             cluster_vertices;   // extra vertices of simplified clusters (recomputed normals)

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

        // @brief Vertex [index] of the concatenated stream [vertices ++ cluster_vertices] (what cluster indices address)
        FORCE_INLINE_R const vertex& cluster_vertex_at(u32 index) const noexcept {
            return index < vertices.size() ? vertices[index] : cluster_vertices[index - vertices.size()];
        }

        // @brief Size of [vertices ++ cluster_vertices]
        FORCE_INLINE_R u32 cluster_vertex_count() const noexcept {
            return static_cast<u32>(vertices.size() + cluster_vertices.size());
        }

        // @brief Normals method of the simplified clusters
        FORCE_INLINE_R cluster_normals_mode cluster_normals() const noexcept {
            return static_cast<cluster_normals_mode>((virtual_geometry.flags & CLUSTER_FLAG_NORMALS_MASK) >> CLUSTER_FLAG_NORMALS_SHIFT);
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


        // @brief True if cluster data was built for the current submesh layout
        FORCE_INLINE_R bool has_clusters() const noexcept {

            return !clusters.empty() && !cluster_indices.empty() && cluster_submeshes.size() == submeshes.size();
        }


        // @brief Which of the two detail systems this mesh uses (the user's choice, see detail_mode)
        FORCE_INLINE_R detail_mode detail() const noexcept {

            return (virtual_geometry.flags & CLUSTER_FLAG_ENABLED) != 0 ? detail_mode::virtual_geometry : detail_mode::discrete_lods;
        }


        // @brief True if the renderer should draw this mesh through the virtual geometry path: that is its detail mode
        // and the clusters are built. Until they are, the mesh is drawn with its discrete levels
        FORCE_INLINE_R bool uses_virtual_geometry() const noexcept {

            return (virtual_geometry.flags & CLUSTER_FLAG_ENABLED) != 0 && has_clusters();
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
                + lod_vertices.capacity() * sizeof(GLT::asset::mesh::vertex)
                + clusters.capacity() * sizeof(GLT::asset::mesh::cluster)
                + cluster_indices.capacity() * sizeof(u32)
                + cluster_submeshes.capacity() * sizeof(GLT::asset::mesh::cluster_submesh)
                + cluster_vertices.capacity() * sizeof(GLT::asset::mesh::vertex);
        }
        
    };

}
