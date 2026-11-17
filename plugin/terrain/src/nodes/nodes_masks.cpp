
#include "util/pch.h"
#include "node_helpers.h"



namespace GLT::terrain::nodes {

    namespace {

        struct height_range_node final : node {
            f32 min = 0.0f, max = 100.0f, falloff = 10.0f;
            void evaluate(context& ctx) override {
                const field& in = ctx.input(0);
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) { out = soft_range(in.at(gx, gy), min, max, falloff); });
            }
            static node_type describe() {
                return node_builder<height_range_node>("terrain.height_range", "Height Range", "Masks")
                    .describe("1 between min and max height, soft edges")
                    .input("height", pin_type::heightfield).output("mask", pin_type::mask)
                    .number("Min", &height_range_node::min).number("Max", &height_range_node::max)
                    .number("Falloff", &height_range_node::falloff, 0.0f, 0.0f, "Width of the soft edges (height units)")
                    .build();
            }
        };


        struct slope_range_node final : node {
            f32 min_angle = 0.0f, max_angle = 30.0f, falloff = 5.0f;      // degrees
            [[nodiscard]] u32 apron(f64) const override { return 1; }
            void evaluate(context& ctx) override {
                const field& height = ctx.input(0);
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) {
                    const f32 angle = glm::degrees(std::atan(glm::length(gradient(height, gx, gy))));
                    out = soft_range(angle, min_angle, max_angle, falloff);
                });
            }
            static node_type describe() {
                return node_builder<slope_range_node>("terrain.slope_range", "Slope Range", "Masks")
                    .describe("1 where the terrain is between min and max steepness (degrees)")
                    .input("height", pin_type::heightfield).output("mask", pin_type::mask)
                    .number("Min Angle", &slope_range_node::min_angle, 0.0f, 90.0f)
                    .number("Max Angle", &slope_range_node::max_angle, 0.0f, 90.0f)
                    .number("Falloff", &slope_range_node::falloff, 0.0f, 45.0f, "Degrees")
                    .build();
            }
        };


        struct aspect_node final : node {
            f32 direction = 0.0f;       // degrees, the downhill direction that gives 1 (0 = +x)
            f32 width = 45.0f;
            f32 min_slope = 2.0f;       // degrees: flat ground has no aspect
            [[nodiscard]] u32 apron(f64) const override { return 1; }
            void evaluate(context& ctx) override {
                const field& height = ctx.input(0);
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) {
                    const glm::vec2 g = gradient(height, gx, gy);
                    const f32 slope = glm::degrees(std::atan(glm::length(g)));
                    const f32 facing = glm::degrees(std::atan2(-g.y, -g.x));        // downhill
                    f32 diff = std::abs(std::fmod(facing - direction + 540.0f, 360.0f) - 180.0f);
                    out = (1.0f - smoothstep(width * 0.5f, width, diff)) * smoothstep(0.0f, std::max(min_slope, 1e-3f), slope);
                });
            }
            static node_type describe() {
                return node_builder<aspect_node>("terrain.aspect", "Aspect", "Masks")
                    .describe("1 on slopes facing a direction (sunny side, wind side)")
                    .input("height", pin_type::heightfield).output("mask", pin_type::mask)
                    .number("Direction", &aspect_node::direction, -180.0f, 180.0f, "Downhill direction in degrees (0 = +X)")
                    .number("Width", &aspect_node::width, 1.0f, 360.0f, "Angular width of the facing range")
                    .number("Min Slope", &aspect_node::min_slope, 0.0f, 90.0f, "Flatter ground fades out")
                    .build();
            }
        };


        struct curvature_node final : node {
            i32 mode = 0;               // convex, concave, both (signed, remapped to 0..1)
            f32 scale = 1.0f;
            [[nodiscard]] u32 apron(f64) const override { return 1; }
            void evaluate(context& ctx) override {
                const field& h = ctx.input(0);
                const f32 inv = static_cast<f32>(1.0 / (ctx.area().spacing * ctx.area().spacing));     // laplacian: 1 / h^2
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) {
                    // Negative laplacian: > 0 on ridges and peaks (convex), < 0 in valleys (concave)
                    const f32 lap = (h.at(gx + 1, gy) + h.at(gx - 1, gy) + h.at(gx, gy + 1) + h.at(gx, gy - 1) - 4.0f * h.at(gx, gy)) * inv;
                    const f32 c = -lap * scale;
                    switch (mode) {
                        case 1:  out = std::clamp(-c, 0.0f, 1.0f); break;
                        case 2:  out = std::clamp(c * 0.5f + 0.5f, 0.0f, 1.0f); break;
                        default: out = std::clamp(c, 0.0f, 1.0f); break;
                    }
                });
            }
            static node_type describe() {
                return node_builder<curvature_node>("terrain.curvature", "Curvature", "Masks")
                    .describe("Ridges and peaks (convex) or valleys and gullies (concave)")
                    .input("height", pin_type::heightfield).output("mask", pin_type::mask)
                    .choice("Mode", &curvature_node::mode, { "Convex", "Concave", "Both (0.5 = flat)" })
                    .number("Scale", &curvature_node::scale, 0.0f, 0.0f, "Sensitivity")
                    .build();
            }
        };


        struct threshold_node final : node {
            f32 threshold = 0.5f, softness = 0.05f;
            void evaluate(context& ctx) override {
                const field& in = ctx.input(0);
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) {
                    out = smoothstep(threshold - softness, threshold + softness, in.at(gx, gy));
                });
            }
            static node_type describe() {
                return node_builder<threshold_node>("terrain.threshold", "Threshold", "Masks")
                    .describe("0 below the threshold, 1 above (any field -> mask; noise -> noise mask)")
                    .input("in", pin_type::heightfield).output("mask", pin_type::mask)
                    .number("Threshold", &threshold_node::threshold)
                    .number("Softness", &threshold_node::softness, 0.0f, 0.0f)
                    .build();
            }
        };


        struct mask_combine_node final : node {
            i32 op = 0;                 // and, or, subtract, xor
            void evaluate(context& ctx) override {
                const field& a = ctx.input(0);
                const field& b = ctx.input(1);
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) {
                    const f32 x = std::clamp(a.at(gx, gy), 0.0f, 1.0f), y = std::clamp(b.at(gx, gy), 0.0f, 1.0f);
                    switch (op) {
                        case 1:  out = std::max(x, y); break;
                        case 2:  out = std::max(x - y, 0.0f); break;
                        case 3:  out = std::abs(x - y); break;
                        default: out = std::min(x, y); break;
                    }
                });
            }
            static node_type describe() {
                return node_builder<mask_combine_node>("terrain.mask_combine", "Mask Combine", "Masks")
                    .input("a", pin_type::mask).input("b", pin_type::mask).output("mask", pin_type::mask)
                    .choice("Operation", &mask_combine_node::op, { "And (min)", "Or (max)", "Subtract (a - b)", "Xor (|a - b|)" })
                    .build();
            }
        };


        struct mask_not_node final : node {
            void evaluate(context& ctx) override {
                const field& in = ctx.input(0);
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) { out = 1.0f - std::clamp(in.at(gx, gy), 0.0f, 1.0f); });
            }
            static node_type describe() {
                return node_builder<mask_not_node>("terrain.mask_not", "Mask Not", "Masks")
                    .input("mask", pin_type::mask).output("mask", pin_type::mask)
                    .build();
            }
        };


        // Grow (max filter) or shrink (min filter) a mask by a radius; square window, separable
        struct grow_shrink_node final : node {
            f32 radius = 10.0f;
            i32 mode = 0;               // grow, shrink
            [[nodiscard]] u32 apron(f64 spacing) const override { return std::min(samples_for(radius, spacing), 256u); }
            void evaluate(context& ctx) override {
                const field& in = ctx.input(0);
                field& out = ctx.output(0);
                const i64 r = static_cast<i64>(apron(ctx.area().spacing));
                const bool grow = mode == 0;
                auto pick = [grow](f32 a, f32 b) { return grow ? std::max(a, b) : std::min(a, b); };

                region temp_area = out.area();
                temp_area.y0 -= r;
                temp_area.height += static_cast<u32>(2 * r);
                field temp(temp_area);
                ctx.for_each(temp, [&](i64 gx, i64 gy, glm::dvec2, f32& v) {
                    f32 acc = in.at(gx, gy);
                    for (i64 i = 1; i <= r; ++i)
                        acc = pick(acc, pick(in.at(gx - i, gy), in.at(gx + i, gy)));
                    v = acc;
                });
                ctx.for_each(out, [&](i64 gx, i64 gy, glm::dvec2, f32& v) {
                    f32 acc = temp.at(gx, gy);
                    for (i64 i = 1; i <= r; ++i)
                        acc = pick(acc, pick(temp.at(gx, gy - i), temp.at(gx, gy + i)));
                    v = acc;
                });
            }
            static node_type describe() {
                return node_builder<grow_shrink_node>("terrain.grow_shrink", "Grow / Shrink", "Masks")
                    .describe("Expands (or erodes) the bright areas of a mask")
                    .input("mask", pin_type::mask).output("mask", pin_type::mask)
                    .number("Radius", &grow_shrink_node::radius, 0.0f, 2000.0f, "World units")
                    .choice("Mode", &grow_shrink_node::mode, { "Grow", "Shrink" })
                    .build();
            }
        };


        struct coast_node final : node {
            f32 water_level = 0.0f, width = 5.0f;
            void evaluate(context& ctx) override {
                const field& in = ctx.input(0);
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) {
                    out = 1.0f - smoothstep(0.0f, std::max(width, 1e-6f), std::abs(in.at(gx, gy) - water_level));
                });
            }
            static node_type describe() {
                return node_builder<coast_node>("terrain.coast", "Coast", "Masks")
                    .describe("1 at the water line, fading out above and below (beaches)")
                    .input("height", pin_type::heightfield).output("mask", pin_type::mask)
                    .number("Water Level", &coast_node::water_level)
                    .number("Width", &coast_node::width, 0.0f, 0.0f, "Height distance from the water line")
                    .build();
            }
        };


        struct distance_to_height_node final : node {
            f32 target = 50.0f, range = 20.0f;
            void evaluate(context& ctx) override {
                const field& in = ctx.input(0);
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) {
                    out = std::clamp(1.0f - std::abs(in.at(gx, gy) - target) / std::max(range, 1e-6f), 0.0f, 1.0f);
                });
            }
            static node_type describe() {
                return node_builder<distance_to_height_node>("terrain.distance_to_height", "Distance To Height", "Masks")
                    .describe("1 at the target height, linear falloff to 0 at the range")
                    .input("height", pin_type::heightfield).output("mask", pin_type::mask)
                    .number("Target", &distance_to_height_node::target)
                    .number("Range", &distance_to_height_node::range, 0.0f, 0.0f)
                    .build();
            }
        };


        // Hydrology: everything below the sea level is pulled down to it (softly) and reported as water
        struct sea_level_node final : node {
            f32 level = 0.0f;
            f32 depth = 0.0f;           // how far below the level the sea floor is kept (0 = flatten to the level)
            f32 softness = 2.0f;
            void evaluate(context& ctx) override {
                const field& in = ctx.input(0);
                field& height = ctx.output(0);
                field& water = ctx.output(1);
                ctx.for_each(height, [&](i64 gx, i64 gy, glm::dvec2, f32& out) {
                    const f32 h = in.at(gx, gy);
                    const f32 below = 1.0f - smoothstep(level - softness, level + softness, h);
                    const f32 floor_h = depth > 0.0f ? std::max(h, level - depth) : level;
                    out = glm::mix(h, std::min(h, floor_h), below);
                    water.at(gx, gy) = below;
                });
            }
            static node_type describe() {
                return node_builder<sea_level_node>("terrain.sea_level", "Sea Level", "Hydrology")
                    .describe("Flattens (or limits) the terrain below the sea level and outputs a water mask")
                    .input("height", pin_type::heightfield)
                    .output("height", pin_type::heightfield)
                    .output("water", pin_type::mask)
                    .number("Level", &sea_level_node::level)
                    .number("Depth", &sea_level_node::depth, 0.0f, 0.0f, "Deepest sea floor below the level (0 = flat)")
                    .number("Softness", &sea_level_node::softness, 0.0f, 0.0f)
                    .build();
            }
        };

    }

    void register_masks(i_node_registry& registry, std::string_view owner) {

        registry.add(owner, height_range_node::describe());
        registry.add(owner, slope_range_node::describe());
        registry.add(owner, aspect_node::describe());
        registry.add(owner, curvature_node::describe());
        registry.add(owner, threshold_node::describe());
        registry.add(owner, mask_combine_node::describe());
        registry.add(owner, mask_not_node::describe());
        registry.add(owner, grow_shrink_node::describe());
        registry.add(owner, coast_node::describe());
        registry.add(owner, distance_to_height_node::describe());
        registry.add(owner, sea_level_node::describe());
    }

}
