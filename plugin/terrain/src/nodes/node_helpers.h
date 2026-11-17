
#pragma once

#include <terrain/node.h>
#include <terrain/noise.h>



// Shared by the built-in node files. Every built-in type name starts with "terrain."
namespace GLT::terrain::nodes {

    // FUNCTION DECLARATION ============================================================================================

    void register_generators(i_node_registry& registry, std::string_view owner);
    void register_combine(i_node_registry& registry, std::string_view owner);
    void register_modify(i_node_registry& registry, std::string_view owner);
    void register_masks(i_node_registry& registry, std::string_view owner);
    void register_utility(i_node_registry& registry, std::string_view owner);

    // All of the above
    void register_builtin(i_node_registry& registry, std::string_view owner);

    // helpers ---------------------------------------------------------------------------------------------------------

    [[nodiscard]] inline f32 smoothstep(f32 edge0, f32 edge1, f32 x) noexcept {
        if (edge0 == edge1)
            return x < edge0 ? 0.0f : 1.0f;
        const f32 t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    // 1 inside [lo, hi], fading to 0 over [falloff] outside
    [[nodiscard]] inline f32 soft_range(f32 value, f32 lo, f32 hi, f32 falloff) noexcept {
        if (falloff <= 0.0f)
            return (value >= lo && value <= hi) ? 1.0f : 0.0f;
        return smoothstep(lo - falloff, lo, value) * (1.0f - smoothstep(hi, hi + falloff, value));
    }

    // World distance -> samples at this spacing (rounded up, never negative)
    [[nodiscard]] inline u32 samples_for(f32 world_distance, f64 spacing) noexcept {
        if (!(world_distance > 0.0f) || !(spacing > 0.0))
            return 0;
        // Capped: a huge parameter must not overflow (the evaluator rejects regions above MAX_REGION_SIDE anyway)
        return static_cast<u32>(std::min(std::ceil(static_cast<f64>(world_distance) / spacing), static_cast<f64>(MAX_REGION_SIDE)));
    }

    // Seed of a noise node: the evaluator's per-node seed, shifted by the user's seed parameter
    [[nodiscard]] inline u64 node_seed(const context& ctx, i32 user_seed) noexcept { return hash_combine(ctx.seed(), static_cast<u64>(static_cast<i64>(user_seed))); }

    // Height gradient (central differences, world units). Needs an apron of 1
    [[nodiscard]] inline glm::vec2 gradient(const field& height, i64 gx, i64 gy) noexcept {
        const f32 inv = static_cast<f32>(0.5 / height.area().spacing);
        return glm::vec2((height.at(gx + 1, gy) - height.at(gx - 1, gy)) * inv, (height.at(gx, gy + 1) - height.at(gx, gy - 1)) * inv);
    }

}
