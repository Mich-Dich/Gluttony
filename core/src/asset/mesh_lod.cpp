#include "util/pch.h"
#include "mesh_lod.h"

#include <future>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#include <meshoptimizer.h>

#include "debug/trace.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::mesh {

    // CONSTANTS =======================================================================================================

    // A level that keeps more than this fraction of the previous level's triangles is not worth a switch (the error
    // limit stopped the simplifier): generation ends there
    constexpr f32                                       MIN_LEVEL_REDUCTION = 0.95f;

    constexpr u32                                       ATTRIBUTE_COUNT = 5;            // normal xyz, uv0 xy

    // Keep-submeshes mode: a submesh may not deviate by more than this x the level's max_error relative to ITS OWN size.
    // Big parts are unaffected (their own size ~ the mesh size); small, simple parts (a strut, a landing foot) keep their
    // shape instead of collapsing into a couple of triangles because they are tiny relative to the whole mesh
    constexpr f32                                       SMALL_PART_ERROR_SCALE = 5.0f;

    // A connected part whose extent is at least this x the level's error is a "large feature": its open outline
    // (border vertices) is locked, so thin panels / sheets keep their silhouette instead of shrinking away edge by edge.
    // Smaller parts stay free and collapse / merge with their neighbours
    constexpr f32                                       LARGE_PART_ERROR_SCALE = 4.0f;

    // If edge collapse stops above this x the level's triangle target (topology, locked outlines, many separate parts),
    // the remaining detail is merged by vertex clustering (meshopt_simplifySloppy) bounded by the same error: anything
    // smaller than the error merges into its neighbourhood, anything larger survives
    constexpr f32                                       CLUSTER_FALLBACK_SLACK = 1.25f;

    // Clustering uses a tighter error than edge collapse: its grid snaps whole thin parts (struts, legs, antennas) flat
    // once a cell is wider than they are, so it only gets half the level's budget
    constexpr f32                                       CLUSTER_ERROR_SCALE = 0.5f;

    // "Prune small parts" only removes parts this much smaller than the level error (sub-pixel at the switch distance).
    // Everything else is merged, never deleted
    constexpr f32                                       PRUNE_ERROR_SCALE = 0.25f;

    // Per-vertex classification (bit flags)
    constexpr u8                                        VERTEX_BORDER = 1u << 0;        // on an open edge (position-welded)
    constexpr u8                                        VERTEX_UV_SEAM = 1u << 1;       // same position, different UV

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // One submesh as a self-contained vertex / index set. meshoptimizer's cost is O(vertex_count) per call, so simplifying
    // a 32-submesh mesh against the full vertex buffer would do 32x the work
    struct local_submesh {

        std::vector<u32>                                to_global;      // local vertex -> mesh vertex
        std::vector<f32>                                positions;      // xyz
        std::vector<f32>                                attributes;     // ATTRIBUTE_COUNT per vertex
        std::vector<u32>                                indices;        // local
        f32                                             extent = 0.0f;  // largest AABB side of this submesh

        // Topology, position-welded (attribute seams are not borders): per local vertex
        std::vector<u8>                                 vertex_flags;   // VERTEX_*
        std::vector<f32>                                part_extent;    // largest AABB side of the connected part it belongs to
        std::vector<u32>                                part;           // connected part id (a local vertex index)
    };

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static f32 max_extent(const GLT::AABB& bounds) noexcept {

        const glm::vec3 e = bounds.max - bounds.min;
        return std::max({ e.x, e.y, e.z, 1e-6f });
    }


    // Border vertices, UV seams and connected parts of one submesh, all on the position-welded mesh
    static void classify_topology(local_submesh& local) {

        const size_t vertex_count = local.to_global.size();
        local.vertex_flags.assign(vertex_count, 0u);
        local.part_extent.assign(vertex_count, 0.0f);
        local.part.assign(vertex_count, 0u);
        if (vertex_count == 0 || local.indices.size() < 3)
            return;

        std::vector<u32> weld(vertex_count);
        meshopt_generatePositionRemap(weld.data(), local.positions.data(), vertex_count, sizeof(f32) * 3);

        // UV seams: a welded vertex whose members disagree on UV
        for (size_t v = 0; v < vertex_count; ++v) {
            const u32 w = weld[v];
            if (w == v)
                continue;
            const f32* a = &local.attributes[v * ATTRIBUTE_COUNT + 3];
            const f32* b = &local.attributes[static_cast<size_t>(w) * ATTRIBUTE_COUNT + 3];
            if (std::abs(a[0] - b[0]) > 1e-5f || std::abs(a[1] - b[1]) > 1e-5f) {
                local.vertex_flags[v] |= VERTEX_UV_SEAM;
                local.vertex_flags[w] |= VERTEX_UV_SEAM;
            }
        }

        // Open edges: welded edges used by exactly one triangle
        const size_t triangle_count = local.indices.size() / 3;
        std::vector<u64> edges;
        edges.reserve(triangle_count * 3);
        for (size_t t = 0; t < triangle_count; ++t)
            for (u32 e = 0; e < 3; ++e) {
                const u32 a = weld[local.indices[t * 3 + e]];
                const u32 b = weld[local.indices[t * 3 + (e + 1) % 3]];
                if (a != b)
                    edges.push_back((static_cast<u64>(std::min(a, b)) << 32) | std::max(a, b));
            }
        std::sort(edges.begin(), edges.end());
        std::vector<u8> welded_border(vertex_count, 0u);
        for (size_t i = 0; i < edges.size(); ) {
            size_t j = i + 1;
            while (j < edges.size() && edges[j] == edges[i])
                ++j;
            if (j - i == 1) {
                welded_border[static_cast<u32>(edges[i] >> 32)] = 1u;
                welded_border[static_cast<u32>(edges[i] & 0xffffffffu)] = 1u;
            }
            i = j;
        }

        // Connected parts (union-find over welded vertices) and their extents
        std::vector<u32> parent(vertex_count);
        for (size_t v = 0; v < vertex_count; ++v)
            parent[v] = static_cast<u32>(v);
        auto find = [&parent](u32 x) {
            while (parent[x] != x) {
                parent[x] = parent[parent[x]];
                x = parent[x];
            }
            return x;
        };
        for (size_t t = 0; t < triangle_count; ++t) {
            const u32 a = find(weld[local.indices[t * 3 + 0]]);
            const u32 b = find(weld[local.indices[t * 3 + 1]]);
            const u32 c = find(weld[local.indices[t * 3 + 2]]);
            parent[b] = a;
            parent[find(c)] = a;
        }

        std::vector<glm::vec3> part_min(vertex_count, glm::vec3(std::numeric_limits<f32>::max()));
        std::vector<glm::vec3> part_max(vertex_count, glm::vec3(std::numeric_limits<f32>::lowest()));
        for (size_t v = 0; v < vertex_count; ++v) {
            const u32 root = find(weld[v]);
            const glm::vec3 p(local.positions[v * 3], local.positions[v * 3 + 1], local.positions[v * 3 + 2]);
            part_min[root] = glm::min(part_min[root], p);
            part_max[root] = glm::max(part_max[root], p);
        }
        for (size_t v = 0; v < vertex_count; ++v) {
            const u32 root = find(weld[v]);
            const glm::vec3 e = part_max[root] - part_min[root];
            local.part_extent[v] = std::max({ e.x, e.y, e.z });
            local.part[v] = root;
            if (welded_border[weld[v]])
                local.vertex_flags[v] |= VERTEX_BORDER;
        }
    }


    // Clustering does not respect parts: a long, thin part (a landing leg, a strut, an antenna) is snapped flat once
    // the grid is wider than it is thick, although its length is far above the error. Every part larger than the error
    // whose clustered footprint shrank below half of its collapsed footprint gets its edge-collapse triangles back.
    // [collapsed] holds [collapsed_count] indices on entry and the merged result on exit; returns the new count
    static size_t keep_large_parts(const local_submesh& local, std::vector<u32>& collapsed, size_t collapsed_count,
        const std::vector<u32>& clustered, f32 error) {

        struct box {
            glm::vec3 mn{ std::numeric_limits<f32>::max() };
            glm::vec3 mx{ std::numeric_limits<f32>::lowest() };
            f32 extent() const { return mx.x < mn.x ? 0.0f : std::max({ mx.x - mn.x, mx.y - mn.y, mx.z - mn.z }); }
        };
        std::unordered_map<u32, box> before, after;
        auto grow = [&](std::unordered_map<u32, box>& boxes, const u32* tri) {
            const u32 part = local.part[tri[0]];
            if (local.part_extent[tri[0]] < error)
                return;                                         // small part: merging it is what clustering is for
            box& b = boxes[part];
            for (u32 k = 0; k < 3; ++k) {
                const glm::vec3 p(local.positions[tri[k] * 3], local.positions[tri[k] * 3 + 1], local.positions[tri[k] * 3 + 2]);
                b.mn = glm::min(b.mn, p);
                b.mx = glm::max(b.mx, p);
            }
        };
        for (size_t t = 0; t + 2 < collapsed_count; t += 3) grow(before, &collapsed[t]);
        for (size_t t = 0; t + 2 < clustered.size(); t += 3) grow(after, &clustered[t]);

        std::unordered_set<u32> restore;
        for (const auto& [part, b] : before) {
            const auto it = after.find(part);
            const f32 kept = it == after.end() ? 0.0f : it->second.extent();
            if (kept < 0.5f * b.extent())
                restore.insert(part);
        }

        std::vector<u32> merged;
        merged.reserve(clustered.size());
        for (size_t t = 0; t + 2 < clustered.size(); t += 3)
            if (!restore.contains(local.part[clustered[t]]))
                merged.insert(merged.end(), { clustered[t], clustered[t + 1], clustered[t + 2] });
        if (!restore.empty())
            for (size_t t = 0; t + 2 < collapsed_count; t += 3)
                if (restore.contains(local.part[collapsed[t]]))
                    merged.insert(merged.end(), { collapsed[t], collapsed[t + 1], collapsed[t + 2] });

        if (merged.size() > collapsed_count)
            return collapsed_count;                             // no gain: keep the plain collapse result
        std::copy(merged.begin(), merged.end(), collapsed.begin());
        return merged.size();
    }


    // Gives the triangles [indices, indices + count) of one level / submesh their own normals. Corners are welded by
    // position; a corner's normal is the area-weighted sum of the faces around its position that are within the
    // smoothing angle of its own face (flat: only its own face, smooth: all of them). New vertices copy everything else
    // from the vertex they replace (UV, tangent re-orthogonalised) and are deduplicated, appended to [lod_vertices], and
    // the indices are rewritten to address [source ++ lod_vertices]
    static void recompute_normals(std::span<const vertex> source, std::vector<vertex>& lod_vertices, u32* indices, size_t count,
        lod_normals_mode mode, f32 cos_angle) {

        const size_t triangle_count = count / 3;
        if (triangle_count == 0 || mode == lod_normals_mode::keep)
            return;

        const u32 source_count = static_cast<u32>(source.size());
        auto fetch = [&](u32 i) -> vertex {
            return i < source_count ? source[i] : lod_vertices[i - source_count];
        };

        // Face normals (area weighted + unit)
        std::vector<glm::vec3> weighted(triangle_count), unit(triangle_count);
        for (size_t t = 0; t < triangle_count; ++t) {
            const glm::vec3 a = fetch(indices[t * 3 + 0]).position;
            const glm::vec3 b = fetch(indices[t * 3 + 1]).position;
            const glm::vec3 c = fetch(indices[t * 3 + 2]).position;
            const glm::vec3 n = glm::cross(b - a, c - a);
            const f32 len = glm::length(n);
            weighted[t] = n;
            unit[t] = (len > 1e-20f && std::isfinite(len)) ? n / len : glm::vec3(0.0f);
        }

        // Weld corners by exact position -> incident faces per position (CSR)
        struct position_hash {
            size_t operator()(const glm::vec3& p) const noexcept {
                u32 x, y, z;
                std::memcpy(&x, &p.x, 4); std::memcpy(&y, &p.y, 4); std::memcpy(&z, &p.z, 4);
                return (static_cast<size_t>(x) * 73856093u) ^ (static_cast<size_t>(y) * 19349663u) ^ (static_cast<size_t>(z) * 83492791u);
            }
        };
        std::unordered_map<glm::vec3, u32, position_hash> position_ids;
        position_ids.reserve(count);
        std::vector<u32> corner_position(count);
        for (size_t c = 0; c < count; ++c) {
            const glm::vec3 p = fetch(indices[c]).position;
            const auto [it, inserted] = position_ids.try_emplace(p, static_cast<u32>(position_ids.size()));
            corner_position[c] = it->second;
        }
        std::vector<u32> offsets(position_ids.size() + 1, 0u);
        for (size_t c = 0; c < count; ++c)
            ++offsets[corner_position[c] + 1];
        for (size_t i = 1; i < offsets.size(); ++i)
            offsets[i] += offsets[i - 1];
        std::vector<u32> faces(count);
        {
            std::vector<u32> cursor(offsets.begin(), offsets.end() - 1);
            for (size_t c = 0; c < count; ++c)
                faces[cursor[corner_position[c]]++] = static_cast<u32>(c / 3);
        }

        // New vertices, deduplicated by (position, UV, quantised normal, handedness): corners that end up identical share
        // one vertex even if they came from different source vertices (seams the simplifier collapsed)
        struct vertex_key {
            u32 position; u32 sign; u64 uv; u64 normal;
            bool operator==(const vertex_key&) const = default;
        };
        struct key_hash {
            size_t operator()(const vertex_key& k) const noexcept {
                u64 h = k.normal * 0x9E3779B97F4A7C15ull;
                h ^= (k.uv + 0x7F4A7C15ull + (h << 6) + (h >> 2));
                h ^= (static_cast<u64>(k.position) << 1 | k.sign) * 0xC2B2AE3D27D4EB4Full;
                return static_cast<size_t>(h ^ (h >> 31));
            }
        };
        std::unordered_map<vertex_key, u32, key_hash> created;
        created.reserve(count);
        auto quantise = [](const glm::vec3& n) {
            auto q = [](f32 x) { return static_cast<u64>(static_cast<u32>(static_cast<i32>(std::lround(std::clamp(x, -1.0f, 1.0f) * 32767.0f)) & 0xffff)); };
            return q(n.x) | (q(n.y) << 16) | (q(n.z) << 32);
        };

        for (size_t c = 0; c < count; ++c) {

            const size_t t = c / 3;
            const vertex original = fetch(indices[c]);
            glm::vec3 n(0.0f);
            if (mode == lod_normals_mode::flat || unit[t] == glm::vec3(0.0f)) {
                n = unit[t];
            } else {
                const u32 p = corner_position[c];
                for (u32 i = offsets[p]; i < offsets[p + 1]; ++i) {
                    const u32 f = faces[i];
                    if (mode == lod_normals_mode::smooth || glm::dot(unit[f], unit[t]) >= cos_angle)
                        n += weighted[f];
                }
            }
            const f32 len = glm::length(n);
            n = (len > 1e-20f && std::isfinite(len)) ? n / len : original.normal;      // degenerate face: keep the old one

            u32 uvx, uvy;
            std::memcpy(&uvx, &original.uv0.x, 4);
            std::memcpy(&uvy, &original.uv0.y, 4);
            const vertex_key key{ corner_position[c], original.tangent.w < 0.0f ? 1u : 0u,
                static_cast<u64>(uvx) | (static_cast<u64>(uvy) << 32), quantise(n) };
            const auto [it, inserted] = created.try_emplace(key, 0u);
            if (inserted) {
                vertex v = original;
                v.normal = n;
                glm::vec3 tangent = glm::vec3(v.tangent) - n * glm::dot(n, glm::vec3(v.tangent));
                const f32 tl = glm::length(tangent);
                if (!(tl > 1e-12f) || !std::isfinite(tl)) {
                    const glm::vec3 axis = std::abs(n.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
                    tangent = glm::normalize(glm::cross(axis, n));
                } else {
                    tangent /= tl;
                }
                v.tangent = glm::vec4(tangent, v.tangent.w < 0.0f ? -1.0f : 1.0f);
                it->second = source_count + static_cast<u32>(lod_vertices.size());
                lod_vertices.push_back(v);
            }
            indices[c] = it->second;
        }
    }


    static std::vector<local_submesh> split_submeshes(std::span<const vertex> vertices, std::span<const u32> indices,
        std::span<const submesh> submeshes) {

        std::vector<local_submesh> out(submeshes.size());
        std::vector<u32> to_local(vertices.size(), UINT32_MAX);

        for (size_t s = 0; s < submeshes.size(); ++s) {

            const submesh& sm = submeshes[s];
            local_submesh& local = out[s];
            if (static_cast<u64>(sm.first_index) + sm.index_count > indices.size())
                continue;                                               // malformed range: leave empty, level gets 0 indices

            local.indices.reserve(sm.index_count);
            for (u32 i = 0; i < sm.index_count; ++i) {

                // A triangle with an out-of-range index is skipped (it used to drop the WHOLE submesh from every LOD)
                if (i % 3 == 0) {
                    const u32 first = sm.first_index + i;
                    if (i + 2 >= sm.index_count || indices[first] >= vertices.size() || indices[first + 1] >= vertices.size()
                        || indices[first + 2] >= vertices.size()) {
                        i += 2;
                        continue;
                    }
                }

                const u32 global = indices[sm.first_index + i];
                u32& mapped = to_local[global];
                if (mapped == UINT32_MAX) {

                    mapped = static_cast<u32>(local.to_global.size());
                    local.to_global.push_back(global);

                    const vertex& v = vertices[global];
                    local.positions.insert(local.positions.end(), { v.position.x, v.position.y, v.position.z });
                    // NaN attributes (assimp tangent space / degenerate faces) would poison the error metric
                    auto clean = [](f32 x) { return std::isfinite(x) ? x : 0.0f; };
                    local.attributes.insert(local.attributes.end(),
                        { clean(v.normal.x), clean(v.normal.y), clean(v.normal.z), clean(v.uv0.x), clean(v.uv0.y) });
                }
                local.indices.push_back(mapped);
            }

            glm::vec3 mn(std::numeric_limits<f32>::max()), mx(std::numeric_limits<f32>::lowest());
            for (u32 global : local.to_global) {
                mn = glm::min(mn, vertices[global].position);
                mx = glm::max(mx, vertices[global].position);
            }
            if (!local.to_global.empty())
                local.extent = std::max({ mx.x - mn.x, mx.y - mn.y, mx.z - mn.z });

            // Reset only the entries this submesh touched (submeshes usually do not share vertices, but they may)
            for (u32 global : local.to_global)
                to_local[global] = UINT32_MAX;

            classify_topology(local);
        }
        return out;
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    void sanitize(lod_settings& settings) {

        settings.lod_count = std::clamp<u32>(settings.lod_count, 1u, MAX_LODS);
        settings.shadow_lod_bias = std::min<u32>(settings.shadow_lod_bias, MAX_LODS - 1);
        settings.cull_screen_size = std::clamp(settings.cull_screen_size, 0.0f, 1.0f);
        settings.pixel_error = std::clamp(settings.pixel_error, 0.05f, 64.0f);
        settings.normal_weight = std::clamp(settings.normal_weight, 0.0f, 16.0f);
        settings.uv_weight = std::clamp(settings.uv_weight, 0.0f, 16.0f);
        settings.smooth_angle = std::isfinite(settings.smooth_angle) ? std::clamp(settings.smooth_angle, 0.0f, 180.0f) : 45.0f;

        settings.levels[0] = { 1.0f, 1.0f, 0.0f, 0 };
        for (u32 i = 1; i < MAX_LODS; ++i) {

            lod_level_settings& level = settings.levels[i];
            const lod_level_settings& previous = settings.levels[i - 1];
            level.triangle_ratio = std::clamp(level.triangle_ratio, 0.001f, previous.triangle_ratio);
            level.max_error = std::clamp(level.max_error, previous.max_error, 1.0f);
            level.screen_size = std::clamp(level.screen_size, 0.0001f, previous.screen_size);
            level._pad = 0;
        }
    }


    void fit_screen_sizes(lod_settings& settings, std::span<const lod_level> lods, const GLT::AABB& bounds) {

        // Screen size s = sphere diameter / viewport height. At size s a world length L covers L * H * s / (2 r) pixels,
        // so the level's error stays below [pixel_error] pixels while s <= 2 r px / (error_world H)
        const f32 radius = std::max(lod_sphere_radius(bounds), 1e-6f);
        const f32 extent = max_extent(bounds);
        const f32 pixels = std::max(settings.pixel_error, 0.05f);

        f32 previous = 1.0f;
        for (u32 i = 1; i < MAX_LODS; ++i) {

            f32 size = previous;
            if (i < lods.size()) {
                const f32 error_world = std::max(lods[i].error, 1e-7f) * extent;
                size = (2.0f * radius * pixels) / (error_world * LOD_REFERENCE_SCREEN_HEIGHT);
            } else {
                size = previous * 0.5f;                                 // ungenerated levels: keep a sane, monotonic value
            }

            size = std::clamp(size, 0.0001f, previous);
            settings.levels[i].screen_size = size;
            previous = size;
        }
    }


    lod_build_output build_lods(std::span<const vertex> vertices, std::span<const u32> indices,
        std::span<const submesh> submeshes, const lod_settings& in_settings) {
        GLT_PROFILE_SCOPE("mesh::build_lods");

        const auto start = std::chrono::steady_clock::now();

        lod_settings settings = in_settings;
        sanitize(settings);

        lod_build_output out{};
        out.requested = settings.lod_count;

        const u32 submesh_count = static_cast<u32>(submeshes.size());
        u32 source_index_count = 0;
        for (const submesh& sm : submeshes)
            source_index_count += sm.index_count;

        out.lods.push_back({ source_index_count, 0.0f, 0, 0 });
        out.ranges.reserve(static_cast<size_t>(settings.lod_count) * submesh_count);
        for (const submesh& sm : submeshes)
            out.ranges.push_back({ sm.first_index, sm.index_count });

        if (settings.lod_count <= 1 || vertices.empty() || indices.empty() || submeshes.empty())
            return out;

        GLT::AABB bounds{};
        bounds.min = bounds.max = vertices.front().position;
        for (const vertex& v : vertices) {
            bounds.min = glm::min(bounds.min, v.position);
            bounds.max = glm::max(bounds.max, v.position);
        }
        const f32 extent = max_extent(bounds);

        const std::vector<local_submesh> locals = split_submeshes(vertices, indices, submeshes);

        const f32 attribute_weights[ATTRIBUTE_COUNT] = {
            settings.normal_weight, settings.normal_weight, settings.normal_weight, settings.uv_weight, settings.uv_weight,
        };

        const bool keep_submeshes = (settings.flags & LOD_FLAG_ALLOW_SUBMESH_REMOVAL) == 0;

        // Errors in mesh units (consistent across submeshes). Permissive: hard-surface meshes are full of hard-normal
        // seams that would otherwise stop every collapse at panel edges; UV seams stay protected (no texture smearing).
        // No meshopt_SimplifyPrune: it deletes whole parts up to the error size; small parts are merged instead (below)
        const unsigned int options = meshopt_SimplifyErrorAbsolute | meshopt_SimplifyPermissive;
        const bool lock_all_borders = (settings.flags & LOD_FLAG_LOCK_BORDERS) != 0;
        const bool prune_tiny_parts = (settings.flags & LOD_FLAG_PRUNE) != 0;

        // Every level is simplified from LOD 0 (exact error, no accumulation), so the levels are independent: build them
        // in parallel and stitch them together in order afterwards
        struct level_result {
            std::vector<u32>                            indices;        // mesh vertex indices, submeshes back to back
            std::vector<u32>                            counts;         // per submesh
            f32                                         error = 0.0f;
        };

        auto simplify_level = [&](u32 level) -> level_result {
            GLT_PROFILE_SCOPE("mesh::build_lods level");

            const lod_level_settings& level_settings = settings.levels[level];
            const f32 target_error = level_settings.max_error * extent;

            level_result result{};
            result.counts.resize(submesh_count, 0);
            std::vector<u32> scratch;
            std::vector<u32> pruned;
            std::vector<u32> clustered;
            std::vector<u8> lock;

            for (u32 s = 0; s < submesh_count; ++s) {

                const local_submesh& local = locals[s];
                const size_t count = local.indices.size();
                if (count < 3)
                    continue;

                const size_t target = std::max<size_t>(3, static_cast<size_t>(static_cast<f64>(count) * level_settings.triangle_ratio) / 3 * 3);
                const size_t vertex_count = local.to_global.size();

                f32 submesh_error = target_error;
                if (keep_submeshes)
                    submesh_error = std::min(target_error, level_settings.max_error * SMALL_PART_ERROR_SCALE * local.extent);

                // Locks: outlines of large parts (all outlines with "Lock borders"), UV seams protected
                lock.assign(vertex_count, 0u);
                const f32 large_part = submesh_error * LARGE_PART_ERROR_SCALE;
                for (size_t v = 0; v < vertex_count; ++v) {
                    const u8 flags = local.vertex_flags[v];
                    if ((flags & VERTEX_BORDER) && (lock_all_borders || local.part_extent[v] >= large_part))
                        lock[v] |= meshopt_SimplifyVertex_Lock;
                    if (flags & VERTEX_UV_SEAM)
                        lock[v] |= meshopt_SimplifyVertex_Protect;
                }

                // Optional: drop parts far below the error (sub-pixel where this level is used), never larger ones
                const u32* source = local.indices.data();
                size_t source_count = count;
                f32 prune_error = 0.0f;
                if (prune_tiny_parts && local.extent > 0.0f) {
                    pruned.resize(count);
                    const f32 prune_relative = std::clamp(submesh_error * PRUNE_ERROR_SCALE / local.extent, 0.0f, 1.0f);
                    const size_t kept = meshopt_simplifyPrune(pruned.data(), local.indices.data(), count,
                        local.positions.data(), vertex_count, sizeof(f32) * 3, prune_relative);
                    if (kept >= 3) {
                        source = pruned.data();
                        source_count = kept - kept % 3;
                    }
                    if (kept < count)
                        prune_error = submesh_error * PRUNE_ERROR_SCALE;     // removed parts count as error (screen sizes)
                }

                const size_t source_target = std::min(target, source_count);

                // 1. Attribute-aware edge collapse, bounded by the error
                scratch.resize(source_count);
                f32 result_error = 0.0f;
                size_t written = meshopt_simplifyWithAttributes(scratch.data(), source, source_count,
                    local.positions.data(), vertex_count, sizeof(f32) * 3,
                    local.attributes.data(), sizeof(f32) * ATTRIBUTE_COUNT, attribute_weights, ATTRIBUTE_COUNT,
                    lock.data(), source_target, submesh_error, options, &result_error);
                written -= written % 3;

                // 2. Collapse stalled well above the target: merge the remaining small detail by clustering. The error
                // bound (relative to this submesh for the sloppy simplifier) keeps every feature larger than it, and the
                // large parts' outlines stay locked
                if (written > static_cast<size_t>(static_cast<f32>(source_target) * CLUSTER_FALLBACK_SLACK) && local.extent > 0.0f) {
                    for (u8& l : lock)
                        l = (l & meshopt_SimplifyVertex_Lock) ? 1u : 0u;
                    clustered.resize(written);
                    f32 cluster_error = 0.0f;
                    const f32 relative = std::clamp(submesh_error * CLUSTER_ERROR_SCALE / local.extent, 0.0f, 1.0f);
                    size_t merged = meshopt_simplifySloppy(clustered.data(), scratch.data(), written,
                        local.positions.data(), vertex_count, sizeof(f32) * 3, lock.data(), source_target, relative, &cluster_error);
                    merged -= merged % 3;
                    if (merged >= 3 && merged < written) {
                        clustered.resize(merged);
                        written = keep_large_parts(local, scratch, written, clustered, submesh_error);
                        result_error = std::max(result_error, cluster_error * local.extent);
                    }
                }

                result_error = std::max(result_error, prune_error);
                if (written > 0)
                    meshopt_optimizeVertexCache(scratch.data(), scratch.data(), written, vertex_count);

                for (size_t i = 0; i < written; ++i)
                    result.indices.push_back(local.to_global[scratch[i]]);

                result.counts[s] = static_cast<u32>(written);
                result.error = std::max(result.error, result_error / extent);
            }
            return result;
        };

        std::vector<std::future<level_result>> jobs;
        for (u32 level = 1; level < settings.lod_count; ++level)
            jobs.push_back(std::async(std::launch::async, simplify_level, level));

        const u32 base_index = static_cast<u32>(indices.size());     // LOD 1+ live after the source indices
        const lod_normals_mode normals_mode = static_cast<lod_normals_mode>((settings.flags & LOD_FLAG_NORMALS_MASK) >> LOD_FLAG_NORMALS_SHIFT);
        const f32 cos_smooth = std::cos(glm::radians(std::clamp(settings.smooth_angle, 0.0f, 180.0f)));
        u32 previous_count = source_index_count;
        bool stalled = false;

        for (auto& job : jobs) {

            level_result result = job.get();                            // always join every job, even after a stall
            if (stalled)
                continue;

            const u32 level_count = static_cast<u32>(result.indices.size());
            if (level_count == 0 || static_cast<f32>(level_count) > static_cast<f32>(previous_count) * MIN_LEVEL_REDUCTION) {

                // Stalled at the error limit (or collapsed to nothing): this level and every following one would not
                // pay for a switch. Drop them
                stalled = true;
                continue;
            }

            // Stitch the level in submesh order. A submesh the simplifier removed completely (small part below the error
            // limit, or pruned) keeps the previous level's geometry instead, unless removal is explicitly allowed
            const size_t previous_ranges = out.ranges.size() - submesh_count;
            const size_t level_start = out.indices.size();
            size_t cursor = 0;
            f32 level_error = result.error;
            for (u32 s = 0; s < submesh_count; ++s) {

                const u32 first = base_index + static_cast<u32>(out.indices.size());
                const u32 count = result.counts[s];

                if (count == 0 && keep_submeshes && locals[s].indices.size() >= 3) {

                    const lod_range previous = out.ranges[previous_ranges + s];
                    for (u32 i = 0; i < previous.index_count; ++i) {
                        const u32 position = previous.first_index + i;
                        const u32 value = position < base_index ? indices[position] : out.indices[position - base_index];
                        out.indices.push_back(value);
                    }
                    out.ranges.push_back({ first, previous.index_count });
                    level_error = std::max(level_error, out.lods.back().error);
                } else {

                    out.indices.insert(out.indices.end(), result.indices.begin() + static_cast<std::ptrdiff_t>(cursor),
                        result.indices.begin() + static_cast<std::ptrdiff_t>(cursor + count));
                    out.ranges.push_back({ first, count });
                }
                cursor += count;
            }

            const u32 stitched_count = static_cast<u32>(out.indices.size() - level_start);
            if (static_cast<f32>(stitched_count) > static_cast<f32>(previous_count) * MIN_LEVEL_REDUCTION) {

                out.indices.resize(level_start);                    // the kept parts ate the reduction: stop here
                out.ranges.resize(previous_ranges + submesh_count);
                stalled = true;
                continue;
            }

            // Normals of this level (its stitched index range, per submesh so material borders stay hard)
            if (normals_mode != lod_normals_mode::keep) {
                for (u32 s = 0; s < submesh_count; ++s) {
                    const lod_range range = out.ranges[previous_ranges + submesh_count + s];
                    recompute_normals(vertices, out.vertices, out.indices.data() + (range.first_index - base_index),
                        range.index_count, normals_mode, cos_smooth);
                }
            }

            out.lods.push_back({ stitched_count, level_error, 0, 0 });
            previous_count = stitched_count;
        }

        out.indices.shrink_to_fit();
        out.vertices.shrink_to_fit();
        out.build_ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count();
        return out;
    }


    f64 generate_lods(mesh_asset& mesh) {

        sanitize(mesh.lod);
        lod_build_output built = build_lods(mesh.vertices, mesh.indices, mesh.submeshes, mesh.lod);

        mesh.lods = std::move(built.lods);
        mesh.lod_ranges = std::move(built.ranges);
        mesh.lod_indices = std::move(built.indices);
        mesh.lod_vertices = std::move(built.vertices);

        if (mesh.lod.flags & LOD_FLAG_AUTO_SCREEN_SIZE)
            fit_screen_sizes(mesh.lod, mesh.lods, mesh.bounds);

        mesh.revision++;

        LOG(info, "generated {} of {} LOD levels in {:.1f} ms ({} -> {} triangles)", mesh.lods.size(), built.requested,
            built.build_ms, mesh.indices.size() / 3, mesh.lods.empty() ? 0u : mesh.lods.back().index_count / 3);
        return built.build_ms;
    }


    void clear_lods(mesh_asset& mesh) {

        mesh.lods.clear();
        mesh.lod_ranges.clear();
        mesh.lod_indices.clear();
        mesh.lod_vertices.clear();
        mesh.revision++;
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

}
