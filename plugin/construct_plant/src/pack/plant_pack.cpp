#include "pack/plant_pack.h"
#include "pack/plant_self_test.h"

#include <procedural/construct/reflect.h>



namespace GLT::construct::plant {

    // TYPES ===========================================================================================================

    namespace pack {

        struct [[=tag_schema{ text("plant") }]] plant_tags {

            [[=attr{
                .display = text("Level"),
                .tooltip = text("Branch order: 0 trunk / drawn branch, 1 branch on it, 2 twig, ..."),
                .propagation = text("drop"),
                .user_editable = false }]]
            i32 level = 0;

            [[=attr{
                .display = text("Sides"),
                .tooltip = text("Tube sides at 5 cm radius (thinner tubes use fewer, thicker ones more)"),
                .propagation = text("copy"),
                .min = 3.0f,
                .max = 64.0f }]]
            i32 sides = 10;

            [[=attr{
                .display = text("Caps"),
                .tooltip = text("Close the tip of the tube"),
                .propagation = text("copy") }]]
            bool caps = true;

            [[=attr{
                .display = text("Two sided"),
                .tooltip = text("Leaf cards get back faces"),
                .propagation = text("copy") }]]
            bool two_sided = true;

            [[=attr{
                .display = text("Leaf density"),
                .tooltip = text("Leaf cards per square metre (leaf planes; overrides the stage setting)"),
                .propagation = text("copy"),
                .min = 0.0f,
                .max = 2000.0f }]]
            f32 leaf_density = 50.0f;

            [[=attr{
                .display = text("Leaf size"),
                .tooltip = text("Leaf length in metres (leaf planes; overrides the stage setting)"),
                .propagation = text("copy"),
                .min = 0.005f,
                .max = 3.0f }]]
            f32 leaf_size = 0.12f;

            [[=attr{
                .display = text("Taper"),
                .tooltip = text("Drawn branches without a radius per point: how much the radius shrinks toward the tip"),
                .propagation = text("copy"),
                .min = 0.0f,
                .max = 1.0f }]]
            f32 taper = 0.6f;
        };


        struct [[=kind_info{ text("plant.branch"), shape_kind::curve, text("Branch") }]] branch_params {};


        struct leaf_card {
            glm::vec3                                   base{ 0.0f };
            glm::vec3                                   direction{ 0.0f, 1.0f, 0.0f };     // along the leaf
            glm::vec3                                   normal{ 0.0f, 0.0f, 1.0f };        // face of the leaf
            f32                                         length = 0.1f;
            f32                                         width = 0.05f;
            f32                                         bend = 0.0f;
        };

        struct [[=kind_info{ text("plant.leaf_cluster"), shape_kind::point, text("Leaves") }]] leaf_cluster_params {
            std::vector<leaf_card>                      cards{};
            bool                                        two_sided = true;
        };

    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    role_id role_branch() { static const role_id id = intern("plant.branch"); return id; }
    role_id role_leaves() { static const role_id id = intern("plant.leaves"); return id; }
    kind_id kind_branch() { static const kind_id id = intern("plant.branch"); return id; }
    kind_id kind_leaf_cluster() { static const kind_id id = intern("plant.leaf_cluster"); return id; }

    namespace tags {
        attribute_id level()        { return attribute_of<^^pack::plant_tags::level>(); }
        attribute_id sides()        { return attribute_of<^^pack::plant_tags::sides>(); }
        attribute_id caps()         { return attribute_of<^^pack::plant_tags::caps>(); }
        attribute_id two_sided()    { return attribute_of<^^pack::plant_tags::two_sided>(); }
        attribute_id leaf_density() { return attribute_of<^^pack::plant_tags::leaf_density>(); }
        attribute_id leaf_size()    { return attribute_of<^^pack::plant_tags::leaf_size>(); }
        attribute_id taper()        { return attribute_of<^^pack::plant_tags::taper>(); }
    }

    namespace {

        constexpr f32                                   TAU = 6.283185307179586f;
        constexpr f32                                   GOLDEN_ANGLE = 2.399963229728653f;
        constexpr u32                                   MAX_BRANCHES_PER_STAGE = 20000;
        constexpr u32                                   MAX_LEAVES_PER_CLUSTER = 4000;

        name_id material_bark() { static const name_id id = intern("bark"); return id; }
        name_id material_leaf() { static const name_id id = intern("leaf"); return id; }

        f32 number(const context& ctx, const element& e, attribute_id id, f32 fallback) {
            const attribute_value v = ctx.attribute(e, id);
            return v.index() ? static_cast<f32>(as_number(v)) : fallback;
        }

        glm::vec3 perpendicular(const glm::vec3& v) {
            const glm::vec3 axis = std::abs(v.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            return glm::normalize(glm::cross(v, axis));
        }

        f32 jitter(rng& r, f32 amount) { return 1.0f + static_cast<f32>(r.range(-1.0, 1.0)) * amount; }

        // Smooth 3D noise along a branch: a few sines with random phases (seeded), 0 at the base
        struct wobble_noise {

            glm::vec3                                   phase_a{}, phase_b{};
            f32                                         frequency = 1.0f;
            f32                                         amplitude = 0.0f;

            wobble_noise(rng& r, f32 amp, f32 freq) : frequency(std::max(freq, 0.0f)), amplitude(amp) {
                phase_a = glm::vec3(static_cast<f32>(r.range(0.0, TAU)), static_cast<f32>(r.range(0.0, TAU)), static_cast<f32>(r.range(0.0, TAU)));
                phase_b = glm::vec3(static_cast<f32>(r.range(0.0, TAU)), static_cast<f32>(r.range(0.0, TAU)), static_cast<f32>(r.range(0.0, TAU)));
            }

            [[nodiscard]] glm::vec3 at(f32 distance, f32 fade_in) const {
                if (amplitude <= 0.0f)
                    return glm::vec3(0.0f);
                const f32 x = distance * frequency * TAU;
                const glm::vec3 n(std::sin(x + phase_a.x) + 0.5f * std::sin(2.3f * x + phase_b.x),
                                  std::sin(0.9f * x + phase_a.y) + 0.5f * std::sin(2.1f * x + phase_b.y),
                                  std::sin(1.1f * x + phase_a.z) + 0.5f * std::sin(1.9f * x + phase_b.z));
                const f32 fade = fade_in > 0.0f ? std::clamp(distance / fade_in, 0.0f, 1.0f) : 1.0f;
                return n * (amplitude * fade / 1.5f);
            }
        };

        // A branch in construct space: where it is at t, which way it runs, how thick it is
        struct branch_sample {
            glm::vec3                                   position{ 0.0f };
            glm::vec3                                   tangent{ 0.0f, 1.0f, 0.0f };
            glm::vec3                                   normal{ 1.0f, 0.0f, 0.0f };
            glm::vec3                                   binormal{ 0.0f, 0.0f, 1.0f };
            f32                                         radius = 0.05f;
        };

        const geometry::curve* curve_of(const element& e) {
            const auto* c = std::get_if<curve_shape>(&e.data);
            return c && c->curve.point_count() >= 2 ? &c->curve : nullptr;
        }

        branch_sample sample_branch(const element& e, f64 t) {
            branch_sample out{};
            const geometry::curve* c = curve_of(e);
            if (!c)
                return out;
            const geometry::curve_sample s = c->sample(t);
            out.position = glm::vec3(e.placement.to_world(s.position));
            out.tangent = glm::normalize(e.placement.rotation * (s.tangent * e.placement.scale));
            out.normal = glm::normalize(e.placement.rotation * s.normal);
            out.binormal = glm::normalize(glm::cross(out.tangent, out.normal));
            out.normal = glm::cross(out.binormal, out.tangent);
            const f32 scale = (e.placement.scale.x + e.placement.scale.z) * 0.5f;
            if (const attribute_value v = c->attribute(attr_radius(), t); v.index())
                out.radius = static_cast<f32>(as_number(v)) * scale;
            return out;
        }

        f32 branch_length(const element& e) {
            const geometry::curve* c = curve_of(e);
            return c ? static_cast<f32>(c->length()) * std::max({ e.placement.scale.x, e.placement.scale.y, e.placement.scale.z }) : 0.0f;
        }

        // Grows a polyline from [dir]: gravity and light seeking bend it a little every step, the wobble displaces it.
        // Points relative to the start
        std::vector<glm::vec3> grow(glm::vec3 dir, f32 length, u32 steps, f32 gravity, f32 light, const wobble_noise& wobble,
                                    f32 base_spacing_power = 1.0f) {
            std::vector<glm::vec3> pts;
            pts.reserve(steps + 1);
            glm::vec3 p(0.0f);
            pts.push_back(p);
            dir = glm::normalize(dir);
            f32 travelled = 0.0f;
            for (u32 k = 1; k <= steps; ++k) {
                const f32 u0 = std::pow(static_cast<f32>(k - 1) / static_cast<f32>(steps), base_spacing_power);
                const f32 u1 = std::pow(static_cast<f32>(k) / static_cast<f32>(steps), base_spacing_power);
                const f32 step = (u1 - u0) * length;
                const f32 bend = 2.0f * (u1 - u0);
                dir = dir + glm::vec3(0.0f, -1.0f, 0.0f) * (gravity * bend) + glm::vec3(0.0f, 1.0f, 0.0f) * (light * bend);
                if (glm::dot(dir, dir) < 1e-8f)
                    dir = glm::vec3(0.0f, 1.0f, 0.0f);
                dir = glm::normalize(dir);
                p += dir * step;
                travelled += step;
                pts.push_back(p + wobble.at(travelled, std::max(length * 0.15f, 0.05f)));
            }
            return pts;
        }

        u32 steps_for(f32 length, f32 per_metre, u32 lo, u32 hi) {
            return std::clamp(static_cast<u32>(std::ceil(length * std::max(per_metre, 0.1f))), lo, hi);
        }

        geometry::curve make_curve(std::vector<glm::vec3> points, std::span<const f32> radii) {
            geometry::curve c(geometry::curve_type::polyline, std::move(points));
            for (u32 i = 0; i < radii.size(); ++i)
                c.set_point_attribute(attr_radius(), i, radii[i]);
            return c;
        }

        // A card: two or three rows of vertices (bent), double sided on request
        void add_leaf(geometry::mesh& m, const pack::leaf_card& leaf, bool two_sided) {
            const glm::vec3 dir = glm::normalize(leaf.direction);
            const glm::vec3 side = glm::normalize(glm::cross(leaf.normal, dir)) * (leaf.width * 0.5f);
            const glm::vec3 droop = -glm::normalize(leaf.normal) * (leaf.bend * leaf.length * 0.35f);
            const bool bent = leaf.bend > 0.01f;
            std::vector<glm::vec3> rows;
            rows.push_back(leaf.base);
            if (bent)
                rows.push_back(leaf.base + dir * (leaf.length * 0.5f) + droop * 0.35f);
            rows.push_back(leaf.base + dir * leaf.length + droop);
            const f32 widths[3] = { 0.35f, 1.0f, 0.25f };

            std::vector<u32> left, right;
            for (size_t i = 0; i < rows.size(); ++i) {
                const f32 w = bent ? widths[i] : (i == 0 ? 0.5f : 1.0f);
                left.push_back(m.add_vertex(rows[i] - side * w));
                right.push_back(m.add_vertex(rows[i] + side * w));
            }
            for (size_t i = 0; i + 1 < rows.size(); ++i) {
                const f32 v0 = static_cast<f32>(i) / static_cast<f32>(rows.size() - 1);
                const f32 v1 = static_cast<f32>(i + 1) / static_cast<f32>(rows.size() - 1);
                const std::array<u32, 4> front{ left[i], right[i], right[i + 1], left[i + 1] };
                const u32 f0 = m.add_face(front);
                m.sync_columns();
                m.set(attribute_domain::face, geometry::attr_material(), f0, material_leaf());
                const u32 h0 = m.face_halfedge(f0);
                const glm::vec2 uv[4] = { { 0, v0 }, { 1, v0 }, { 1, v1 }, { 0, v1 } };
                for (u32 k = 0; k < 4; ++k)
                    m.set(attribute_domain::corner, geometry::attr_uv(), h0 + k, uv[k]);
                if (two_sided) {
                    const std::array<u32, 4> back{ left[i], left[i + 1], right[i + 1], right[i] };
                    const u32 f1 = m.add_face(back);
                    m.sync_columns();
                    m.set(attribute_domain::face, geometry::attr_material(), f1, material_leaf());
                    const u32 h1 = m.face_halfedge(f1);
                    const glm::vec2 uvb[4] = { { 0, v0 }, { 0, v1 }, { 1, v1 }, { 1, v0 } };
                    for (u32 k = 0; k < 4; ++k)
                        m.set(attribute_domain::corner, geometry::attr_uv(), h1 + k, uvb[k]);
                }
            }
        }

    }

    // Stages ---------------------------------------------------------------------------------------------------------------

    namespace pack {

        enum class distribution { per_parent, per_metre };
        enum class arrangement { spiral, alternate, opposite, whorled, random };
        enum class length_mode { fraction_of_parent, metres };
        enum class leaf_layout { along_and_tips, along, tips };

        // Trunk ----------------------------------------------------------------------------------------------------------

        struct trunk_settings {

            [[=setting{
                .display = text("Stems"),
                .tooltip = text("Stems growing from the ground"),
                .group = text("Distribution"),
                .min = 1.0f,
                .max = 32.0f }]]
            i32 count = 1;

            [[=setting{
                .display = text("Spread"),
                .tooltip = text("Radius of the circle the stems start in"),
                .group = text("Distribution"),
                .unit = text("m"),
                .min = 0.0f,
                .max = 20.0f }]]
            f32 spread = 0.0f;

            [[=setting{
                .display = text("Height"),
                .group = text("Shape"),
                .unit = text("m"),
                .min = 0.1f,
                .max = 120.0f }]]
            f32 height = 7.0f;

            [[=setting{
                .display = text("Height variation"),
                .tooltip = text("Random change of the height per stem (0.2: +-20 %)"),
                .group = text("Shape"),
                .min = 0.0f,
                .max = 1.0f }]]
            f32 height_variation = 0.15f;

            [[=setting{
                .display = text("Lean"),
                .tooltip = text("Tilt of the stems away from vertical"),
                .group = text("Shape"),
                .unit = text("deg"),
                .min = 0.0f,
                .max = 85.0f }]]
            f32 lean = 3.0f;

            [[=setting{
                .display = text("Lean variation"),
                .group = text("Shape"),
                .unit = text("deg"),
                .min = 0.0f,
                .max = 85.0f }]]
            f32 lean_variation = 4.0f;

            [[=setting{
                .display = text("Radius"),
                .tooltip = text("Radius at the base (before the flare)"),
                .group = text("Thickness"),
                .unit = text("m"),
                .min = 0.002f,
                .max = 10.0f }]]
            f32 radius = 0.22f;

            [[=setting{
                .display = text("Radius profile"),
                .tooltip = text("Radius from the base (left) to the top (right), times Radius"),
                .group = text("Thickness") }]]
            profile_curve radius_profile = profile_curve::of({ { 0.0f, 1.0f }, { 0.2f, 0.75f }, { 1.0f, 0.12f } });

            [[=setting{
                .display = text("Root flare"),
                .tooltip = text("Extra radius at the ground (times Radius)"),
                .group = text("Thickness"),
                .min = 0.0f,
                .max = 4.0f }]]
            f32 flare = 0.6f;

            [[=setting{
                .display = text("Flare height"),
                .group = text("Thickness"),
                .unit = text("m"),
                .min = 0.01f,
                .max = 10.0f }]]
            f32 flare_height = 0.6f;

            [[=setting{
                .display = text("Wobble"),
                .tooltip = text("How far the stem wanders from a straight line"),
                .group = text("Noise"),
                .unit = text("m"),
                .min = 0.0f,
                .max = 5.0f }]]
            f32 wobble = 0.12f;

            [[=setting{
                .display = text("Wobble frequency"),
                .tooltip = text("Waves per metre"),
                .group = text("Noise"),
                .min = 0.0f,
                .max = 10.0f }]]
            f32 wobble_frequency = 0.35f;

            [[=setting{
                .display = text("Sides"),
                .tooltip = text("Tube sides at 5 cm radius (thicker tubes get more)"),
                .group = text("Mesh"),
                .min = 3.0f,
                .max = 48.0f }]]
            i32 sides = 10;

            [[=setting{
                .display = text("Points per metre"),
                .tooltip = text("Resolution along the stem"),
                .group = text("Mesh"),
                .min = 0.5f,
                .max = 40.0f,
                .advanced = true }]]
            f32 resolution = 3.0f;
        };


        struct [[=generator_info{
            .name = text("plant.trunk"),
            .display = text("Trunk"),
            .description = text("Stems from the ground: height, lean, root flare, radius profile, wobble"),
            .color = 0xb07a4a }]] trunk final : generator_of<trunk_settings> {

            static std::vector<kind_id> emits() { return { kind_branch() }; }

            void generate(generator_context& ctx, const trunk_settings& s) override {

                rng r = ctx.random(0x7a0c);
                for (i32 i = 0; i < std::max(s.count, 1); ++i) {
                    const f32 around = s.count > 1 ? TAU * static_cast<f32>(i) / static_cast<f32>(s.count) + static_cast<f32>(r.range(-0.4, 0.4)) : 0.0f;
                    const f32 dist = s.count > 1 ? s.spread * static_cast<f32>(r.range(0.4, 1.0)) : 0.0f;
                    const glm::vec3 start(std::cos(around) * dist, 0.0f, std::sin(around) * dist);

                    const f32 height = std::max(s.height * jitter(r, s.height_variation), 0.05f);
                    const f32 lean = glm::radians(std::max(s.lean + static_cast<f32>(r.range(-1.0, 1.0)) * s.lean_variation, 0.0f));
                    const f32 lean_dir = s.count > 1 ? around : static_cast<f32>(r.range(0.0, TAU));
                    const glm::vec3 dir(std::sin(lean) * std::cos(lean_dir), std::cos(lean), std::sin(lean) * std::sin(lean_dir));

                    const u32 steps = steps_for(height, s.resolution, 6, 256);
                    wobble_noise wobble(r, s.wobble, s.wobble_frequency);
                    std::vector<glm::vec3> pts = grow(dir, height, steps, 0.0f, 0.0f, wobble, 1.4f);
                    std::vector<f32> radii(pts.size());
                    f32 along = 0.0f;
                    for (size_t k = 0; k < pts.size(); ++k) {
                        if (k > 0)
                            along += glm::length(pts[k] - pts[k - 1]);
                        const f32 u = std::clamp(along / height, 0.0f, 1.0f);
                        const f32 flare_t = std::max(0.0f, 1.0f - along / std::max(s.flare_height, 1e-3f));
                        radii[k] = std::max(s.radius * s.radius_profile.evaluate(u) + s.radius * s.flare * flare_t * flare_t, 0.002f);
                    }

                    frame placement{};
                    placement.origin = glm::dvec3(start);
                    ctx.emit_root("stem", i, kind_branch())
                        .placement(placement)
                        .shape(curve_shape{ make_curve(std::move(pts), radii) })
                        .set(tags::level(), i32(0))
                        .set(tags::sides(), s.sides)
                        .set(tags::caps(), true);
                }
            }
        };

        // Drawn branches -------------------------------------------------------------------------------------------------

        struct drawn_settings {

            [[=setting{
                .display = text("Snap distance"),
                .tooltip = text("A branch whose first point is this close to another branch starts on it (forks)"),
                .group = text("Connection"),
                .unit = text("m"),
                .min = 0.0f,
                .max = 5.0f }]]
            f32 snap = 0.3f;

            [[=setting{
                .display = text("Taper"),
                .tooltip = text("Branches without a radius per point: how much the radius shrinks toward the tip"),
                .group = text("Thickness"),
                .min = 0.0f,
                .max = 1.0f }]]
            f32 taper = 0.6f;

            [[=setting{
                .display = text("Radius"),
                .tooltip = text("Branches without a radius per point"),
                .group = text("Thickness"),
                .unit = text("m"),
                .min = 0.002f,
                .max = 5.0f }]]
            f32 radius = 0.06f;

            [[=setting{
                .display = text("Wobble"),
                .tooltip = text("Seeded noise on top of the drawn shape (0: exactly as drawn)"),
                .group = text("Noise"),
                .unit = text("m"),
                .min = 0.0f,
                .max = 2.0f }]]
            f32 wobble = 0.0f;

            [[=setting{
                .display = text("Wobble frequency"),
                .group = text("Noise"),
                .min = 0.0f,
                .max = 10.0f }]]
            f32 wobble_frequency = 1.0f;

            [[=setting{
                .display = text("Sides"),
                .group = text("Mesh"),
                .min = 3.0f,
                .max = 48.0f }]]
            i32 sides = 10;

            [[=setting{
                .display = text("Points per metre"),
                .group = text("Mesh"),
                .min = 0.5f,
                .max = 40.0f,
                .advanced = true }]]
            f32 resolution = 6.0f;
        };


        struct [[=generator_info{
            .name = text("plant.drawn"),
            .display = text("Drawn branches"),
            .description = text("The branch splines you placed in the view (role Branch). A branch starting near another one forks from it"),
            .color = 0xd9a066,
            .guide_roles = text("plant.branch") }]] drawn final : generator_of<drawn_settings> {

            static std::vector<kind_id> emits() { return { kind_branch() }; }

            void generate(generator_context& ctx, const drawn_settings& s) override {

                const context& q = ctx.query();
                struct info { const element* g; geometry::curve source; glm::vec3 start; element_builder* b; };
                std::vector<info> all;
                for (const element* g : ctx.guides()) {
                    const geometry::curve* c = curve_of(*g);
                    if (!c || g->shape != shape_kind::curve) {
                        ctx.diagnose(severity::warning, g, "a drawn branch needs a spline with at least two points");
                        continue;
                    }
                    all.push_back({ g, *c, glm::vec3(g->placement.to_world(c->points()[0])), nullptr });
                }

                // Where each branch starts on another one (closest sample within the snap distance)
                std::vector<i32> parent(all.size(), -1);
                std::vector<glm::vec3> attach(all.size());
                std::vector<f64> attach_t(all.size(), 0.0);
                for (size_t i = 0; i < all.size(); ++i) {
                    f32 best = s.snap;
                    for (size_t j = 0; j < all.size(); ++j) {
                        if (i == j)
                            continue;
                        for (const geometry::curve_sample& smp : all[j].source.samples()) {
                            const glm::vec3 p(all[j].g->placement.to_world(smp.position));
                            const f32 d = glm::length(p - all[i].start);
                            if (d < best && smp.t < 0.999) {
                                best = d;
                                parent[i] = static_cast<i32>(j);
                                attach[i] = p;
                                attach_t[i] = smp.t;
                            }
                        }
                    }
                }
                // No cycles: walking up must end (drop the link that closes a loop)
                for (size_t i = 0; i < all.size(); ++i) {
                    std::vector<size_t> chain{ i };
                    for (i32 cur = parent[i]; cur >= 0; cur = parent[static_cast<size_t>(cur)]) {
                        if (std::ranges::find(chain, static_cast<size_t>(cur)) != chain.end()) {
                            parent[chain.back()] = -1;
                            break;
                        }
                        chain.push_back(static_cast<size_t>(cur));
                    }
                }

                std::vector<element_handle> handles(all.size());
                for (size_t i = 0; i < all.size(); ++i) {
                    const element& g = *all[i].g;
                    const geometry::curve& c = all[i].source;
                    const f32 length = static_cast<f32>(c.length());
                    const u32 steps = steps_for(length, s.resolution, 4, 512);
                    rng r = ctx.random(g, 0xd4a3);
                    wobble_noise wobble(r, s.wobble, s.wobble_frequency);
                    const bool has_radii = c.find_point_column(attr_radius()) != nullptr;
                    const f32 taper = number(q, g, tags::taper(), s.taper);
                    const f32 base_radius = number(q, g, attr_radius(), s.radius);

                    // Snapped: pull the start onto the parent, fading out along the first third
                    glm::vec3 shift(0.0f);
                    if (parent[i] >= 0)
                        shift = glm::inverse(g.placement.rotation) * (attach[i] - all[i].start) / g.placement.scale;

                    std::vector<glm::vec3> pts;
                    std::vector<f32> radii;
                    for (u32 k = 0; k <= steps; ++k) {
                        const f64 t = static_cast<f64>(k) / static_cast<f64>(steps);
                        const geometry::curve_sample smp = c.sample(t);
                        const f32 fade = std::max(0.0f, 1.0f - static_cast<f32>(t) * 3.0f);
                        pts.push_back(smp.position + shift * fade + wobble.at(static_cast<f32>(smp.distance), std::max(length * 0.15f, 0.05f)));
                        f32 radius = base_radius * (1.0f - taper * static_cast<f32>(t));
                        if (has_radii)
                            if (const attribute_value v = c.attribute(attr_radius(), t); v.index())
                                radius = static_cast<f32>(as_number(v));
                        radii.push_back(std::max(radius, 0.002f));
                    }

                    element_builder b = ctx.emit(g, "branch", -1, kind_branch());
                    b.shape(curve_shape{ make_curve(std::move(pts), radii) })
                        .set(tags::level(), i32(0))
                        .set(tags::sides(), s.sides)
                        .set(tags::caps(), true);
                    handles[i] = b.handle();
                }
                for (size_t i = 0; i < all.size(); ++i)
                    if (parent[i] >= 0) {
                        attribute_set params{};
                        params.set(intern("plant.t"), attach_t[i]);
                        ctx.relate(intern("plant.fork"), intern("plant.fork"), handles[i], handles[static_cast<size_t>(parent[i])], std::move(params));
                    }
            }
        };

        // Branches -------------------------------------------------------------------------------------------------------

        struct branch_settings {

            [[=setting{
                .display = text("Distribution"),
                .tooltip = text("Per parent: a fixed number on every parent. Per metre: longer parents get more"),
                .group = text("Distribution") }]]
            distribution mode = distribution::per_parent;

            [[=setting{
                .display = text("Count"),
                .tooltip = text("Branches per parent (Distribution: per parent)"),
                .group = text("Distribution"),
                .min = 0.0f,
                .max = 400.0f }]]
            i32 count = 9;

            [[=setting{
                .display = text("Density"),
                .tooltip = text("Branches per metre of parent (Distribution: per metre)"),
                .group = text("Distribution"),
                .unit = text("/m"),
                .min = 0.0f,
                .max = 200.0f }]]
            f32 density = 4.0f;

            [[=setting{
                .display = text("Range"),
                .tooltip = text("Part of the parent they grow on (0: its base, 1: its tip)"),
                .group = text("Distribution"),
                .min = 0.0f,
                .max = 1.0f }]]
            glm::vec2 range{ 0.35f, 0.95f };

            [[=setting{
                .display = text("Arrangement"),
                .tooltip = text("Spiral (golden angle), alternate, opposite pairs, whorls, or random around the parent"),
                .group = text("Distribution") }]]
            arrangement layout = arrangement::spiral;

            [[=setting{
                .display = text("Per whorl"),
                .tooltip = text("Branches per node (Arrangement: whorled)"),
                .group = text("Distribution"),
                .min = 2.0f,
                .max = 16.0f }]]
            i32 whorl = 4;

            [[=setting{
                .display = text("Density profile"),
                .tooltip = text("Chance to grow along the range (left: range start, right: range end)"),
                .group = text("Distribution") }]]
            profile_curve density_profile = profile_curve::constant(1.0f);

            [[=setting{
                .display = text("Position jitter"),
                .tooltip = text("Random shift along the parent, as a fraction of the spacing"),
                .group = text("Distribution"),
                .min = 0.0f,
                .max = 1.0f }]]
            f32 position_jitter = 0.4f;

            [[=setting{
                .display = text("Rotation jitter"),
                .tooltip = text("Random turn around the parent"),
                .group = text("Distribution"),
                .unit = text("deg"),
                .min = 0.0f,
                .max = 180.0f }]]
            f32 rotation_jitter = 20.0f;

            [[=setting{
                .display = text("Angle at base"),
                .tooltip = text("Angle to the parent at the start of the range"),
                .group = text("Angle"),
                .unit = text("deg"),
                .min = 0.0f,
                .max = 180.0f }]]
            f32 angle = 60.0f;

            [[=setting{
                .display = text("Angle at tip"),
                .tooltip = text("Angle to the parent at the end of the range"),
                .group = text("Angle"),
                .unit = text("deg"),
                .min = 0.0f,
                .max = 180.0f }]]
            f32 angle_tip = 35.0f;

            [[=setting{
                .display = text("Angle jitter"),
                .group = text("Angle"),
                .unit = text("deg"),
                .min = 0.0f,
                .max = 90.0f }]]
            f32 angle_jitter = 8.0f;

            [[=setting{
                .display = text("Length mode"),
                .tooltip = text("Fraction of the parent's length, or metres"),
                .group = text("Length") }]]
            length_mode length_unit = length_mode::fraction_of_parent;

            [[=setting{
                .display = text("Length"),
                .group = text("Length"),
                .min = 0.0f,
                .max = 100.0f }]]
            f32 length = 0.45f;

            [[=setting{
                .display = text("Length profile"),
                .tooltip = text("Length by where the branch sits on the parent (left: range start, right: range end)"),
                .group = text("Length") }]]
            profile_curve length_profile = profile_curve::of({ { 0.0f, 0.75f }, { 0.35f, 1.0f }, { 1.0f, 0.35f } });

            [[=setting{
                .display = text("Length jitter"),
                .group = text("Length"),
                .min = 0.0f,
                .max = 1.0f }]]
            f32 length_jitter = 0.2f;

            [[=setting{
                .display = text("Shortest"),
                .tooltip = text("Branches shorter than this are not grown"),
                .group = text("Length"),
                .unit = text("m"),
                .min = 0.0f,
                .max = 10.0f,
                .advanced = true }]]
            f32 min_length = 0.04f;

            [[=setting{
                .display = text("Radius ratio"),
                .tooltip = text("Base radius as a fraction of the parent's radius where it starts"),
                .group = text("Thickness"),
                .min = 0.01f,
                .max = 1.0f }]]
            f32 radius_ratio = 0.6f;

            [[=setting{
                .display = text("Radius profile"),
                .tooltip = text("Radius from base (left) to tip (right), times the base radius"),
                .group = text("Thickness") }]]
            profile_curve radius_profile = profile_curve::linear(1.0f, 0.2f);

            [[=setting{
                .display = text("Thinnest"),
                .group = text("Thickness"),
                .unit = text("m"),
                .min = 0.0005f,
                .max = 1.0f,
                .advanced = true }]]
            f32 min_radius = 0.003f;

            [[=setting{
                .display = text("Gravity"),
                .tooltip = text("Bends the branch down over its length (negative: up)"),
                .group = text("Growth"),
                .min = -3.0f,
                .max = 3.0f }]]
            f32 gravity = 0.15f;

            [[=setting{
                .display = text("Light seeking"),
                .tooltip = text("Bends the branch toward the sky"),
                .group = text("Growth"),
                .min = 0.0f,
                .max = 3.0f }]]
            f32 light_seeking = 0.1f;

            [[=setting{
                .display = text("Wobble"),
                .tooltip = text("Wandering, as a fraction of the branch length"),
                .group = text("Growth"),
                .min = 0.0f,
                .max = 0.5f }]]
            f32 wobble = 0.04f;

            [[=setting{
                .display = text("Wobble frequency"),
                .tooltip = text("Waves per metre"),
                .group = text("Growth"),
                .min = 0.0f,
                .max = 20.0f,
                .advanced = true }]]
            f32 wobble_frequency = 1.2f;

            [[=setting{
                .display = text("Sides"),
                .tooltip = text("Tube sides at 5 cm radius (thinner tubes use fewer)"),
                .group = text("Mesh"),
                .min = 3.0f,
                .max = 48.0f }]]
            i32 sides = 8;

            [[=setting{
                .display = text("Points per metre"),
                .group = text("Mesh"),
                .min = 0.5f,
                .max = 60.0f,
                .advanced = true }]]
            f32 resolution = 5.0f;

            [[=setting{
                .display = text("Close tips"),
                .group = text("Mesh") }]]
            bool caps = true;
        };


        struct [[=generator_info{
            .name = text("plant.branches"),
            .display = text("Branches"),
            .description = text("Branches along the branches of another stage: distribution, angles, length, thickness, growth. Stack it for twigs"),
            .color = 0x8fb36a,
            .takes_input = true,
            .accepts = text("plant.trunk|plant.drawn|plant.branches") }]] branches final : generator_of<branch_settings> {

            static std::vector<kind_id> emits() { return { kind_branch() }; }

            // A Branches stage the user adds: defaults for its level (branches stages between it and the trunk)
            static void on_add(generator_instance& g, const construct_data& data) {
                u32 level = 1;
                u32 current = g.inputs.empty() ? 0 : g.inputs.front();
                for (u32 guard = 0; guard < 16 && current != 0; ++guard) {
                    const auto it = std::ranges::find(data.generators, current, &generator_instance::id);
                    if (it == data.generators.end() || it->type != STAGE_BRANCHES || it->inputs.empty())
                        break;
                    ++level;
                    current = it->inputs.front();
                }
                g = make_stage(STAGE_BRANCHES, g.id, g.inputs, level);
            }

            void generate(generator_context& ctx, const branch_settings& s) override {

                const context& q = ctx.query();
                const f32 r0 = std::clamp(std::min(s.range.x, s.range.y), 0.0f, 1.0f);
                const f32 r1 = std::clamp(std::max(s.range.x, s.range.y), 0.0f, 1.0f);
                u32 emitted = 0;

                for (const element* parent : ctx.inputs()) {
                    if (parent->kind != kind_branch() || !curve_of(*parent))
                        continue;
                    const f32 parent_length = branch_length(*parent);
                    const i32 parent_level = static_cast<i32>(number(q, *parent, tags::level(), 0.0f));
                    rng r = ctx.random(*parent, 0xb7a9);

                    i32 count = s.mode == distribution::per_parent ? s.count
                                                                  : static_cast<i32>(std::floor(s.density * parent_length * (r1 - r0) + r.uniform()));
                    count = std::clamp(count, 0, 2000);
                    const i32 per_node = s.layout == arrangement::opposite ? 2 : (s.layout == arrangement::whorled ? std::max(s.whorl, 2) : 1);
                    const i32 nodes = (count + per_node - 1) / per_node;

                    for (i32 k = 0; k < count && emitted < MAX_BRANCHES_PER_STAGE; ++k) {
                        const i32 node = k / per_node, in_node = k % per_node;
                        // Position along the range (nodes evenly spaced, jittered)
                        f32 u = nodes > 1 ? (static_cast<f32>(node) + 0.5f) / static_cast<f32>(nodes) : 0.5f;
                        u = std::clamp(u + static_cast<f32>(r.range(-0.5, 0.5)) * s.position_jitter / static_cast<f32>(std::max(nodes, 1)), 0.0f, 1.0f);
                        const f32 chance = s.density_profile.evaluate(u);
                        const f32 roll = static_cast<f32>(r.uniform());
                        if (roll > chance)
                            continue;
                        const f64 t = r0 + (r1 - r0) * u;

                        // Around the parent
                        f32 around = 0.0f;
                        switch (s.layout) {
                            case arrangement::spiral:    around = GOLDEN_ANGLE * static_cast<f32>(k); break;
                            case arrangement::alternate: around = 3.14159265f * static_cast<f32>(k); break;
                            case arrangement::opposite:  around = 3.14159265f * static_cast<f32>(in_node) + 1.5707963f * static_cast<f32>(node); break;
                            case arrangement::whorled:   around = TAU * static_cast<f32>(in_node) / static_cast<f32>(per_node) + 0.5f * static_cast<f32>(node); break;
                            case arrangement::random:    around = static_cast<f32>(r.range(0.0, TAU)); break;
                        }
                        around += glm::radians(static_cast<f32>(r.range(-1.0, 1.0)) * s.rotation_jitter);

                        const branch_sample at = sample_branch(*parent, t);
                        const glm::vec3 radial = at.normal * std::cos(around) + at.binormal * std::sin(around);
                        const f32 angle = glm::radians(glm::mix(s.angle, s.angle_tip, u) + static_cast<f32>(r.range(-1.0, 1.0)) * s.angle_jitter);
                        const glm::vec3 dir = glm::normalize(at.tangent * std::cos(angle) + radial * std::sin(angle));

                        const f32 base_length = s.length_unit == length_mode::fraction_of_parent ? s.length * parent_length : s.length;
                        const f32 length = base_length * s.length_profile.evaluate(u) * jitter(r, s.length_jitter);
                        if (length < std::max(s.min_length, 0.005f))
                            continue;
                        const f32 base_radius = std::max(std::min(at.radius * s.radius_ratio, at.radius * 0.95f), s.min_radius);

                        const u32 steps = steps_for(length, s.resolution, 3, 256);
                        wobble_noise wobble(r, s.wobble * length, s.wobble_frequency);
                        std::vector<glm::vec3> pts = grow(dir, length, steps, s.gravity, s.light_seeking, wobble);
                        std::vector<f32> radii(pts.size());
                        for (size_t p = 0; p < pts.size(); ++p)
                            radii[p] = std::max(base_radius * s.radius_profile.evaluate(static_cast<f32>(p) / static_cast<f32>(pts.size() - 1)),
                                                s.min_radius);

                        frame placement{};
                        placement.origin = glm::dvec3(at.position + radial * (at.radius * 0.25f));
                        element_builder b = ctx.emit(*parent, "branch", k, kind_branch());
                        b.placement(placement)
                            .shape(curve_shape{ make_curve(std::move(pts), radii) })
                            .set(tags::level(), parent_level + 1)
                            .set(tags::sides(), s.sides)
                            .set(tags::caps(), s.caps);
                        attribute_set params{};
                        params.set(intern("plant.t"), t);
                        if (const auto self = q.find(parent->path))
                            ctx.relate(intern("plant.fork"), intern("plant.fork"), b.handle(), *self, std::move(params));
                        ++emitted;
                    }
                }
                if (emitted >= MAX_BRANCHES_PER_STAGE)
                    ctx.diagnose(severity::warning, nullptr, std::format("stopped at {} branches (lower the count / density)", MAX_BRANCHES_PER_STAGE));
            }
        };

        // Leaves ---------------------------------------------------------------------------------------------------------

        struct leaf_settings {

            [[=setting{
                .display = text("Placement"),
                .tooltip = text("Along the branches, at their tips, or both"),
                .group = text("Distribution") }]]
            leaf_layout layout = leaf_layout::along_and_tips;

            [[=setting{
                .display = text("Density"),
                .tooltip = text("Leaves per metre of branch (along)"),
                .group = text("Distribution"),
                .unit = text("/m"),
                .min = 0.0f,
                .max = 500.0f }]]
            f32 density = 14.0f;

            [[=setting{
                .display = text("At the tip"),
                .tooltip = text("Leaves around the tip of each branch"),
                .group = text("Distribution"),
                .min = 0.0f,
                .max = 64.0f }]]
            i32 tip_count = 3;

            [[=setting{
                .display = text("Range"),
                .tooltip = text("Part of the branch with leaves (0: base, 1: tip)"),
                .group = text("Distribution"),
                .min = 0.0f,
                .max = 1.0f }]]
            glm::vec2 range{ 0.3f, 1.0f };

            [[=setting{
                .display = text("Size"),
                .tooltip = text("Leaf length"),
                .group = text("Shape"),
                .unit = text("m"),
                .min = 0.005f,
                .max = 3.0f }]]
            f32 size = 0.14f;

            [[=setting{
                .display = text("Size jitter"),
                .group = text("Shape"),
                .min = 0.0f,
                .max = 1.0f }]]
            f32 size_jitter = 0.25f;

            [[=setting{
                .display = text("Width"),
                .tooltip = text("Width as a fraction of the length"),
                .group = text("Shape"),
                .min = 0.02f,
                .max = 2.0f }]]
            f32 aspect = 0.55f;

            [[=setting{
                .display = text("Bend"),
                .tooltip = text("Curl of the card along its length"),
                .group = text("Shape"),
                .min = 0.0f,
                .max = 1.0f }]]
            f32 bend = 0.2f;

            [[=setting{
                .display = text("Angle"),
                .tooltip = text("Angle to the branch"),
                .group = text("Orientation"),
                .unit = text("deg"),
                .min = 0.0f,
                .max = 180.0f }]]
            f32 angle = 55.0f;

            [[=setting{
                .display = text("Angle jitter"),
                .group = text("Orientation"),
                .unit = text("deg"),
                .min = 0.0f,
                .max = 90.0f }]]
            f32 angle_jitter = 20.0f;

            [[=setting{
                .display = text("Face up"),
                .tooltip = text("Turns the leaf surfaces toward the sky"),
                .group = text("Orientation"),
                .min = 0.0f,
                .max = 1.0f }]]
            f32 face_up = 0.4f;

            [[=setting{
                .display = text("Droop"),
                .tooltip = text("Leaves hang down"),
                .group = text("Orientation"),
                .min = 0.0f,
                .max = 1.0f }]]
            f32 droop = 0.15f;

            [[=setting{
                .display = text("Two sided"),
                .tooltip = text("Back faces (off: half the triangles, needs a two sided material)"),
                .group = text("Mesh") }]]
            bool two_sided = true;
        };


        struct [[=generator_info{
            .name = text("plant.leaves"),
            .display = text("Leaves"),
            .description = text("Leaf cards along and at the tips of the branches of another stage"),
            .color = 0x5fbf4f,
            .takes_input = true,
            .accepts = text("plant.trunk|plant.drawn|plant.branches") }]] leaves final : generator_of<leaf_settings> {

            static std::vector<kind_id> emits() { return { kind_leaf_cluster() }; }

            void generate(generator_context& ctx, const leaf_settings& s) override {

                const f32 r0 = std::clamp(std::min(s.range.x, s.range.y), 0.0f, 1.0f);
                const f32 r1 = std::clamp(std::max(s.range.x, s.range.y), 0.0f, 1.0f);
                for (const element* parent : ctx.inputs()) {
                    if (parent->kind != kind_branch() || !curve_of(*parent))
                        continue;
                    rng r = ctx.random(*parent, 0x1eaf);
                    leaf_cluster_params cluster{};
                    cluster.two_sided = s.two_sided;
                    std::vector<glm::vec3> points;

                    auto add = [&](f64 t, f32 around, bool tip) {
                        const branch_sample at = sample_branch(*parent, t);
                        const glm::vec3 radial = at.normal * std::cos(around) + at.binormal * std::sin(around);
                        const f32 angle = glm::radians((tip ? s.angle * 0.6f : s.angle) + static_cast<f32>(r.range(-1.0, 1.0)) * s.angle_jitter);
                        glm::vec3 dir = glm::normalize(at.tangent * std::cos(angle) + radial * std::sin(angle));
                        dir = glm::normalize(dir + glm::vec3(0.0f, -s.droop, 0.0f));
                        // Leaf surface: across the leaf is perpendicular to the branch; turn it toward the sky
                        glm::vec3 normal = glm::normalize(glm::cross(dir, glm::cross(radial, dir) + glm::vec3(0.0f, 0.0001f, 0.0f)));
                        if (normal.y < 0.0f)
                            normal = -normal;
                        normal = glm::normalize(glm::mix(normal, glm::vec3(0.0f, 1.0f, 0.0f), s.face_up));
                        normal = glm::normalize(normal - dir * glm::dot(normal, dir));
                        leaf_card card{};
                        card.base = at.position + radial * (at.radius * 0.8f);
                        card.direction = dir;
                        card.normal = normal;
                        card.length = s.size * jitter(r, s.size_jitter);
                        card.width = card.length * s.aspect;
                        card.bend = s.bend;
                        cluster.cards.push_back(card);
                        points.push_back(card.base);
                    };

                    if (s.layout != leaf_layout::tips) {
                        const f32 length = branch_length(*parent) * (r1 - r0);
                        const u32 n = std::min(static_cast<u32>(std::floor(s.density * length + r.uniform())), MAX_LEAVES_PER_CLUSTER);
                        for (u32 k = 0; k < n; ++k) {
                            const f64 t = r0 + (r1 - r0) * (static_cast<f64>(k) + r.uniform()) / static_cast<f64>(n);
                            add(t, GOLDEN_ANGLE * static_cast<f32>(k) + static_cast<f32>(r.range(-0.4, 0.4)), false);
                        }
                    }
                    if (s.layout != leaf_layout::along)
                        for (i32 k = 0; k < s.tip_count; ++k)
                            add(1.0, TAU * (static_cast<f32>(k) + static_cast<f32>(r.uniform()) * 0.5f) / static_cast<f32>(std::max(s.tip_count, 1)), true);

                    if (cluster.cards.empty())
                        continue;
                    ctx.emit(*parent, "leaves", -1, kind_leaf_cluster())
                        .placement(frame{})
                        .shape(point_shape{ std::move(points) })
                        .params(std::move(cluster));
                }
            }
        };

        // Leaf planes ----------------------------------------------------------------------------------------------------

        struct leaf_plane_settings {

            [[=setting{
                .display = text("Density"),
                .tooltip = text("Leaves per square metre (a plane's own Leaf density attribute overrides it)"),
                .group = text("Distribution"),
                .unit = text("/m2"),
                .min = 0.0f,
                .max = 2000.0f }]]
            f32 density = 50.0f;

            [[=setting{
                .display = text("Size"),
                .group = text("Shape"),
                .unit = text("m"),
                .min = 0.005f,
                .max = 3.0f }]]
            f32 size = 0.12f;

            [[=setting{
                .display = text("Size jitter"),
                .group = text("Shape"),
                .min = 0.0f,
                .max = 1.0f }]]
            f32 size_jitter = 0.25f;

            [[=setting{
                .display = text("Width"),
                .group = text("Shape"),
                .min = 0.02f,
                .max = 2.0f }]]
            f32 aspect = 0.55f;

            [[=setting{
                .display = text("Bend"),
                .group = text("Shape"),
                .min = 0.0f,
                .max = 1.0f }]]
            f32 bend = 0.15f;

            [[=setting{
                .display = text("Tilt"),
                .tooltip = text("Random tilt away from the plane"),
                .group = text("Orientation"),
                .unit = text("deg"),
                .min = 0.0f,
                .max = 90.0f }]]
            f32 tilt = 35.0f;

            [[=setting{
                .display = text("Two sided"),
                .group = text("Mesh") }]]
            bool two_sided = true;
        };


        struct [[=generator_info{
            .name = text("plant.leaf_planes"),
            .display = text("Leaf planes"),
            .description = text("Leaf cards scattered over the planes you placed in the view (role Leaf plane)"),
            .color = 0x7fd45f,
            .guide_roles = text("plant.leaves") }]] leaf_planes final : generator_of<leaf_plane_settings> {

            static std::vector<kind_id> emits() { return { kind_leaf_cluster() }; }

            void generate(generator_context& ctx, const leaf_plane_settings& s) override {

                const context& q = ctx.query();
                for (const element* g : ctx.guides()) {
                    const auto* surface = std::get_if<surface_shape>(&g->data);
                    if (!surface)
                        continue;
                    const geometry::mesh& m = surface->surface;
                    const f32 density = number(q, *g, tags::leaf_density(), s.density);
                    const f32 size = number(q, *g, tags::leaf_size(), s.size);
                    const f32 tilt = glm::radians(s.tilt);

                    std::vector<f32> cumulative;
                    f32 area = 0.0f;
                    std::vector<u32> vs;
                    for (u32 f = 0; f < m.face_count(); ++f) {
                        m.face_vertices(f, vs);
                        glm::vec3 cross(0.0f);
                        for (size_t i = 0; i < vs.size(); ++i)
                            cross += glm::cross(m.position(vs[i]), m.position(vs[(i + 1) % vs.size()]));
                        area += 0.5f * glm::length(cross) * g->placement.scale.x * g->placement.scale.z;
                        cumulative.push_back(area);
                    }
                    const u32 count = static_cast<u32>(std::min(density * area, static_cast<f32>(MAX_LEAVES_PER_CLUSTER)));
                    if (count == 0 || area <= 0.0f)
                        continue;

                    rng r = ctx.random(*g, 0x9a7e);
                    leaf_cluster_params cluster{};
                    cluster.two_sided = s.two_sided;
                    std::vector<glm::vec3> points;
                    for (u32 k = 0; k < count; ++k) {
                        const f32 pick = static_cast<f32>(r.uniform()) * area;
                        const u32 f = std::min(static_cast<u32>(std::ranges::lower_bound(cumulative, pick) - cumulative.begin()), m.face_count() - 1);
                        m.face_vertices(f, vs);
                        glm::vec3 p(0.0f);
                        f32 wsum = 0.0f;
                        for (const u32 v : vs) {
                            const f32 w = static_cast<f32>(r.uniform()) + 0.05f;
                            p += m.position(v) * w;
                            wsum += w;
                        }
                        p /= wsum;
                        const glm::vec3 n = glm::normalize(g->placement.rotation * m.face_normal(f));
                        const glm::vec3 t0 = perpendicular(n);
                        const f32 spin = static_cast<f32>(r.range(0.0, TAU));
                        const glm::vec3 along = t0 * std::cos(spin) + glm::cross(n, t0) * std::sin(spin);
                        const f32 lean = static_cast<f32>(r.range(-1.0, 1.0)) * tilt;
                        const glm::vec3 dir = glm::normalize(along * std::cos(lean) + n * std::sin(lean));

                        leaf_card card{};
                        card.base = glm::vec3(g->placement.to_world(p));
                        card.direction = dir;
                        card.normal = glm::normalize(n - dir * glm::dot(n, dir));
                        card.length = size * jitter(r, s.size_jitter);
                        card.width = card.length * s.aspect;
                        card.bend = s.bend;
                        cluster.cards.push_back(card);
                        points.push_back(card.base);
                    }
                    ctx.emit(*g, "leaves", -1, kind_leaf_cluster())
                        .placement(frame{})
                        .shape(point_shape{ std::move(points) })
                        .params(std::move(cluster));
                }
            }
        };

        // Realizers ------------------------------------------------------------------------------------------------------

        struct [[=realizer_info{ text("plant.branch") }]] branch_realizer final : realizer {

            static matcher match() { return { .kinds = { kind_branch() } }; }

            void realize(realize_context& ctx, const element& e) override {

                const geometry::curve* c = curve_of(e);
                if (!c)
                    return;
                const context& q = ctx.query();
                f32 base = 0.05f;
                if (const attribute_value v = c->attribute(attr_radius(), 0.0); v.index())
                    base = static_cast<f32>(as_number(v));
                const f32 sides_at_5cm = number(q, e, tags::sides(), 10.0f);
                const u32 sides = static_cast<u32>(std::clamp(static_cast<i32>(std::lround(sides_at_5cm * std::clamp(std::sqrt(base / 0.05f), 0.4f, 2.5f))),
                                                              3, 64));
                const bool caps = number(q, e, tags::caps(), 1.0f) != 0.0f;
                geometry::mesh tube = geometry::sweep_circle(*c, { .sides = sides, .radius = base, .radius_attribute = attr_radius(),
                                                                   .caps = caps, .segments_per_span = c->type() == geometry::curve_type::polyline ? 1u : 6u });
                for (u32 f = 0; f < tube.face_count(); ++f)
                    tube.set(attribute_domain::face, geometry::attr_material(), f, material_bark());
                ctx.mesh("bark", std::move(tube), 60.0f);
            }
        };


        struct [[=realizer_info{ text("plant.leaf_cluster") }]] leaf_realizer final : realizer {

            static matcher match() { return { .kinds = { kind_leaf_cluster() } }; }

            void realize(realize_context& ctx, const element& e) override {

                const leaf_cluster_params* cluster = e.params.get<leaf_cluster_params>();
                if (!cluster || cluster->cards.empty())
                    return;
                geometry::mesh m{};
                for (const leaf_card& card : cluster->cards)
                    add_leaf(m, card, cluster->two_sided);
                m.link_twins();
                ctx.mesh("leaves", std::move(m), 0.0f);
            }
        };

        // Policies -------------------------------------------------------------------------------------------------------

        struct [[=policy_info{ text("plant.outputs") }]] outputs_policy final : i_outputs {
            using hook = outputs_hook;
            void outputs(const output_query&, std::vector<output_desc>& out) const override {
                out.push_back({ .name = "bark", .purpose = output_purpose::render, .include = { .names = { "bark" } }, .topo = topology::merged_shells });
                out.push_back({ .name = "leaves", .purpose = output_purpose::render, .include = { .names = { "leaves" } }, .topo = topology::open_surface });
            }
        };


        // Plants are organic: numbers blend smoothly when a guide's cage is cut (the spaceship pack keeps them discrete)
        struct [[=policy_info{ text("plant.smooth_numbers") }]] smooth_numbers final : i_propagation {
            using hook = propagation_hook;
            void propagate(const attribute_desc& desc, const lineage_event& event, attribute_io& io, const next_propagation& next) const override {
                const bool numeric = io.source_count() > 0 && (std::holds_alternative<f32>(io.source(0)) || std::holds_alternative<f64>(io.source(0))
                                                            || std::holds_alternative<glm::vec2>(io.source(0)) || std::holds_alternative<glm::vec3>(io.source(0)));
                if (numeric && (event.kind == lineage_kind::split || event.kind == lineage_kind::subdivide)) {
                    apply_builtin_mode(intern("interpolate"), desc, event, io);
                    return;
                }
                next(desc, event, io);
            }
        };

    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    namespace {

        guide make_branch_guide(std::string name, const glm::dvec3& origin, std::vector<glm::vec3> points, std::vector<f32> radii) {

            guide g{};
            g.name = std::move(name);
            g.role = role_branch();
            g.placement.origin = origin;
            curve_guide c{};
            c.type = geometry::curve_type::catmull_rom;
            c.points = std::move(points);
            std::vector<attribute_value> r;
            for (const f32 v : radii)
                r.emplace_back(v);
            c.point_attributes.emplace_back(attr_radius(), std::move(r));
            g.shape = std::move(c);
            return g;
        }


        // A point on a branch guide (world) at parameter t, with the branch's radius there
        std::optional<std::pair<glm::dvec3, f32>> point_on(const guide& g, f64 t) {

            const auto* c = std::get_if<curve_guide>(&g.shape);
            if (!c || c->points.size() < 2)
                return std::nullopt;
            geometry::curve curve(c->type, c->points, c->closed);
            f32 radius = 0.05f;
            for (const auto& [id, values] : c->point_attributes)
                if (id == attr_radius())
                    for (u32 i = 0; i < values.size(); ++i)
                        curve.set_point_attribute(id, i, values[i]);
            if (curve.find_point_column(attr_radius()))
                radius = static_cast<f32>(as_number(curve.attribute(attr_radius(), t)));
            return std::pair{ g.placement.to_world(curve.sample(t).position), radius };
        }


        // Makes sure a stage of [type] exists (adding it with the pack's defaults)
        void ensure_stage(construct_data& data, std::string_view type) {

            if (std::ranges::any_of(data.generators, [&](const generator_instance& g) { return g.type == type; }))
                return;
            data.generators.push_back(make_stage(type, next_stage_id(data.generators), {}));
        }

    }


    void register_pack(i_registry& registry, std::string_view owner) {

        construct::register_pack<^^pack>(registry, owner, {
            .name = std::string(PACK_NAME),
            .version = { 0, 3, 0 },
            .extends = {},
            .depends_on = {},
            .display = "Plant",
            .description = "Plants as stages: trunk, drawn branches, branches / twigs, leaves, leaf planes",
            .upgrade = [](construct_data& data) { return upgrade(data); },
            .self_test = [](std::vector<std::string>& failures, u32& passed) { run_self_test(failures, passed); },
            .legacy_asset_types = { "construct_plant" },
        });

        // Editor declarations ---------------------------------------------------------------------------------------------
        const std::string pack(PACK_NAME);
        registry.add(owner, pack, role_desc{
            .name = "plant.branch",
            .display = "Branch spline",
            .description = "A branch you draw: grown by a Drawn branches stage (radius per point, wobble and snapping there)",
            .attributes = { tags::taper(), attr_radius() },
            .point_attributes = { attr_radius() },
        });
        registry.add(owner, pack, role_desc{
            .name = "plant.leaves",
            .display = "Leaf plane",
            .description = "Leaf cards are scattered over it by a Leaf planes stage",
            .attributes = { tags::leaf_density(), tags::leaf_size() },
            .point_attributes = {},
        });

        registry.add(owner, pack, guide_template_desc{
            .name = "plant.branch_spline",
            .display = "Branch spline",
            .description = "A branch you draw: starts on the selected branch spline (60 % along it), or at the ground. "
                           "Grown by the Drawn branches stage (added if missing)",
            .make = [](const construct_data&, const guide* selected) {
                glm::dvec3 at(0.0);
                f32 radius = 0.15f;
                if (selected && selected->role == role_branch())
                    if (auto p = point_on(*selected, 0.6)) {
                        radius = std::max(p->second * 0.6f, 0.01f);
                        return make_branch_guide("branch", p->first, { { 0, 0, 0 }, { 0.5f, 0.35f, 0.1f }, { 1.0f, 0.6f, 0.15f } },
                                                 { radius, radius * 0.75f, radius * 0.55f });
                    }
                return make_branch_guide("trunk", at, { { 0, 0, 0 }, { 0.05f, 1.2f, 0.0f }, { 0.0f, 2.5f, 0.0f } }, { radius, radius * 0.75f, radius * 0.5f });
            },
            .after_add = [](construct_data& data) { ensure_stage(data, STAGE_DRAWN); },
        });
        registry.add(owner, pack, guide_template_desc{
            .name = "plant.leaf_plane",
            .display = "Leaf plane",
            .description = "Leaf cards scattered over a plane (at the selected branch spline's tip). Grown by the Leaf planes stage (added if missing)",
            .make = [](const construct_data&, const guide* selected) {
                guide g{};
                g.name = "leaves";
                g.role = role_leaves();
                g.placement.origin = glm::dvec3(0.0, 2.5, 0.0);
                if (selected && selected->role == role_branch())
                    if (auto p = point_on(*selected, 1.0))
                        g.placement.origin = p->first;
                g.shape = surface_guide{ { 1.2f, 1.0f }, { 2, 2 } };
                return g;
            },
            .after_add = [](construct_data& data) { ensure_stage(data, STAGE_LEAF_PLANES); },
        });

        for (preset_desc& p : presets())
            registry.add(owner, pack, std::move(p));
    }


    generator_instance make_stage(std::string_view type, u32 id, std::vector<u32> inputs, u32 level) {

        generator_instance g{};
        g.id = id;
        g.type = std::string(type);
        g.inputs = std::move(inputs);
        if (type == STAGE_TRUNK) {
            g.label = "Trunk";
        } else if (type == STAGE_DRAWN) {
            g.label = "Drawn branches";
        } else if (type == STAGE_LEAVES) {
            g.label = "Leaves";
        } else if (type == STAGE_LEAF_PLANES) {
            g.label = "Leaf planes";
        } else if (type == STAGE_BRANCHES) {
            g.label = level <= 1 ? "Branches" : (level == 2 ? "Twigs" : std::format("Twigs (level {})", level));
            if (level >= 2) {
                g.set("mode", i32(1));
                g.set("density", level == 2 ? 6.0f : 9.0f);
                g.set("range", glm::vec2(0.2f, 0.98f));
                g.set("length", level == 2 ? 0.35f : 0.3f);
                g.set("angle", 55.0f);
                g.set("angle_tip", 40.0f);
                g.set("radius_ratio", 0.55f);
                g.set("gravity", 0.08f);
                g.set("light_seeking", 0.15f);
                g.set("wobble", 0.06f);
                g.set("sides", level == 2 ? 6 : 4);
                g.set("resolution", 8.0f);
            }
        } else {
            g.label = std::string(type);
        }
        return g;
    }

    namespace {

        construct_data broadleaf() {

            construct_data data{};
            data.packs = { std::string(PACK_NAME) };
            data.generators.push_back(make_stage(STAGE_TRUNK, 1, {}));
            data.generators.push_back(make_stage(STAGE_BRANCHES, 2, { 1 }, 1));
            data.generators.push_back(make_stage(STAGE_BRANCHES, 3, { 2 }, 2));
            data.generators.push_back(make_stage(STAGE_LEAVES, 4, { 3 }));
            return data;
        }


        construct_data conifer() {

            construct_data data{};
            data.packs = { std::string(PACK_NAME) };
            generator_instance trunk = make_stage(STAGE_TRUNK, 1, {});
            trunk.set("height", 11.0f);
            trunk.set("radius", 0.2f);
            trunk.set("radius_profile", profile_curve::of({ { 0.0f, 1.0f }, { 0.1f, 0.8f }, { 1.0f, 0.03f } }));
            trunk.set("lean", 1.0f);
            trunk.set("lean_variation", 1.5f);
            trunk.set("wobble", 0.05f);
            generator_instance whorls = make_stage(STAGE_BRANCHES, 2, { 1 }, 1);
            whorls.set("mode", i32(1));
            whorls.set("density", 7.0f);
            whorls.set("range", glm::vec2(0.12f, 0.98f));
            whorls.set("layout", i32(3));
            whorls.set("whorl", 5);
            whorls.set("angle", 88.0f);
            whorls.set("angle_tip", 65.0f);
            whorls.set("length_unit", i32(1));
            whorls.set("length", 3.0f);
            whorls.set("length_profile", profile_curve::of({ { 0.0f, 1.0f }, { 1.0f, 0.08f } }));
            whorls.set("radius_ratio", 0.35f);
            whorls.set("gravity", 0.35f);
            whorls.set("light_seeking", 0.05f);
            whorls.set("sides", 6);
            generator_instance twigs = make_stage(STAGE_BRANCHES, 3, { 2 }, 2);
            twigs.set("density", 3.0f);
            twigs.set("layout", i32(1));
            twigs.set("angle", 60.0f);
            twigs.set("angle_tip", 50.0f);
            twigs.set("gravity", 0.2f);
            generator_instance needles = make_stage(STAGE_LEAVES, 4, { 2, 3 });
            needles.set("density", 22.0f);
            needles.set("tip_count", 2);
            needles.set("size", 0.07f);
            needles.set("aspect", 0.12f);
            needles.set("bend", 0.0f);
            needles.set("angle", 45.0f);
            needles.set("face_up", 0.1f);
            data.generators = { trunk, whorls, twigs, needles };
            return data;
        }


        construct_data bush() {

            construct_data data{};
            data.packs = { std::string(PACK_NAME) };
            generator_instance stems = make_stage(STAGE_TRUNK, 1, {});
            stems.set("count", 6);
            stems.set("spread", 0.2f);
            stems.set("height", 1.4f);
            stems.set("radius", 0.025f);
            stems.set("flare", 0.0f);
            stems.set("lean", 25.0f);
            stems.set("lean_variation", 15.0f);
            stems.set("wobble", 0.06f);
            stems.set("wobble_frequency", 1.2f);
            stems.set("sides", 6);
            generator_instance branches = make_stage(STAGE_BRANCHES, 2, { 1 }, 2);
            branches.set("density", 5.0f);
            branches.set("length", 0.5f);
            branches.label = "Branches";
            generator_instance leaves = make_stage(STAGE_LEAVES, 3, { 1, 2 });
            leaves.set("density", 30.0f);
            leaves.set("size", 0.07f);
            leaves.set("range", glm::vec2(0.15f, 1.0f));
            data.generators = { stems, branches, leaves };
            return data;
        }


        guide drawn_branch(std::string name, i64 index, glm::vec3 origin, std::vector<glm::vec3> points, std::vector<f32> radii) {
            guide g{};
            g.name = std::move(name);
            g.index = index;
            g.role = role_branch();
            g.placement.origin = glm::dvec3(origin);
            curve_guide c{};
            c.type = geometry::curve_type::catmull_rom;
            c.points = std::move(points);
            std::vector<attribute_value> r;
            for (const f32 v : radii)
                r.emplace_back(v);
            c.point_attributes.emplace_back(attr_radius(), std::move(r));
            g.shape = std::move(c);
            return g;
        }


        construct_data hand_drawn() {

            construct_data data{};
            data.packs = { std::string(PACK_NAME) };
            data.guides.push_back(drawn_branch("trunk", -1, { 0, 0, 0 }, { { 0, 0, 0 }, { 0.05f, 1.2f, 0.0f }, { -0.05f, 2.4f, 0.05f }, { 0.0f, 3.4f, 0.0f } },
                                               { 0.22f, 0.17f, 0.13f, 0.08f }));
            data.guides.push_back(drawn_branch("branch", 0, { 0.0f, 1.9f, 0.0f }, { { 0, 0, 0 }, { 0.6f, 0.45f, 0.1f }, { 1.3f, 0.8f, 0.2f } }, { 0.09f, 0.07f, 0.04f }));
            data.guides.push_back(drawn_branch("branch", 1, { -0.02f, 2.5f, 0.03f }, { { 0, 0, 0 }, { -0.5f, 0.4f, 0.4f }, { -1.0f, 0.75f, 0.9f } }, { 0.08f, 0.06f, 0.035f }));
            data.guides.push_back(drawn_branch("branch", 2, { 0.0f, 3.0f, 0.0f }, { { 0, 0, 0 }, { 0.1f, 0.4f, -0.6f }, { 0.25f, 0.7f, -1.1f } }, { 0.07f, 0.05f, 0.03f }));
            guide leaves{};
            leaves.name = "leaves";
            leaves.role = role_leaves();
            leaves.placement.origin = glm::dvec3(0.0, 3.7, 0.0);
            leaves.placement.rotation = glm::quat(glm::vec3(0.15f, 0.0f, -0.1f));
            leaves.shape = surface_guide{ { 1.6f, 1.4f }, { 2, 2 } };
            data.guides.push_back(std::move(leaves));
            upgrade(data);
            return data;
        }


    }


    std::vector<preset_desc> presets() {

        return {
            { "plant.broadleaf", "Broadleaf tree", "Trunk, branches, twigs and leaves: everything follows the seed", &broadleaf },
            { "plant.conifer", "Conifer", "Straight tapering trunk with whorls of drooping branches and needles", &conifer },
            { "plant.bush", "Bush", "Several thin stems from one spot, dense small leaves", &bush },
            { "plant.hand_drawn", "Hand drawn", "A trunk and branches drawn as splines, plus a leaf plane; twigs and leaves grow on them", &hand_drawn },
        };
    }


    construct_data make_default_tree() { return broadleaf(); }


    bool upgrade(construct_data& data) {

        if (!data.generators.empty())
            return false;
        const bool branches = std::ranges::any_of(data.guides, [](const guide& g) { return g.role == role_branch(); });
        const bool planes = std::ranges::any_of(data.guides, [](const guide& g) { return g.role == role_leaves(); });
        if (!branches && !planes)
            return false;
        if (branches) {
            data.generators.push_back(make_stage(STAGE_DRAWN, 1, {}));
            data.generators.push_back(make_stage(STAGE_BRANCHES, 2, { 1 }, 2));
            data.generators.push_back(make_stage(STAGE_LEAVES, 3, { 2 }));
        }
        if (planes)
            data.generators.push_back(make_stage(STAGE_LEAF_PLANES, next_stage_id(data.generators), {}));
        return true;
    }

}
