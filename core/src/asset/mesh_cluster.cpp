#include "util/pch.h"
#include "mesh_cluster.h"

#include <atomic>
#include <cassert>
#include <cmath>
#include <cstring>
#include <future>
#include <thread>

#include <meshoptimizer.h>

// clusterlod: meshoptimizer's reference implementation of the cluster hierarchy build (MIT, same author and licence as
// the library). It ships in the library's demo folder, next to the include directory of the `meshoptimizer` target.
// This is the one translation unit that compiles its implementation
#define CLUSTERLOD_IMPLEMENTATION
#include <../demo/clusterlod.h>

#include "debug/trace.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::mesh {

    // CONSTANTS =======================================================================================================

    constexpr u32                                       CLUSTER_ATTRIBUTE_COUNT = 5;    // normal xyz, uv0 xy

    // Attribute floats that mark a seam the simplifier must not collapse across: uv0 (bits 3 and 4). Normal seams (hard
    // edges) stay collapsible, otherwise hard-surface meshes stop simplifying at every panel edge. A collapse across a
    // hard edge leaves triangles on a vertex that carries the neighbouring face's normal, so this only works when the
    // simplified clusters get normals of their own (assign_normals()). cluster_normals_mode::keep does not: it is the
    // behaviour before the normals methods existed, kept so meshes built with it look the same after a rebuild.
    // Protecting the normal floats there instead was tried and dropped: on a faceted mesh every vertex is then a
    // protected seam, the edge-collapse simplifier cannot move anything and every group falls back to the sloppy one
    constexpr unsigned int                              CLUSTER_PROTECT_MASK = (1u << 3) | (1u << 4);

    // assign_normals(): a recomputed normal within this angle of the one the vertex already has reuses that vertex
    // instead of creating a new one (cos 1 degree). Smooth meshes then need hardly any extra vertices
    constexpr f32                                       NORMAL_REUSE_COS = 0.99985f;

    // meshoptimizer's limit of vertices per meshlet. Clusters are drawn from the shared vertex buffer through an index
    // range, so vertices per cluster cost nothing at runtime: allow as many as the library takes, which keeps clusters of
    // faceted meshes (3 vertices per triangle) from ending up a third full
    constexpr size_t                                    CLUSTER_MAX_VERTICES = 256;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // One submesh as a self-contained vertex / index set (meshoptimizer's cost is O(vertex_count) per call)
    struct cluster_source {

        std::vector<u32>                                to_global;      // local vertex -> mesh vertex
        std::vector<f32>                                positions;      // xyz
        std::vector<f32>                                attributes;     // CLUSTER_ATTRIBUTE_COUNT per vertex
        std::vector<u8>                                 locks;          // meshopt_SimplifyVertex_* per vertex, empty = none
        std::vector<u32>                                indices;        // local
    };

    // A vertex a simplified cluster uses instead of a source vertex: everything of [source] with another normal
    struct cluster_new_vertex {

        u32                                             source = 0;     // local vertex it copies
        glm::vec3                                       normal{ 0.0f };
    };

    // Clusters of one submesh, before they are stitched into the mesh-wide arrays
    struct cluster_part {

        std::vector<cluster>                            clusters;       // first_index relative to [indices]
        std::vector<u32>                                indices;        // per corner: local vertex of the submesh (what the simplifier produced)
        std::vector<u32>                                records;        // per corner: vertex to draw with. < local vertex count: that local vertex, else new_vertices[value - count]
        std::vector<cluster_new_vertex>                 new_vertices;
        std::vector<i32>                                refined;        // per cluster: group it was simplified from, -1 = source triangles
        std::vector<u32>                                group_first;    // per group: its member clusters [first, first + count)
        std::vector<u32>                                group_count;
        std::vector<u32>                                group_depth;
        u32                                             max_visible = 0;
        u32                                             level_count = 0;
    };

    // What the build remembers of a group: its sphere + error, and how many clusters went in / came out
    struct cluster_group {

        clodBounds                                      simplified{};
        u32                                             inputs = 0;
        u32                                             outputs = 0;
    };

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // Per mesh vertex: true if its position is used by more than one submesh. Simplifying two submeshes independently
    // moves their common border differently on each side, which opens cracks along material seams; locked vertices stay
    static std::vector<u8> find_seam_vertices(std::span<const vertex> vertices, std::span<const u32> indices,
        std::span<const submesh> submeshes) {

        std::vector<u8> seam(vertices.size(), 0u);
        if (submeshes.size() < 2 || vertices.empty())
            return seam;

        // Same position -> same id
        std::vector<unsigned int> remap(vertices.size());
        meshopt_generatePositionRemap(remap.data(), &vertices[0].position.x, vertices.size(), sizeof(vertex));

        // owner: first submesh that used the welded position (+1), seam once a second one shows up
        std::vector<u32> owner(vertices.size(), 0u);
        std::vector<u8> shared(vertices.size(), 0u);
        for (size_t s = 0; s < submeshes.size(); ++s) {

            const submesh& sm = submeshes[s];
            if (static_cast<u64>(sm.first_index) + sm.index_count > indices.size())
                continue;
            for (u32 i = 0; i < sm.index_count; ++i) {
                const u32 v = indices[sm.first_index + i];
                if (v >= vertices.size())
                    continue;
                const u32 welded = remap[v];
                if (owner[welded] == 0u)
                    owner[welded] = static_cast<u32>(s) + 1u;
                else if (owner[welded] != static_cast<u32>(s) + 1u)
                    shared[welded] = 1u;
            }
        }
        for (size_t v = 0; v < vertices.size(); ++v)
            seam[v] = shared[remap[v]];
        return seam;
    }


    static cluster_source make_source(std::span<const vertex> vertices, std::span<const u32> indices, const submesh& sm,
        std::span<const u8> seam, std::vector<u32>& to_local) {

        cluster_source source{};
        if (static_cast<u64>(sm.first_index) + sm.index_count > indices.size())
            return source;                                          // malformed range: no clusters, the submesh is not drawn

        bool any_lock = false;
        source.indices.reserve(sm.index_count);
        for (u32 i = 0; i + 2 < sm.index_count; i += 3) {

            const u32* triangle = &indices[sm.first_index + i];
            if (triangle[0] >= vertices.size() || triangle[1] >= vertices.size() || triangle[2] >= vertices.size())
                continue;                                           // corrupt triangle: skipped, as in mesh_lod.cpp

            for (u32 corner = 0; corner < 3; ++corner) {

                const u32 global = triangle[corner];
                u32& mapped = to_local[global];
                if (mapped == UINT32_MAX) {

                    mapped = static_cast<u32>(source.to_global.size());
                    source.to_global.push_back(global);

                    const vertex& v = vertices[global];
                    source.positions.insert(source.positions.end(), { v.position.x, v.position.y, v.position.z });
                    // NaN attributes (assimp tangent space / degenerate faces) would poison the error metric
                    auto clean = [](f32 x) { return std::isfinite(x) ? x : 0.0f; };
                    source.attributes.insert(source.attributes.end(),
                        { clean(v.normal.x), clean(v.normal.y), clean(v.normal.z), clean(v.uv0.x), clean(v.uv0.y) });

                    const bool locked = !seam.empty() && seam[global] != 0u;
                    source.locks.push_back(locked ? static_cast<u8>(meshopt_SimplifyVertex_Lock) : u8{ 0 });
                    any_lock |= locked;
                }
                source.indices.push_back(mapped);
            }
        }

        // Reset only the entries this submesh touched (submeshes usually do not share vertices, but they may)
        for (u32 global : source.to_global)
            to_local[global] = UINT32_MAX;
        if (!any_lock)
            source.locks.clear();
        return source;
    }


    // Gives the simplified clusters of one submesh normals that fit their own triangles (cluster_normals_mode other than
    // keep). Fills part.records / part.new_vertices; level 0 clusters keep the source vertices.
    //
    // The rule that keeps shading continuous across clusters of different levels is the one the simplifier uses for
    // positions: when a group of clusters is simplified, the vertices on the group's outline (shared with other groups
    // of that level, or seam-locked) are locked. Everything a renderer can draw next to the group's simplified clusters
    // shares exactly those vertices, so there the simplified clusters INHERIT the vertex (normal included) the group's
    // input clusters used. All other positions are interior: every triangle around them belongs to this group's
    // output, whichever clusters are drawn, so they get a normal recomputed from those triangles:
    //      flat         the face normal (everywhere, also on the outline: flat shading has nothing to keep continuous)
    //      smooth       area-weighted sum of all faces around the position
    //      auto_smooth  ... of the faces within the smoothing angle of the corner's own face
    // By induction every vertex position has one consistent set of normals in every selection of clusters.
    //
    // On the outline the simplifier may have left a triangle on the vertex of a neighbouring face (collapse across a
    // hard edge): a corner takes the inherited normal that fits its face best, from any vertex at that position
    static void assign_normals(cluster_part& part, const cluster_source& source, std::span<const vertex> vertices,
        const cluster_normals_mode mode, const f32 cos_angle) {
        GLT_PROFILE_SCOPE("mesh::build_clusters normals");

        const u32 local_count = static_cast<u32>(source.to_global.size());
        const size_t group_total = part.group_first.size();
        if (local_count == 0 || group_total == 0)
            return;

        auto position_of = [&](const u32 local) {
            return glm::vec3(source.positions[local * 3 + 0], source.positions[local * 3 + 1], source.positions[local * 3 + 2]);
        };
        auto source_of = [&](const u32 record) {
            return record < local_count ? record : part.new_vertices[record - local_count].source;
        };
        auto normal_of = [&](const u32 record) -> glm::vec3 {
            if (record >= local_count)
                return part.new_vertices[record - local_count].normal;
            const glm::vec3 n = vertices[source.to_global[record]].normal;
            return std::isfinite(n.x + n.y + n.z) ? n : glm::vec3(0.0f);
        };
        // Same texture coordinate and handedness: one can stand in for the other when the normal is the same
        auto same_wedge = [&](const u32 local_a, const u32 local_b) {
            if (local_a == local_b)
                return true;
            const vertex& a = vertices[source.to_global[local_a]];
            const vertex& b = vertices[source.to_global[local_b]];
            return a.uv0 == b.uv0 && (a.tangent.w < 0.0f) == (b.tangent.w < 0.0f);
        };

        // Same position -> same id (the simplifier welds by position in the same way)
        std::vector<unsigned int> welded(local_count);
        meshopt_generatePositionRemap(welded.data(), source.positions.data(), local_count, sizeof(f32) * 3);

        std::vector<u8> seam_locked(local_count, 0u);
        for (u32 v = 0; v < local_count && !source.locks.empty(); ++v)
            if (source.locks[v] & meshopt_SimplifyVertex_Lock)
                seam_locked[welded[v]] = 1u;

        // New vertices, shared by every corner that asks for the same (vertex, normal)
        struct record_key {
            u32 vertex; u64 normal;
            bool operator==(const record_key&) const = default;
        };
        struct record_key_hash {
            size_t operator()(const record_key& k) const noexcept {
                u64 h = (k.normal + 0x9E3779B97F4A7C15ull) * 0xC2B2AE3D27D4EB4Full;
                h ^= static_cast<u64>(k.vertex) * 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
                return static_cast<size_t>(h ^ (h >> 31));
            }
        };
        std::unordered_map<record_key, u32, record_key_hash> created;
        auto quantise = [](const glm::vec3& n) {
            auto q = [](f32 x) { return static_cast<u64>(static_cast<u32>(static_cast<i32>(std::lround(std::clamp(x, -1.0f, 1.0f) * 32767.0f)) & 0xffff)); };
            return q(n.x) | (q(n.y) << 16) | (q(n.z) << 32);
        };
        auto make_record = [&](const u32 local, const glm::vec3& normal) -> u32 {
            if (glm::dot(normal_of(local), normal) >= NORMAL_REUSE_COS)
                return local;                                       // the source vertex already has this normal
            const auto [it, inserted] = created.try_emplace(record_key{ local, quantise(normal) }, 0u);
            if (inserted) {
                it->second = local_count + static_cast<u32>(part.new_vertices.size());
                part.new_vertices.push_back({ local, normal });
            }
            return it->second;
        };

        // Clusters made from each group
        std::vector<std::vector<u32>> outputs(group_total);
        for (u32 c = 0; c < part.clusters.size(); ++c)
            if (part.refined[c] >= 0)
                outputs[static_cast<size_t>(part.refined[c])].push_back(c);

        // Per welded position: the group that used it first at the level being processed, and whether a second one did
        constexpr u32 NONE = UINT32_MAX;
        std::vector<u32> owner(local_count, NONE), owner_level(local_count, NONE), shared_level(local_count, NONE);

        u32 max_depth = 0;
        for (const u32 depth : part.group_depth)
            max_depth = std::max(max_depth, depth);

        // Scratch, reused for every group
        std::unordered_map<u32, std::vector<u32>> inherited_at;      // welded position -> vertices the group's inputs use there
        std::unordered_map<u32, u32> inherited_of;                  // local vertex -> vertex the group's inputs use for it
        std::vector<glm::vec3> weighted, unit;                      // per face of the group's output
        std::vector<std::pair<u32, u32>> incident;                  // (welded position, face), sorted

        for (u32 depth = 0; depth <= max_depth; ++depth) {

            // ---- outline of every group of this level: positions more than one group uses ------------------------------
            for (size_t g = 0; g < group_total; ++g) {
                if (part.group_depth[g] != depth)
                    continue;
                for (u32 m = 0; m < part.group_count[g]; ++m) {
                    const cluster& member = part.clusters[part.group_first[g] + m];
                    for (u32 i = 0; i < member.index_count; ++i) {
                        const u32 p = welded[part.indices[member.first_index + i]];
                        if (owner_level[p] != depth) {
                            owner_level[p] = depth;
                            owner[p] = static_cast<u32>(g);
                        } else if (owner[p] != g) {
                            shared_level[p] = depth;
                        }
                    }
                }
            }
            auto on_outline = [&](const u32 p) { return shared_level[p] == depth || seam_locked[p] != 0u; };

            // ---- the clusters made from each group ---------------------------------------------------------------------
            for (size_t g = 0; g < group_total; ++g) {

                if (part.group_depth[g] != depth || outputs[g].empty())
                    continue;

                inherited_at.clear();
                inherited_of.clear();
                if (mode != cluster_normals_mode::flat)
                    for (u32 m = 0; m < part.group_count[g]; ++m) {
                        const cluster& member = part.clusters[part.group_first[g] + m];
                        for (u32 i = 0; i < member.index_count; ++i) {
                            const u32 local = part.indices[member.first_index + i];
                            const u32 record = part.records[member.first_index + i];
                            inherited_of.try_emplace(local, record);
                            if (on_outline(welded[local])) {
                                std::vector<u32>& list = inherited_at[welded[local]];
                                if (std::find(list.begin(), list.end(), record) == list.end())
                                    list.push_back(record);
                            }
                        }
                    }

                // Faces of the group's output, and which of them meet at each position
                size_t corner_total = 0;
                for (const u32 c : outputs[g])
                    corner_total += part.clusters[c].index_count;
                weighted.clear(); unit.clear(); incident.clear();
                weighted.reserve(corner_total / 3); unit.reserve(corner_total / 3); incident.reserve(corner_total);
                for (const u32 c : outputs[g]) {
                    const cluster& out = part.clusters[c];
                    for (u32 t = 0; t + 2 < out.index_count; t += 3) {
                        const u32* tri = &part.indices[out.first_index + t];
                        const glm::vec3 a = position_of(tri[0]);
                        const glm::vec3 n = glm::cross(position_of(tri[1]) - a, position_of(tri[2]) - a);
                        const f32 length = glm::length(n);
                        const u32 face = static_cast<u32>(weighted.size());
                        weighted.push_back(n);
                        unit.push_back((length > 1e-20f && std::isfinite(length)) ? n / length : glm::vec3(0.0f));
                        for (u32 corner = 0; corner < 3; ++corner)
                            incident.emplace_back(welded[tri[corner]], face);
                    }
                }
                std::sort(incident.begin(), incident.end());

                // Sum of the faces around [p] that a corner of [face] is smooth with
                auto corner_normal = [&](const u32 p, const u32 face) -> glm::vec3 {
                    glm::vec3 sum(0.0f);
                    for (auto it = std::lower_bound(incident.begin(), incident.end(), std::pair<u32, u32>(p, 0u));
                        it != incident.end() && it->first == p; ++it)
                        if (mode == cluster_normals_mode::smooth || glm::dot(unit[it->second], unit[face]) >= cos_angle)
                            sum += weighted[it->second];
                    const f32 length = glm::length(sum);
                    return (length > 1e-20f && std::isfinite(length)) ? sum / length : unit[face];
                };

                u32 face = 0;
                for (const u32 c : outputs[g]) {

                    const cluster& out = part.clusters[c];
                    for (u32 t = 0; t + 2 < out.index_count; t += 3, ++face) {
                        for (u32 corner = 0; corner < 3; ++corner) {

                            const size_t at = static_cast<size_t>(out.first_index) + t + corner;
                            const u32 local = part.indices[at];
                            const u32 p = welded[local];
                            const glm::vec3 face_normal = unit[face];
                            const auto own = inherited_of.find(local);
                            const u32 own_record = own != inherited_of.end() ? own->second : local;

                            if (face_normal == glm::vec3(0.0f)) {           // degenerate triangle: draws nothing, keep what was there
                                part.records[at] = own_record;
                                continue;
                            }
                            if (mode == cluster_normals_mode::flat) {
                                part.records[at] = make_record(local, face_normal);
                                continue;
                            }

                            if (on_outline(p)) {

                                // Inherit: the normal at this position that fits the face best, preferring the vertex
                                // the corner already references
                                u32 best = NONE;
                                f32 best_fit = -2.0f;
                                if (const auto list = inherited_at.find(p); list != inherited_at.end())
                                    for (const u32 record : list->second) {
                                        const f32 fit = glm::dot(normal_of(record), face_normal) + (source_of(record) == local ? 1e-4f : 0.0f);
                                        if (fit > best_fit) {
                                            best_fit = fit;
                                            best = record;
                                        }
                                    }
                                if (best != NONE && (mode == cluster_normals_mode::smooth || best_fit >= cos_angle)) {
                                    part.records[at] = same_wedge(source_of(best), local) ? best : make_record(local, normal_of(best));
                                    continue;
                                }
                                // Nothing inherited is smooth with this face (a hard edge on the outline): its own normal
                                part.records[at] = make_record(local, corner_normal(p, face));
                                continue;
                            }

                            // Interior: recomputed. Reuse the vertex the inputs had when it already carries that normal
                            const glm::vec3 normal = corner_normal(p, face);
                            part.records[at] = glm::dot(normal_of(own_record), normal) >= NORMAL_REUSE_COS && same_wedge(source_of(own_record), local)
                                ? own_record : make_record(local, normal);
                        }
                    }
                }
            }
        }
    }


    static glm::vec4 to_sphere(const clodBounds& bounds) noexcept {

        return { bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius };
    }


    static cluster_part build_part(const cluster_source& source, std::span<const vertex> vertices, const cluster_settings& settings,
        u32 submesh_index) {
        GLT_PROFILE_SCOPE("mesh::build_clusters submesh");

        cluster_part part{};
        if (source.indices.size() < 3)
            return part;

        const cluster_normals_mode normals = static_cast<cluster_normals_mode>(
            (settings.flags & CLUSTER_FLAG_NORMALS_MASK) >> CLUSTER_FLAG_NORMALS_SHIFT);

        clodConfig config = clodDefaultConfig(settings.cluster_triangles);
        config.max_vertices = std::min<size_t>(CLUSTER_MAX_VERTICES, static_cast<size_t>(settings.cluster_triangles) * 2);
        config.optimize_bounds = false;                             // culling spheres + cones are computed below, per cluster

        const f32 attribute_weights[CLUSTER_ATTRIBUTE_COUNT] = {
            settings.normal_weight, settings.normal_weight, settings.normal_weight, settings.uv_weight, settings.uv_weight,
        };

        clodMesh mesh{};
        mesh.indices = source.indices.data();
        mesh.index_count = source.indices.size();
        mesh.vertex_count = source.to_global.size();
        mesh.vertex_positions = source.positions.data();
        mesh.vertex_positions_stride = sizeof(f32) * 3;
        mesh.vertex_attributes = source.attributes.data();
        mesh.vertex_attributes_stride = sizeof(f32) * CLUSTER_ATTRIBUTE_COUNT;
        mesh.vertex_lock = source.locks.empty() ? nullptr : source.locks.data();
        mesh.attribute_weights = attribute_weights;
        mesh.attribute_count = CLUSTER_ATTRIBUTE_COUNT;
        mesh.attribute_protect_mask = CLUSTER_PROTECT_MASK;

        std::vector<cluster_group> groups;
        u32 level_count = 0;

        // Called once per group, in build order: the group's member clusters are final at that point. A group either
        // was simplified ([group.simplified.error] = error of the coarser clusters made from it) or is terminal
        // (FLT_MAX: nothing coarser exists). The returned id comes back as clodCluster::refined of the clusters that were
        // made from this group
        clodBuild(config, mesh, [&](clodGroup group, const clodCluster* members, size_t member_count) -> int {

            const int group_id = static_cast<int>(groups.size());
            groups.push_back({ group.simplified, static_cast<u32>(member_count), 0u });
            level_count = std::max(level_count, static_cast<u32>(group.depth) + 1u);
            part.group_first.push_back(static_cast<u32>(part.clusters.size()));
            part.group_depth.push_back(static_cast<u32>(group.depth));

            for (size_t m = 0; m < member_count; ++m) {

                const clodCluster& member = members[m];
                if (member.index_count < 3)
                    continue;

                // Tight sphere + backface cone of this cluster's own triangles
                const meshopt_Bounds bounds = meshopt_computeClusterBounds(member.indices, member.index_count,
                    source.positions.data(), source.to_global.size(), sizeof(f32) * 3);

                cluster out{};
                out.bounds = { bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius };
                out.cone_axis[0] = bounds.cone_axis_s8[0];
                out.cone_axis[1] = bounds.cone_axis_s8[1];
                out.cone_axis[2] = bounds.cone_axis_s8[2];
                out.cone_cutoff = bounds.cone_cutoff_s8;

                if (member.refined >= 0 && static_cast<size_t>(member.refined) < groups.size()) {

                    // Made by simplifying group [refined]: that group's sphere + error decide whether this cluster is
                    // detailed enough, the same values its member clusters use as their parent
                    cluster_group& origin = groups[static_cast<size_t>(member.refined)];
                    origin.outputs++;
                    out.lod_bounds = to_sphere(origin.simplified);
                    out.lod_error = origin.simplified.error;

                } else {

                    out.lod_bounds = out.bounds;                    // source triangles: always detailed enough
                    out.lod_error = 0.0f;
                }

                out.parent_bounds = to_sphere(group.simplified);
                out.parent_error = group.simplified.error;          // FLT_MAX (= CLUSTER_ERROR_NONE) for terminal groups
                out.first_index = static_cast<u32>(part.indices.size());
                out.index_count = static_cast<u32>(member.index_count - member.index_count % 3);
                out.submesh = submesh_index;
                out.depth = static_cast<u32>(group.depth);

                for (u32 i = 0; i < out.index_count; ++i)
                    part.indices.push_back(member.indices[i]);
                part.clusters.push_back(out);
                part.refined.push_back(member.refined >= 0 && static_cast<size_t>(member.refined) < groups.size() ? member.refined : -1);
            }
            part.group_count.push_back(static_cast<u32>(part.clusters.size()) - part.group_first.back());
            return group_id;
        });

        // Vertices the clusters are drawn with: the source vertices, or new ones with recomputed normals
        part.records = part.indices;
        if (normals != cluster_normals_mode::keep)
            assign_normals(part, source, vertices, normals, std::cos(glm::radians(settings.smooth_angle)));

        // Upper bound of clusters that can be selected at the same time. Every selection is the source clusters with
        // some groups "collapsed" (their members replaced by the clusters made from them), so it can only exceed the
        // source cluster count where a group produced more clusters than it consumed
        u32 source_clusters = 0;
        for (const cluster& c : part.clusters)
            source_clusters += c.depth == 0 ? 1u : 0u;
        u32 growth = 0;
        for (const cluster_group& group : groups)
            growth += group.outputs > group.inputs ? group.outputs - group.inputs : 0u;

        part.max_visible = source_clusters + growth;
        part.level_count = level_count;
        return part;
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    void sanitize(cluster_settings& settings) {

        // Settings written before the normals method existed (version 0): a mesh that already is in virtual geometry mode
        // keeps what its clusters were built with (the source normals); any other mesh gets today's default
        if (settings.version < 1u) {
            settings.smooth_angle = cluster_settings{}.smooth_angle;
            if (!(settings.flags & CLUSTER_FLAG_ENABLED))
                settings.flags = (settings.flags & ~static_cast<u32>(CLUSTER_FLAG_NORMALS_MASK))
                    | (cluster_settings{}.flags & CLUSTER_FLAG_NORMALS_MASK);
            else
                settings.flags &= ~static_cast<u32>(CLUSTER_FLAG_NORMALS_MASK);
        }
        settings.version = CLUSTER_SETTINGS_VERSION;

        settings.flags &= (CLUSTER_FLAG_ENABLED | CLUSTER_FLAG_LOCK_SEAMS | CLUSTER_FLAG_NORMALS_MASK);
        settings.cluster_triangles = std::clamp<u32>(settings.cluster_triangles, MIN_CLUSTER_TRIANGLES, MAX_CLUSTER_TRIANGLES);
        settings.normal_weight = std::isfinite(settings.normal_weight) ? std::clamp(settings.normal_weight, 0.0f, 16.0f) : 0.5f;
        settings.uv_weight = std::isfinite(settings.uv_weight) ? std::clamp(settings.uv_weight, 0.0f, 16.0f) : 0.5f;
        settings.error_scale = std::isfinite(settings.error_scale) ? std::clamp(settings.error_scale, 0.05f, 20.0f) : 1.0f;
        settings.smooth_angle = std::isfinite(settings.smooth_angle) ? std::clamp(settings.smooth_angle, 0.0f, 180.0f) : 45.0f;
        settings._reserved = 0;
    }


    cluster_build_output build_clusters(std::span<const vertex> vertices, std::span<const u32> indices,
        std::span<const submesh> submeshes, const cluster_settings& in_settings) {
        GLT_PROFILE_SCOPE("mesh::build_clusters");

        const auto start = std::chrono::steady_clock::now();

        cluster_settings settings = in_settings;
        sanitize(settings);

        cluster_build_output out{};
        if (vertices.empty() || indices.empty() || submeshes.empty())
            return out;

        // ---- per submesh sources (sequential: they share the to_local scratch) --------------------------------------
        std::vector<u8> seam;
        if (settings.flags & CLUSTER_FLAG_LOCK_SEAMS)
            seam = find_seam_vertices(vertices, indices, submeshes);

        std::vector<cluster_source> sources(submeshes.size());
        {
            std::vector<u32> to_local(vertices.size(), UINT32_MAX);
            for (size_t s = 0; s < submeshes.size(); ++s)
                sources[s] = make_source(vertices, indices, submeshes[s], seam, to_local);
        }

        // ---- hierarchies, one submesh per task ----------------------------------------------------------------------
        std::vector<cluster_part> parts(submeshes.size());
        {
            std::atomic<size_t> next{ 0 };
            auto worker = [&]() {
                for (size_t s = next.fetch_add(1); s < sources.size(); s = next.fetch_add(1))
                    parts[s] = build_part(sources[s], vertices, settings, static_cast<u32>(s));
            };

            const size_t thread_count = std::min<size_t>(sources.size(), std::max(1u, std::thread::hardware_concurrency()));
            std::vector<std::future<void>> jobs;
            for (size_t t = 1; t < thread_count; ++t)
                jobs.push_back(std::async(std::launch::async, worker));
            worker();
            for (auto& job : jobs)
                job.get();
        }

        // ---- stitch in submesh order --------------------------------------------------------------------------------
        size_t cluster_total = 0, index_total = 0, vertex_total = 0;
        for (const cluster_part& part : parts) {
            cluster_total += part.clusters.size();
            index_total += part.records.size();
            vertex_total += part.new_vertices.size();
        }
        if (cluster_total > UINT32_MAX || index_total > UINT32_MAX || vertices.size() + vertex_total > UINT32_MAX)
            return {};                                              // cannot be addressed with 32-bit offsets

        out.clusters.reserve(cluster_total);
        out.indices.reserve(index_total);
        out.submeshes.reserve(submeshes.size());
        out.vertices.reserve(vertex_total);

        for (size_t s = 0; s < parts.size(); ++s) {

            const cluster_part& part = parts[s];
            const cluster_source& source = sources[s];
            const u32 local_count = static_cast<u32>(source.to_global.size());
            const u32 first_cluster = static_cast<u32>(out.clusters.size());
            const u32 first_index = static_cast<u32>(out.indices.size());

            // New vertices of this submesh: the source vertex with the recomputed normal, tangent made orthogonal to it.
            // Cluster indices address [source vertices ++ out.vertices]
            const u32 first_new_vertex = static_cast<u32>(vertices.size() + out.vertices.size());
            for (const cluster_new_vertex& added : part.new_vertices) {

                vertex v = vertices[source.to_global[added.source]];
                v.normal = added.normal;
                glm::vec3 tangent = glm::vec3(v.tangent) - added.normal * glm::dot(added.normal, glm::vec3(v.tangent));
                const f32 tangent_length = glm::length(tangent);
                if (!(tangent_length > 1e-12f) || !std::isfinite(tangent_length)) {
                    const glm::vec3 axis = std::abs(added.normal.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
                    tangent = glm::normalize(glm::cross(axis, added.normal));
                } else {
                    tangent /= tangent_length;
                }
                v.tangent = glm::vec4(tangent, v.tangent.w < 0.0f ? -1.0f : 1.0f);
                out.vertices.push_back(v);
            }

            for (cluster c : part.clusters) {

                c.first_index += first_index;
                if (c.depth == 0)
                    out.source_triangles += c.index_count / 3;
                if (c.parent_error >= CLUSTER_ERROR_NONE)
                    out.coarsest_triangles += c.index_count / 3;
                out.clusters.push_back(c);
            }
            for (const u32 record : part.records)
                out.indices.push_back(record < local_count ? source.to_global[record] : first_new_vertex + (record - local_count));
            out.submeshes.push_back({ first_cluster, static_cast<u32>(part.clusters.size()), part.max_visible, part.level_count });
            out.level_count = std::max(out.level_count, part.level_count);
        }

        out.build_ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count();
        return out;
    }


    f64 generate_clusters(mesh_asset& mesh) {

        sanitize(mesh.virtual_geometry);
        cluster_build_output built = build_clusters(mesh.vertices, mesh.indices, mesh.submeshes, mesh.virtual_geometry);

        mesh.clusters = std::move(built.clusters);
        mesh.cluster_indices = std::move(built.indices);
        mesh.cluster_submeshes = std::move(built.submeshes);
        mesh.cluster_vertices = std::move(built.vertices);
        mesh.revision++;

        LOG(info, "generated {} clusters in {} levels in {:.1f} ms ({} triangles, coarsest selection {}, {} extra vertices)", mesh.clusters.size(),
            built.level_count, built.build_ms, built.source_triangles, built.coarsest_triangles, mesh.cluster_vertices.size());
        return built.build_ms;
    }


    void clear_clusters(mesh_asset& mesh) {

        mesh.clusters.clear();
        mesh.cluster_indices.clear();
        mesh.cluster_submeshes.clear();
        mesh.cluster_vertices.clear();
        mesh.clusters.shrink_to_fit();
        mesh.cluster_indices.shrink_to_fit();
        mesh.cluster_vertices.shrink_to_fit();
        mesh.revision++;
    }


    bool set_detail_mode(mesh_asset& mesh, detail_mode mode) {

        if (mesh.detail() == mode)
            return false;

        if (mode == detail_mode::virtual_geometry) {

            mesh.virtual_geometry.flags |= CLUSTER_FLAG_ENABLED;
            mesh.revision++;
        } else {

            mesh.virtual_geometry.flags &= ~static_cast<u32>(CLUSTER_FLAG_ENABLED);
            clear_clusters(mesh);               // bumps the revision
        }
        return true;
    }


    bool validate_clusters(std::span<const cluster> clusters, std::span<const u32> indices,
        std::span<const cluster_submesh> submeshes, size_t vertex_count, size_t submesh_count) {

        if (clusters.empty() || indices.empty() || submeshes.size() != submesh_count)
            return false;
        if (clusters.size() > UINT32_MAX || indices.size() > UINT32_MAX)
            return false;

        // Submesh ranges: back to back, covering every cluster exactly once
        u64 cursor = 0;
        for (size_t s = 0; s < submeshes.size(); ++s) {

            const cluster_submesh& sm = submeshes[s];
            if (sm.first_cluster != cursor)
                return false;
            cursor += sm.cluster_count;
            if (cursor > clusters.size())
                return false;

            for (u32 c = 0; c < sm.cluster_count; ++c)
                if (clusters[sm.first_cluster + c].submesh != s)
                    return false;
        }
        if (cursor != clusters.size())
            return false;

        for (const cluster& c : clusters) {

            if (c.index_count < 3 || c.index_count % 3 != 0 || c.index_count > MAX_CLUSTER_TRIANGLES * 3)
                return false;
            if (static_cast<u64>(c.first_index) + c.index_count > indices.size())
                return false;
            if (!std::isfinite(c.bounds.x + c.bounds.y + c.bounds.z + c.bounds.w)
                || !std::isfinite(c.lod_bounds.x + c.lod_bounds.y + c.lod_bounds.z + c.lod_bounds.w)
                || !std::isfinite(c.parent_bounds.x + c.parent_bounds.y + c.parent_bounds.z + c.parent_bounds.w))
                return false;
            if (!(c.lod_error >= 0.0f) || !(c.parent_error >= c.lod_error))    // also rejects NaN
                return false;
        }

        for (const u32 index : indices)
            if (index >= vertex_count)
                return false;
        return true;
    }


    f32 projected_cluster_error(const glm::vec4& sphere, f32 error, const cluster_view& view, f32 scale) {

        // Same expression as virtual_geometry_cull.comp.glsl: the error seen from the nearest point of the sphere, as a
        // fraction of the viewport height. Rotation invariant on purpose (no popping while the camera turns)
        const glm::vec3 center = glm::vec3(view.model * glm::vec4(glm::vec3(sphere), 1.0f));
        const f32 distance = std::max(glm::distance(center, view.camera_position) - sphere.w * scale, view.near_plane);
        return error * scale * view.error_scale * (view.projection * 0.5f) / distance;
    }


    bool cluster_in_cut(const cluster& value, const cluster_view& view) {

        const f32 scale = std::max({ glm::length(glm::vec3(view.model[0])), glm::length(glm::vec3(view.model[1])),
            glm::length(glm::vec3(view.model[2])) });
        const f32 threshold = view.pixel_error / std::max(view.viewport_height, 1.0f);

        const bool detailed_enough = value.lod_error <= 0.0f
            || projected_cluster_error(value.lod_bounds, value.lod_error, view, scale) <= threshold;
        const bool parent_too_coarse = value.parent_error >= CLUSTER_ERROR_NONE
            || projected_cluster_error(value.parent_bounds, value.parent_error, view, scale) > threshold;
        return detailed_enough && parent_too_coarse;
    }


    cluster_cut measure_cut(std::span<const cluster> clusters, const cluster_view& view) {

        cluster_cut cut{};
        for (const cluster& c : clusters) {

            if (!cluster_in_cut(c, view))
                continue;
            cut.clusters++;
            cut.triangles += c.index_count / 3;
            cut.deepest_level = std::max(cut.deepest_level, c.depth);
        }
        return cut;
    }


    void collect_cut(std::span<const cluster> clusters, const cluster_view& view, std::vector<u32>& out) {

        out.clear();
        for (size_t index = 0; index < clusters.size(); ++index)
            if (cluster_in_cut(clusters[index], view))
                out.push_back(static_cast<u32>(index));
    }


    // Debug colours. Twins of hash() / hsv() / id_color() / level_color() in virtual_geometry_debug.vert.glsl

    static u32 debug_hash(u32 x) noexcept {

        x ^= x >> 16; x *= 0x7feb352du;
        x ^= x >> 15; x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }


    static glm::vec3 debug_hsv(const f32 hue, const f32 saturation, const f32 value) noexcept {

        const glm::vec3 k = glm::clamp(glm::abs(glm::fract(glm::vec3(hue) + glm::vec3(0.0f, 2.0f / 3.0f, 1.0f / 3.0f)) * 6.0f - 3.0f) - 1.0f,
            0.0f, 1.0f);
        return value * glm::mix(glm::vec3(1.0f), k, saturation);
    }


    glm::vec3 cluster_debug_color(const u32 cluster_index) {

        const u32 h = debug_hash(cluster_index + 1u);
        return debug_hsv(static_cast<f32>(h & 0xFFFFu) / 65536.0f, 0.55f + 0.35f * static_cast<f32>((h >> 16) & 0xFFu) / 255.0f,
            0.70f + 0.30f * static_cast<f32>(h >> 24) / 255.0f);
    }


    glm::vec3 cluster_level_color(const u32 depth) {

        return debug_hsv(glm::fract(1.0f / 3.0f - static_cast<f32>(std::min(depth, 15u)) * 0.06f), 0.85f, 0.90f);
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

}
