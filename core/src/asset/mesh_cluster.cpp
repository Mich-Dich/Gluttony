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
    // edges) stay collapsible, otherwise hard-surface meshes stop simplifying at every panel edge
    constexpr unsigned int                              CLUSTER_PROTECT_MASK = (1u << 3) | (1u << 4);

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

    // Clusters of one submesh, before they are stitched into the mesh-wide arrays
    struct cluster_part {

        std::vector<cluster>                            clusters;       // first_index relative to [indices]
        std::vector<u32>                                indices;        // mesh vertex indices
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


    static glm::vec4 to_sphere(const clodBounds& bounds) noexcept {

        return { bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius };
    }


    static cluster_part build_part(const cluster_source& source, const cluster_settings& settings, u32 submesh_index) {
        GLT_PROFILE_SCOPE("mesh::build_clusters submesh");

        cluster_part part{};
        if (source.indices.size() < 3)
            return part;

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
                    part.indices.push_back(source.to_global[member.indices[i]]);
                part.clusters.push_back(out);
            }
            return group_id;
        });

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

        settings.flags &= (CLUSTER_FLAG_ENABLED | CLUSTER_FLAG_LOCK_SEAMS);
        settings.cluster_triangles = std::clamp<u32>(settings.cluster_triangles, MIN_CLUSTER_TRIANGLES, MAX_CLUSTER_TRIANGLES);
        settings.normal_weight = std::isfinite(settings.normal_weight) ? std::clamp(settings.normal_weight, 0.0f, 16.0f) : 0.5f;
        settings.uv_weight = std::isfinite(settings.uv_weight) ? std::clamp(settings.uv_weight, 0.0f, 16.0f) : 0.5f;
        settings.error_scale = std::isfinite(settings.error_scale) ? std::clamp(settings.error_scale, 0.05f, 20.0f) : 1.0f;
        for (u32& reserved : settings._reserved)
            reserved = 0;
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
                    parts[s] = build_part(sources[s], settings, static_cast<u32>(s));
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
        size_t cluster_total = 0, index_total = 0;
        for (const cluster_part& part : parts) {
            cluster_total += part.clusters.size();
            index_total += part.indices.size();
        }
        if (cluster_total > UINT32_MAX || index_total > UINT32_MAX)
            return {};                                              // cannot be addressed with 32-bit offsets

        out.clusters.reserve(cluster_total);
        out.indices.reserve(index_total);
        out.submeshes.reserve(submeshes.size());

        for (const cluster_part& part : parts) {

            const u32 first_cluster = static_cast<u32>(out.clusters.size());
            const u32 first_index = static_cast<u32>(out.indices.size());

            for (cluster c : part.clusters) {

                c.first_index += first_index;
                if (c.depth == 0)
                    out.source_triangles += c.index_count / 3;
                if (c.parent_error >= CLUSTER_ERROR_NONE)
                    out.coarsest_triangles += c.index_count / 3;
                out.clusters.push_back(c);
            }
            out.indices.insert(out.indices.end(), part.indices.begin(), part.indices.end());
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
        mesh.revision++;

        LOG(info, "generated {} clusters in {} levels in {:.1f} ms ({} triangles, coarsest selection {})", mesh.clusters.size(),
            built.level_count, built.build_ms, built.source_triangles, built.coarsest_triangles);
        return built.build_ms;
    }


    void clear_clusters(mesh_asset& mesh) {

        mesh.clusters.clear();
        mesh.cluster_indices.clear();
        mesh.cluster_submeshes.clear();
        mesh.clusters.shrink_to_fit();
        mesh.cluster_indices.shrink_to_fit();
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
