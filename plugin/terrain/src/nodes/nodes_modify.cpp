
#include "util/pch.h"
#include "node_helpers.h"



namespace GLT::terrain::nodes {

    namespace {

        // per-sample ------------------------------------------------------------------------------------------------------

        struct remap_node final : node {
            f32 in_min = 0.0f, in_max = 1.0f, out_min = 0.0f, out_max = 100.0f;
            bool clamp = true;
            void evaluate(context& ctx) override {
                const field& in = ctx.input(0);
                const f32 span = in_max - in_min;
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) {
                    f32 t = span != 0.0f ? (in.at(gx, gy) - in_min) / span : 0.0f;
                    if (clamp)
                        t = std::clamp(t, 0.0f, 1.0f);
                    out = out_min + (out_max - out_min) * t;
                });
            }
            static node_type describe() {
                return node_builder<remap_node>("terrain.remap", "Remap", "Modify")
                    .describe("Maps [in min, in max] linearly to [out min, out max]")
                    .input("in", pin_type::heightfield).output("out", pin_type::heightfield)
                    .number("In Min", &remap_node::in_min).number("In Max", &remap_node::in_max)
                    .number("Out Min", &remap_node::out_min).number("Out Max", &remap_node::out_max)
                    .toggle("Clamp", &remap_node::clamp, "Values outside the input range stay at the ends")
                    .build();
            }
        };


        struct clamp_node final : node {
            f32 min = 0.0f, max = 1.0f;
            void evaluate(context& ctx) override {
                const field& in = ctx.input(0);
                const f32 lo = std::min(min, max), hi = std::max(min, max);
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) { out = std::clamp(in.at(gx, gy), lo, hi); });
            }
            static node_type describe() {
                return node_builder<clamp_node>("terrain.clamp", "Clamp", "Modify")
                    .input("in", pin_type::heightfield).output("out", pin_type::heightfield)
                    .number("Min", &clamp_node::min).number("Max", &clamp_node::max)
                    .build();
            }
        };


        struct invert_node final : node {
            f32 pivot = 0.0f;
            void evaluate(context& ctx) override {
                const field& in = ctx.input(0);
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) { out = 2.0f * pivot - in.at(gx, gy); });
            }
            static node_type describe() {
                return node_builder<invert_node>("terrain.invert", "Invert", "Modify")
                    .describe("Mirrors around the pivot: mountains become valleys (pivot 0.5 inverts a 0..1 mask)")
                    .input("in", pin_type::heightfield).output("out", pin_type::heightfield)
                    .number("Pivot", &invert_node::pivot)
                    .build();
            }
        };


        struct abs_node final : node {
            void evaluate(context& ctx) override {
                const field& in = ctx.input(0);
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) { out = std::abs(in.at(gx, gy)); });
            }
            static node_type describe() {
                return node_builder<abs_node>("terrain.abs", "Abs", "Modify")
                    .input("in", pin_type::heightfield).output("out", pin_type::heightfield)
                    .build();
            }
        };


        struct power_node final : node {
            f32 exponent = 2.0f;
            f32 scale = 1.0f;       // values are normalized by this before the power (height 0..scale -> 0..1)
            void evaluate(context& ctx) override {
                const field& in = ctx.input(0);
                const f32 s = std::abs(scale) > 1e-12f ? scale : 1.0f;
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) {
                    const f32 v = in.at(gx, gy) / s;
                    out = std::copysign(std::pow(std::abs(v), exponent), v) * s;
                });
            }
            static node_type describe() {
                return node_builder<power_node>("terrain.power", "Power", "Modify")
                    .describe("Reshapes heights: exponent > 1 flattens lowlands and sharpens peaks (sign kept)")
                    .input("in", pin_type::heightfield).output("out", pin_type::heightfield)
                    .number("Exponent", &power_node::exponent, 0.05f, 10.0f)
                    .number("Scale", &power_node::scale, 0.0f, 0.0f, "Height that maps to 1 before the power is applied")
                    .build();
            }
        };


        struct quantize_node final : node {
            f32 step = 10.0f;
            void evaluate(context& ctx) override {
                const field& in = ctx.input(0);
                const f32 s = std::max(std::abs(step), 1e-6f);
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) { out = std::floor(in.at(gx, gy) / s) * s; });
            }
            static node_type describe() {
                return node_builder<quantize_node>("terrain.quantize", "Quantize", "Modify")
                    .describe("Hard steps of a fixed height")
                    .input("in", pin_type::heightfield).output("out", pin_type::heightfield)
                    .number("Step", &quantize_node::step, 0.001f, 10000.0f)
                    .build();
            }
        };


        struct terrace_node final : node {
            f32 step = 20.0f;
            f32 smoothness = 0.3f;      // 0 = hard terraces, 1 = untouched
            void evaluate(context& ctx) override {
                const field& in = ctx.input(0);
                const f32 s = std::max(std::abs(step), 1e-6f);
                const f32 k = std::clamp(smoothness, 0.0f, 1.0f);
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) {
                    const f32 v = in.at(gx, gy) / s;
                    const f32 base = std::floor(v);
                    const f32 t = v - base;
                    // Flat treads with a steep riser at the end of each step; smoothness widens the riser
                    const f32 riser = smoothstep(1.0f - std::max(k, 0.02f), 1.0f, t);
                    out = (base + glm::mix(riser, t, k)) * s;
                });
            }
            static node_type describe() {
                return node_builder<terrace_node>("terrain.terrace", "Terrace", "Modify")
                    .describe("Flat steps with risers between them (rice fields, mesas)")
                    .input("in", pin_type::heightfield).output("out", pin_type::heightfield)
                    .number("Step", &terrace_node::step, 0.01f, 10000.0f, "Height of one terrace")
                    .number("Smoothness", &terrace_node::smoothness, 0.0f, 1.0f)
                    .build();
            }
        };


        struct scale_offset_node final : node {
            f32 scale = 1.0f, offset = 0.0f;
            void evaluate(context& ctx) override {
                const field& in = ctx.input(0);
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) { out = in.at(gx, gy) * scale + offset; });
            }
            static node_type describe() {
                return node_builder<scale_offset_node>("terrain.scale_offset", "Scale / Offset", "Modify")
                    .describe("in * scale + offset")
                    .input("in", pin_type::heightfield).output("out", pin_type::heightfield)
                    .number("Scale", &scale_offset_node::scale).number("Offset", &scale_offset_node::offset)
                    .build();
            }
        };

        // neighbourhood ---------------------------------------------------------------------------------------------------

        // Separable gaussian over [radius] world units. Exact across chunks: every output sample runs the same
        // arithmetic on the same lattice samples, whatever region it is part of
        void gaussian_blur(const field& in, field& out, u32 r) {

            const region& area = out.area();
            if (r == 0) {
                for (i64 gy = area.y0; gy < area.y1(); ++gy)
                    for (i64 gx = area.x0; gx < area.x1(); ++gx)
                        out.at(gx, gy) = in.at(gx, gy);
                return;
            }

            const f32 sigma = std::max(static_cast<f32>(r) * 0.5f, 0.5f);
            std::vector<f32> weights(2 * r + 1);
            f32 total = 0.0f;
            for (i64 i = -static_cast<i64>(r); i <= static_cast<i64>(r); ++i) {
                const f32 w = std::exp(-static_cast<f32>(i * i) / (2.0f * sigma * sigma));
                weights[static_cast<size_t>(i + r)] = w;
                total += w;
            }
            for (auto& w : weights)
                w /= total;

            // Horizontal pass over the output columns, rows grown by r for the vertical pass
            region temp_area = area;
            temp_area.y0 -= r;
            temp_area.height += 2 * r;
            field temp(temp_area);
            for (i64 gy = temp_area.y0; gy < temp_area.y1(); ++gy)
                for (i64 gx = temp_area.x0; gx < temp_area.x1(); ++gx) {
                    f32 sum = 0.0f;
                    for (i64 i = -static_cast<i64>(r); i <= static_cast<i64>(r); ++i)
                        sum += in.at(gx + i, gy) * weights[static_cast<size_t>(i + r)];
                    temp.at(gx, gy) = sum;
                }
            for (i64 gy = area.y0; gy < area.y1(); ++gy)
                for (i64 gx = area.x0; gx < area.x1(); ++gx) {
                    f32 sum = 0.0f;
                    for (i64 i = -static_cast<i64>(r); i <= static_cast<i64>(r); ++i)
                        sum += temp.at(gx, gy + i) * weights[static_cast<size_t>(i + r)];
                    out.at(gx, gy) = sum;
                }
        }


        struct blur_node final : node {
            f32 radius = 20.0f;         // world units
            [[nodiscard]] u32 apron(f64 spacing) const override { return std::min(samples_for(radius, spacing), 256u); }
            void evaluate(context& ctx) override {
                gaussian_blur(ctx.input(0), ctx.output(0), apron(ctx.area().spacing));
            }
            static node_type describe() {
                return node_builder<blur_node>("terrain.blur", "Blur", "Modify")
                    .describe("Gaussian blur (heightfields and masks)")
                    .input("in", pin_type::heightfield).output("out", pin_type::heightfield)
                    .number("Radius", &blur_node::radius, 0.0f, 2000.0f, "World units")
                    .build();
            }
        };


        struct sharpen_node final : node {
            f32 radius = 20.0f;
            f32 amount = 1.0f;
            [[nodiscard]] u32 apron(f64 spacing) const override { return std::min(samples_for(radius, spacing), 256u); }
            void evaluate(context& ctx) override {
                const field& in = ctx.input(0);
                field& out = ctx.output(0);
                gaussian_blur(in, out, apron(ctx.area().spacing));
                ctx.for_each(out, [&](i64 gx, i64 gy, glm::dvec2, f32& value) {
                    value = in.at(gx, gy) + (in.at(gx, gy) - value) * amount;       // unsharp mask
                });
            }
            static node_type describe() {
                return node_builder<sharpen_node>("terrain.sharpen", "Sharpen", "Modify")
                    .describe("Unsharp mask: exaggerates detail smaller than the radius")
                    .input("in", pin_type::heightfield).output("out", pin_type::heightfield)
                    .number("Radius", &sharpen_node::radius, 0.0f, 2000.0f, "World units")
                    .number("Amount", &sharpen_node::amount, 0.0f, 10.0f)
                    .build();
            }
        };


        // Edge-preserving smoothing: like Blur, but neighbours much higher or lower than the sample count less (cliffs survive)
        struct smooth_node final : node {
            f32 radius = 10.0f;
            f32 edge = 20.0f;           // height difference at which a neighbour's weight halves
            [[nodiscard]] u32 apron(f64 spacing) const override { return std::min(samples_for(radius, spacing), 32u); }
            void evaluate(context& ctx) override {
                const field& in = ctx.input(0);
                const i64 r = static_cast<i64>(apron(ctx.area().spacing));
                const f32 e2 = std::max(edge * edge, 1e-6f);
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) {
                    const f32 center = in.at(gx, gy);
                    f32 sum = 0.0f, total = 0.0f;
                    for (i64 dy = -r; dy <= r; ++dy)
                        for (i64 dx = -r; dx <= r; ++dx) {
                            const f32 v = in.at(gx + dx, gy + dy);
                            const f32 d = v - center;
                            const f32 w = 1.0f / (1.0f + d * d / e2);
                            sum += v * w;
                            total += w;
                        }
                    out = total > 0.0f ? sum / total : center;
                });
            }
            static node_type describe() {
                return node_builder<smooth_node>("terrain.smooth", "Smooth", "Modify")
                    .describe("Edge-preserving smoothing: removes small bumps, keeps cliffs")
                    .input("in", pin_type::heightfield).output("out", pin_type::heightfield)
                    .number("Radius", &smooth_node::radius, 0.0f, 200.0f, "World units")
                    .number("Edge", &smooth_node::edge, 0.01f, 10000.0f, "Height difference treated as an edge")
                    .build();
            }
        };


        struct warp_by_field_node final : node {
            f32 strength = 50.0f;       // world units per unit of the warp fields
            f32 limit = 200.0f;         // largest displacement (bounds the apron)
            [[nodiscard]] u32 apron(f64 spacing) const override { return std::min(samples_for(std::abs(limit), spacing), 1024u) + 1; }
            void evaluate(context& ctx) override {
                const field& in = ctx.input(0);
                const field& wx = ctx.input(1);
                const field& wz = ctx.input(2);
                const f64 l = std::min(static_cast<f64>(std::abs(limit)), static_cast<f64>(apron(ctx.area().spacing) - 1) * ctx.area().spacing);
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2 p, f32& out) {
                    glm::dvec2 d(static_cast<f64>(wx.at(gx, gy)) * strength, static_cast<f64>(wz.at(gx, gy)) * strength);
                    d = glm::clamp(d, glm::dvec2(-l), glm::dvec2(l));
                    out = in.sample(p + d);
                });
            }
            static node_type describe() {
                return node_builder<warp_by_field_node>("terrain.warp_by_field", "Warp By Field", "Modify")
                    .describe("Displaces the input by two fields (x and z offsets)")
                    .input("in", pin_type::heightfield)
                    .input("offset x", pin_type::heightfield)
                    .input("offset z", pin_type::heightfield)
                    .output("out", pin_type::heightfield)
                    .number("Strength", &warp_by_field_node::strength, 0.0f, 0.0f, "World units per unit of the offset fields")
                    .number("Limit", &warp_by_field_node::limit, 0.0f, 10000.0f, "Largest displacement in world units")
                    .build();
            }
        };


        // Thermal erosion: material slides down where the slope exceeds the talus angle. Jacobi iterations (every sample
        // reads the previous iteration only), so a chunk with an apron of [iterations] samples is exact
        struct thermal_erosion_node final : node {
            i32 iterations = 20;
            f32 talus = 0.6f;           // slope (height / distance) where material starts to slide
            f32 strength = 0.5f;
            [[nodiscard]] u32 apron(f64) const override { return static_cast<u32>(std::clamp(iterations, 0, 200)); }
            void evaluate(context& ctx) override {
                const field& in = ctx.input(0);
                const u32 n = apron(ctx.area().spacing);
                const f32 limit = talus * static_cast<f32>(ctx.area().spacing);
                const f32 k = std::clamp(strength, 0.0f, 1.0f) * 0.25f;
                static constexpr i64 OFFSETS[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };

                field current(ctx.area().expanded(n));
                ctx.for_each(current, [&](i64 gx, i64 gy, glm::dvec2, f32& v) { v = in.at(gx, gy); });
                for (u32 it = 0; it < n; ++it) {
                    if (ctx.cancelled())
                        return;
                    field next(ctx.area().expanded(n - it - 1));
                    ctx.for_each(next, [&](i64 gx, i64 gy, glm::dvec2, f32& v) {
                        const f32 h = current.at(gx, gy);
                        f32 delta = 0.0f;
                        for (const auto& o : OFFSETS) {
                            const f32 d = h - current.at(gx + o[0], gy + o[1]);
                            if (d > limit)
                                delta -= (d - limit) * k;           // flows out to a lower neighbour
                            else if (-d > limit)
                                delta += (-d - limit) * k;          // flows in from a higher neighbour
                        }
                        v = h + delta;
                    });
                    current = std::move(next);
                }
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) { out = current.at(gx, gy); });
            }
            static node_type describe() {
                return node_builder<thermal_erosion_node>("terrain.thermal_erosion", "Thermal Erosion", "Modify")
                    .describe("Slopes steeper than the talus collapse into scree: softens cliffs, fills gullies")
                    .input("in", pin_type::heightfield).output("out", pin_type::heightfield)
                    .integer("Iterations", &thermal_erosion_node::iterations, 0, 200, "More = stronger (and slower, larger apron)")
                    .number("Talus", &thermal_erosion_node::talus, 0.0f, 5.0f, "Steepest stable slope (height per world unit)")
                    .number("Strength", &thermal_erosion_node::strength, 0.0f, 1.0f)
                    .build();
            }
        };

    }

    void register_modify(i_node_registry& registry, std::string_view owner) {

        registry.add(owner, remap_node::describe());
        registry.add(owner, clamp_node::describe());
        registry.add(owner, invert_node::describe());
        registry.add(owner, abs_node::describe());
        registry.add(owner, power_node::describe());
        registry.add(owner, quantize_node::describe());
        registry.add(owner, terrace_node::describe());
        registry.add(owner, scale_offset_node::describe());
        registry.add(owner, blur_node::describe());
        registry.add(owner, sharpen_node::describe());
        registry.add(owner, smooth_node::describe());
        registry.add(owner, warp_by_field_node::describe());
        registry.add(owner, thermal_erosion_node::describe());
    }

}
