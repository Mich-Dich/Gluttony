#include "util/pch.h"
#include "mesh_lod.h"

#include <future>

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

                const u32 global = indices[sm.first_index + i];
                if (global >= vertices.size()) {
                    local.indices.clear();
                    break;
                }

                u32& mapped = to_local[global];
                if (mapped == UINT32_MAX) {

                    mapped = static_cast<u32>(local.to_global.size());
                    local.to_global.push_back(global);

                    const vertex& v = vertices[global];
                    local.positions.insert(local.positions.end(), { v.position.x, v.position.y, v.position.z });
                    local.attributes.insert(local.attributes.end(), { v.normal.x, v.normal.y, v.normal.z, v.uv0.x, v.uv0.y });
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

        unsigned int options = meshopt_SimplifyErrorAbsolute;          // errors in mesh units, consistent across submeshes
        if (settings.flags & LOD_FLAG_LOCK_BORDERS)
            options |= meshopt_SimplifyLockBorder;
        if (settings.flags & LOD_FLAG_PRUNE)
            options |= meshopt_SimplifyPrune;

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

            for (u32 s = 0; s < submesh_count; ++s) {

                const local_submesh& local = locals[s];
                const size_t count = local.indices.size();
                if (count < 3)
                    continue;

                const size_t target = std::max<size_t>(3, static_cast<size_t>(static_cast<f64>(count) * level_settings.triangle_ratio) / 3 * 3);
                const size_t vertex_count = local.to_global.size();

                f32 submesh_error = target_error;
                unsigned int submesh_options = options;
                if (keep_submeshes) {
                    submesh_error = std::min(target_error, level_settings.max_error * SMALL_PART_ERROR_SCALE * local.extent);
                    if (local.extent < extent * 0.25f)
                        submesh_options &= ~static_cast<unsigned int>(meshopt_SimplifyPrune);   // small part: never prune it away
                }

                scratch.resize(count);
                f32 result_error = 0.0f;
                size_t written = meshopt_simplifyWithAttributes(scratch.data(), local.indices.data(), count,
                    local.positions.data(), vertex_count, sizeof(f32) * 3,
                    local.attributes.data(), sizeof(f32) * ATTRIBUTE_COUNT, attribute_weights, ATTRIBUTE_COUNT,
                    nullptr, target, submesh_error, submesh_options, &result_error);

                written -= written % 3;
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

            out.lods.push_back({ stitched_count, level_error, 0, 0 });
            previous_count = stitched_count;
        }

        out.indices.shrink_to_fit();
        out.build_ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count();
        return out;
    }


    f64 generate_lods(mesh_asset& mesh) {

        sanitize(mesh.lod);
        lod_build_output built = build_lods(mesh.vertices, mesh.indices, mesh.submeshes, mesh.lod);

        mesh.lods = std::move(built.lods);
        mesh.lod_ranges = std::move(built.ranges);
        mesh.lod_indices = std::move(built.indices);

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
        mesh.revision++;
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

}
