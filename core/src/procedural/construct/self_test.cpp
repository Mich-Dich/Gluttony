#include "procedural/construct/self_test.h"

#include "procedural/construct/core_pack.h"
#include "procedural/construct/demo_pack.h"
#include "procedural/construct/reflect.h"
#include "procedural/construct/serialize.h"
#include "procedural/construct/evaluation.h"

#include <chrono>



// Test pack content (registered per test into fresh registries) ======================================================

namespace GLT::construct::test_pack {

    // A hook of the tests: which policy answered
    class i_choice {
    public:
        virtual ~i_choice() = default;
        [[nodiscard]] virtual i32 value() const = 0;
    };

    struct choice_hook {
        using interface = i_choice;
        static constexpr std::string_view name = "test.choice";
        static constexpr composition default_composition = composition::first_match;
    };

    struct fixed_choice final : i_choice {
        explicit fixed_choice(i32 v) : m_value(v) {}
        [[nodiscard]] i32 value() const override { return m_value; }
        i32 m_value;
    };

    struct [[=tag_schema{ text("test") }]] test_tags {
        [[=attr{ .propagation = text("decay"), .rate = 0.5f }]] f32 vigor = 1.0f;
    };

    struct [[=kind_info{ text("test.node"), shape_kind::frame }]] node_params {
        [[=param{ .display = text("Size") }]] f32 size = 2.0f;
    };

    // Endless recursion: every element of kind test.node gets one child of the same kind
    struct [[=production_info{ text("test.recurse"), text("test.recursive") }]] recurse final : production {
        static matcher match() { return { .kinds = { intern("test.node"), kind_guide_point() } }; }
        void expand(expand_context& ctx, const element& e) override {
            ctx.emit(e, "child", 0, intern("test.node")).shape(frame_shape{});
        }
    };

    // Records the stage order it sees
    struct [[=stage_info{ text("test.after_expand") }]] after_expand final : stage {
        static std::vector<std::string> after() { return { "core.expand" }; }
        static std::vector<std::string> before() { return { "core.realize" }; }
        void run(stage_context& ctx) override { ctx.diagnose(severity::info, "test.after_expand", "ran"); }
    };

    struct [[=stage_info{ text("test.wrap_realize"), stage_mode::wrap, text("core.realize") }]] wrap_realize final : stage_wrapper {
        void run(stage_context& ctx) override {
            ctx.diagnose(severity::info, "test.wrap_realize", "before");
            run_next(ctx);
            ctx.diagnose(severity::info, "test.wrap_realize", std::format("after: {} products", ctx.products().size()));
        }
    };

    struct [[=stage_info{ text("test.replace_finish"), stage_mode::replace, text("core.finish") }]] replace_finish final : stage {
        void run(stage_context& ctx) override { ctx.diagnose(severity::info, "test.replace_finish", "replaced"); }
    };

    struct [[=policy_info{ text("test.outputs") }]] test_outputs final : i_outputs {
        using hook = outputs_hook;
        void outputs(const output_query&, std::vector<output_desc>& out) const override {
            out.push_back({ .name = "all", .purpose = output_purpose::render, .include = {}, .topo = topology::merged_shells, .lods = true });
            out.push_back({ .name = "posts", .purpose = output_purpose::render, .include = { .names = {}, .kinds = { intern("demo.post") } },
                            .topo = topology::shells_per_element, .lods = true });
            out.push_back({ .name = "rails", .purpose = output_purpose::collision, .include = { .names = { "top_rail", "mid_rail" } },
                            .topo = topology::as_realized, .lods = false });
        }
    };

}



// Stage (generator) test pack =====================================================================================

namespace GLT::construct::test_stage_pack {

    enum class layout { line, ring };

    struct points_settings {
        [[=setting{ .display = text("Count"), .group = text("Distribution"), .min = 0.0f, .max = 50.0f }]]
        i32 count = 3;
        [[=setting{ .group = text("Shape"), .unit = text("m") }]]
        f32 spacing = 1.0f;
        layout arrangement = layout::ring;
        profile_curve scale = profile_curve::linear(1.0f, 0.5f);
        bool jitter = true;
    };

    // Points in a line (or ring) at the root; jittered by the seed
    struct [[=generator_info{ .name = text("test.points"), .display = text("Points") }]] points final : generator_of<points_settings> {
        static std::vector<kind_id> emits() { return { intern("test.point") }; }
        void generate(generator_context& ctx, const points_settings& s) override {
            rng r = ctx.random(1);
            for (i32 i = 0; i < s.count; ++i) {
                frame f{};
                const f64 jitter = s.jitter ? r.uniform() * 0.1 : 0.0;
                f.origin = s.arrangement == layout::line ? glm::dvec3(i * s.spacing + jitter, 0.0, 0.0)
                                                         : glm::dvec3(std::cos(i) * s.spacing, jitter, std::sin(i) * s.spacing);
                ctx.emit_root("p", i, intern("test.point")).placement(f).shape(frame_shape{});
            }
        }
    };

    struct children_settings { i32 per_parent = 2; };

    struct [[=generator_info{ .name = text("test.children"), .takes_input = true }]] children final : generator_of<children_settings> {
        static void on_add(generator_instance& g, const construct_data&) { g.label = "Added children"; }
        void generate(generator_context& ctx, const children_settings& s) override {
            for (const element* in : ctx.inputs())
                for (i32 k = 0; k < s.per_parent; ++k)
                    ctx.emit(*in, "c", k, intern("test.point")).shape(frame_shape{});
        }
    };

}

// A module's root namespace: packs are the annotated namespaces below it (register_packs_in)
namespace GLT::construct::test_module_root {

    namespace [[=pack{ .name = text("test.annotated"), .display = text("Annotated"), .major = 2, .minor = 3, .patch = 4,
                       .depends_on = text("core") }]] annotated {

        struct one_settings { i32 count = 1; };

        struct [[=generator_info{ .name = text("test.annotated.one") }]] one final : generator_of<one_settings> {
            void generate(generator_context&, const one_settings&) override {}
        };

        inline void customize_pack(pack_desc& desc) { desc.legacy_asset_types = { "annotated_legacy" }; }

        inline void register_extras(i_registry& registry, std::string_view owner, const std::string& pack) {
            registry.add(owner, pack, preset_desc{ .name = "test.annotated.preset", .display = "Preset", .description = {}, .make = {} });
        }
    }

    namespace not_a_pack {
        struct [[=generator_info{ .name = text("test.unpacked") }]] unpacked final : generator_of<annotated::one_settings> {
            void generate(generator_context&, const annotated::one_settings&) override {}
        };
    }
}

namespace GLT::construct {

    struct demo_pack_placeholder { int x = 0; };

    // FUNCTION IMPLEMENTATION =========================================================================================

    namespace {

        struct runner {

            self_test_report&                           report;

            void check(bool ok, std::string_view what) {
                if (ok) {
                    ++report.passed;
                } else {
                    ++report.failed;
                    report.failures.emplace_back(what);
                }
            }

            template<typename A, typename B>
            void equal(const A& a, const B& b, std::string_view what) {
                if (a == b)
                    ++report.passed;
                else {
                    ++report.failed;
                    if constexpr (std::formattable<A, char> && std::formattable<B, char>)
                        report.failures.push_back(std::format("{}: got {}, expected {}", what, a, b));
                    else
                        report.failures.emplace_back(what);
                }
            }
        };


        std::shared_ptr<const registry_snapshot> fresh_snapshot(bool demo) {

            registry reg;
            register_core_pack(reg, "test");
            if (demo)
                register_demo_pack(reg, "test");
            return reg.snapshot();
        }


        bool has_diag(const evaluation_result& r, std::string_view source, std::string_view text_part) {

            return std::ranges::any_of(r.diagnostics, [&](const diagnostic& d) {
                return d.source == source && d.message.find(text_part) != std::string::npos;
            });
        }


        std::string first_error(const evaluation_result& r) {

            for (const diagnostic& d : r.diagnostics)
                if (d.level == severity::error)
                    return std::format("[{}] {}: {}", d.source, d.element.to_string(), d.message);
            return {};
        }

        // ---- tests ---------------------------------------------------------------------------------------------------

        void test_paths(runner& t) {

            const element_path p = element_path::root("hull").child("frames").child("station", 12).child("rib");
            t.equal(p.to_string(), std::string("hull/frames/station[12]/rib"), "path to_string");
            auto parsed = element_path::parse("hull/frames/station[12]/rib");
            t.check(parsed.has_value() && *parsed == p, "path parse round trip");
            t.check(parsed && parsed->hash() == p.hash(), "path hash equal after parse");
            t.check(!element_path::parse("a/[3]").has_value(), "path parse rejects an empty segment");
            t.check(!element_path::parse("a/b[x]").has_value(), "path parse rejects a bad index");
            t.check(element_path::root("a").child("b", 2) < element_path::root("a").child("b", 10), "path order by index");
            t.check(element_path::root("a") < element_path::root("b"), "path order by text");
            t.check(p.parent().is_prefix_of(p), "path parent is a prefix");
            t.check(intern("ship.rib") == intern(std::string("ship.") + "rib"), "intern: equal names are equal ids");
            t.check(intern("ship.rib").str() == "ship.rib", "intern: text kept");
            t.check(intern("").empty(), "intern: empty name is the empty id");
        }


        void test_schema_reflection(runner& t) {

            auto snap = fresh_snapshot(true);
            const tag_schema_desc* core = nullptr;
            const tag_schema_desc* demo = nullptr;
            for (const auto& s : snap->tag_schemas) {
                if (s.desc->name == "core") core = s.desc.get();
                if (s.desc->name == "demo") demo = s.desc.get();
            }
            t.check(core && demo, "reflection: core and demo schemas registered");
            if (!core || !demo)
                return;
            auto find = [](const tag_schema_desc& s, std::string_view id) -> const attribute_desc* {
                for (const auto& a : s.attributes)
                    if (a.id == intern(id))
                        return &a;
                return nullptr;
            };
            const attribute_desc* radius = find(*core, "core.radius");
            t.check(radius && std::get<f32>(radius->default_value) == 0.05f, "reflection: core.radius default from the member initializer");
            t.check(radius && radius->propagation == intern("interpolate"), "reflection: core.radius propagation from the annotation");
            t.check(radius && has_domain(radius->domains, attribute_domain::curve), "reflection: core.radius domains");
            const attribute_desc* spacing = find(*demo, "demo.spacing");
            t.check(spacing && spacing->display == "Post spacing" && spacing->affects_topology, "reflection: demo.spacing display / flags");
            t.check(attribute_of<^^test_pack::test_tags::vigor>() == intern("test.vigor"), "reflection: attribute_of<^^member>");

            const kind_desc* post = nullptr;
            for (const auto& k : snap->kinds)
                if (k.desc->name == "demo.post") post = k.desc.get();
            t.check(post && post->params.size() == 2 && post->params[0].display == "Height", "reflection: kind params");
            t.check(post && post->make_default && !post->make_default().empty(), "reflection: kind default params block");
            const tag_schema_desc test_schema = reflect_tag_schema<test_pack::test_tags>();
            t.check(test_schema.attributes.size() == 1 && test_schema.attributes[0].rate == 0.5f, "reflection: rate from the annotation");
            const kind_desc node = reflect_kind<test_pack::node_params>();
            const param_block block = node.make_default();
            t.check(block.get<test_pack::node_params>() && block.get<test_pack::node_params>()->size == 2.0f, "reflection: params block holds the default struct");
            t.check(block.get<demo_pack_placeholder>() == nullptr, "reflection: params block rejects another type");

            u32 stages = 0;
            for (const auto& s : snap->stages)
                if (s.pack == "core") ++stages;
            t.equal(stages, 6u, "reflection: core stages registered");
        }


        void test_mesh_and_kernel(runner& t) {

            using namespace geometry;
            const mesh box = make_primitive(primitive_kind::box, {});
            t.equal(box.vertex_count(), 8u, "box vertices");
            t.equal(box.face_count(), 6u, "box faces");
            t.check(box.validate().empty(), "box half-edges valid: " + box.validate());
            bool closed = true;
            for (u32 h = 0; h < box.halfedge_count(); ++h)
                closed = closed && box.he(h).twin != INVALID_INDEX;
            t.check(closed, "box is closed (every half-edge has a twin)");
            bool outward = true;
            for (u32 f = 0; f < box.face_count(); ++f)
                outward = outward && glm::dot(box.face_normal(f), box.face_centroid(f)) > 0.0f;
            t.check(outward, "box faces point outward");

            const mode_propagator copy{};
            const mesh cut = loop_cut(box, 0, std::array{ 0.5f }, copy);
            t.check(cut.validate().empty(), "loop cut result valid: " + cut.validate());
            t.equal(cut.vertex_count(), 12u, "loop cut on a box adds 4 vertices");
            t.equal(cut.face_count(), 10u, "loop cut on a box: 4 ring faces split in two");
            const mesh cut2 = loop_cut(box, 0, std::array{ 0.25f, 0.75f }, copy);
            t.equal(cut2.face_count(), 14u, "two loop cuts at once: ring faces split in three");

            const mesh cyl = make_primitive(primitive_kind::cylinder, { .segments = 12 });
            t.check(cyl.validate().empty(), "cylinder valid");
            u32 side_h = INVALID_INDEX;                     // a vertical side edge: its ring goes around the cylinder
            for (u32 h = 0; h < cyl.halfedge_count() && side_h == INVALID_INDEX; ++h) {
                const glm::vec3 d = cyl.position(cyl.he(h).vertex) - cyl.position(cyl.from_vertex(h));
                if (std::abs(d.y) > 0.5f)
                    side_h = h;
            }
            const edge_ring ring = find_edge_ring(cyl, side_h);
            t.check(ring.closed && ring.entries.size() == 12, "cylinder: ring around the side is closed with 12 faces");
            const mesh cyl_cut = loop_cut(cyl, side_h, std::array{ 0.5f }, copy);
            t.check(cyl_cut.validate().empty(), "cylinder loop cut valid");
            t.equal(cyl_cut.face_count(), 26u, "cylinder loop cut: 12 side faces split, 2 caps kept");

            const mesh sphere = make_primitive(primitive_kind::sphere, { .radius = 1.0f, .segments = 16, .rings = 8 });
            t.check(sphere.validate().empty(), "sphere valid");
            bool sphere_out = true;
            for (u32 f = 0; f < sphere.face_count(); ++f)
                sphere_out = sphere_out && glm::dot(sphere.face_normal(f), sphere.face_centroid(f)) > 0.0f;
            t.check(sphere_out, "sphere faces point outward");
            const mesh capsule = make_primitive(primitive_kind::capsule, { .radius = 0.5f, .height = 1.0f, .segments = 12, .rings = 8 });
            t.check(capsule.validate().empty(), "capsule valid");

            // Triangulation: hard box edges split normals (24 corners), a sphere shares them
            const triangle_mesh tb = triangulate(box);
            t.equal(tb.indices.size(), size_t(36), "box triangulates to 12 triangles");
            t.equal(tb.positions.size(), size_t(24), "box: hard edges give 24 output vertices");
            const triangle_mesh ts = triangulate(sphere, 40.0f);
            t.check(ts.positions.size() < sphere.halfedge_count(), "sphere: smooth corners are shared");

            // Mirror: still valid and outward
            const mesh mirrored = mirror(box, glm::vec4(1, 0, 0, -2), copy);
            bool m_out = mirrored.validate().empty();
            for (u32 f = 0; f < mirrored.face_count(); ++f)
                m_out = m_out && glm::dot(mirrored.face_normal(f), mirrored.face_centroid(f) - glm::vec3(4, 0, 0)) > 0.0f;
            t.check(m_out, "mirror: valid, faces outward");

            // Curves: frames orthonormal, sweep valid
            const curve c(curve_type::catmull_rom, { { 0, 0, 0 }, { 1, 0, 0 }, { 2, 1, 0 }, { 2, 2, 1 } });
            bool ortho = true;
            for (const curve_sample& s : c.samples())
                ortho = ortho && std::abs(glm::dot(s.tangent, s.normal)) < 1e-3f && std::abs(glm::length(s.normal) - 1.0f) < 1e-3f;
            t.check(ortho, "curve: rotation-minimizing frames are orthonormal");
            t.check(std::abs(c.sample(1.0).position.x - 2.0f) < 1e-4f && std::abs(c.sample(1.0).position.z - 1.0f) < 1e-4f, "curve: t = 1 is the last point");
            const mesh tube = sweep_circle(c, { .sides = 8, .radius = 0.1f });
            t.check(tube.validate().empty(), "sweep valid");
            bool tube_closed = true;
            for (u32 h = 0; h < tube.halfedge_count(); ++h)
                tube_closed = tube_closed && tube.he(h).twin != INVALID_INDEX;
            t.check(tube_closed, "sweep with caps is closed");
            const curve loop(curve_type::catmull_rom, { { 1, 0, 0 }, { 0, 0, 1 }, { -1, 0, 0 }, { 0, 0, -1 } }, true);
            const mesh ring_tube = sweep_circle(loop, { .sides = 6, .radius = 0.1f });
            bool ring_closed = ring_tube.validate().empty();
            for (u32 h = 0; h < ring_tube.halfedge_count(); ++h)
                ring_closed = ring_closed && ring_tube.he(h).twin != INVALID_INDEX;
            t.check(ring_closed, "sweep along a closed curve is a closed torus");
        }


        // The C0 acceptance test of the design: one loop cut, two propagation rule sets
        void test_two_policies(runner& t) {

            using namespace geometry;
            const attribute_id armor = intern("test.armor"), v = intern("test.v");
            mesh box = make_primitive(primitive_kind::box, { .size = glm::vec3(2.0f, 1.0f, 1.0f) });
            for (u32 f = 0; f < box.face_count(); ++f)
                box.set(attribute_domain::face, armor, f, i32(3));
            for (u32 i = 0; i < box.vertex_count(); ++i)
                box.set(attribute_domain::vertex, v, i, box.position(i).x < 0.0f ? 1.0f : 0.2f);
            for (u32 h = 0; h < box.halfedge_count(); ++h)
                box.set(attribute_domain::edge, attr_hard(), h, false);  // all soft: only the new loop can become hard

            u32 along_x = INVALID_INDEX;                    // an edge along x: its ring cuts across x
            for (u32 h = 0; h < box.halfedge_count() && along_x == INVALID_INDEX; ++h) {
                const glm::vec3 d = box.position(box.he(h).vertex) - box.position(box.from_vertex(h));
                if (std::abs(d.x) > 1.0f)
                    along_x = h;
            }

            tag_schema_desc ship = tag_schema_builder("test").add("armor", i32(0), attribute_domain::face, "copy")
                                                             .add("v", 0.0f, attribute_domain::vertex, "copy").build();
            tag_schema_desc hard = tag_schema_builder("core").add("hard", false, attribute_domain::edge, "boundary").build();
            tag_schema_desc plant = tag_schema_builder("test").add("armor", i32(0), attribute_domain::face, "copy")
                                                              .add("v", 0.0f, attribute_domain::vertex, "interpolate").build();
            const std::array ship_schemas{ ship, hard };
            const mode_propagator ship_rules(intern("copy"), ship_schemas);
            const mode_propagator plant_rules(intern("copy"), std::span(&plant, 1));

            const mesh a = loop_cut(box, along_x, std::array{ 0.5f }, ship_rules);
            const mesh b = loop_cut(box, along_x, std::array{ 0.5f }, plant_rules);

            bool armor_kept = true;
            for (u32 f = 0; f < a.face_count(); ++f)
                armor_kept = armor_kept && a.get(attribute_domain::face, armor, f) == attribute_value(i32(3));
            t.check(armor_kept, "two policies: both halves keep armor = 3 (copy)");

            bool discrete = true, blended = true;
            for (u32 i = box.vertex_count(); i < a.vertex_count(); ++i) {
                const f32 va = std::get<f32>(a.get(attribute_domain::vertex, v, i));
                const f32 vb = std::get<f32>(b.get(attribute_domain::vertex, v, i));
                discrete = discrete && (va == 1.0f || va == 0.2f);
                blended = blended && std::abs(vb - 0.6f) < 1e-5f;
            }
            t.check(discrete, "two policies: spaceship rules keep new vertex values discrete");
            t.check(blended, "two policies: plant rules interpolate new vertex values to 0.6");

            u32 seams = 0;
            for (u32 h = 0; h < a.halfedge_count(); ++h) {
                const attribute_value& hv = a.get(attribute_domain::edge, attr_hard(), h);
                if (const bool* hb = std::get_if<bool>(&hv); hb && *hb) {
                    const glm::vec3 p0 = a.position(a.from_vertex(h)), p1 = a.position(a.he(h).vertex);
                    if (std::abs(p0.x) < 1e-5f && std::abs(p1.x) < 1e-5f)
                        ++seams;
                }
            }
            t.equal(seams, 8u, "two policies: the new edge loop is marked hard (boundary mode, 4 edges x 2 half-edges)");
            bool plant_soft = true;
            for (u32 h = 0; h < b.halfedge_count(); ++h) {
                const glm::vec3 p0 = b.position(b.from_vertex(h)), p1 = b.position(b.he(h).vertex);
                if (std::abs(p0.x) < 1e-5f && std::abs(p1.x) < 1e-5f)
                    plant_soft = plant_soft && b.get(attribute_domain::edge, attr_hard(), h) == attribute_value(false);
            }
            t.check(plant_soft, "two policies: plant rules leave the new loop soft (copy of the rails)");
            t.check(a.hash() != b.hash(), "two policies: results differ");
        }


        void test_policies(runner& t) {

            registry reg;
            register_core_pack(reg, "test");
            reg.add_pack("test", { .name = "a" });
            reg.add_pack("test", { .name = "b" });
            using test_pack::choice_hook;
            using test_pack::fixed_choice;
            auto add = [&](std::string_view pack, std::string name, i32 v, policy_scope scope) {
                reg.add("test", pack, policy_desc::make<choice_hook, fixed_choice>(std::move(name), std::move(scope), std::make_shared<const fixed_choice>(v)));
            };
            add("core", "global", 1, { .level = scope_level::pack });               // core pack-level = global
            add("a", "pack_a", 2, { .level = scope_level::pack });
            add("b", "pack_b", 3, { .level = scope_level::pack });
            add("a", "by_kind", 4, { .level = scope_level::kind, .kind = kind_guide_volume() });
            add("a", "by_tag", 5, { .level = scope_level::attribute, .attribute = intern("test.special"), .value = true });
            add("a", "by_path", 6, { .level = scope_level::element, .path = element_path::root("pinned") });
            add("a", "construct_wide", 7, { .level = scope_level::pack });

            auto data = std::make_shared<construct_data>();
            data->packs = { "a", "b" };
            guide plain{ .name = "plain", .shape = point_guide{} };
            guide vol{ .name = "vol", .shape = volume_guide{} };
            guide special{ .name = "special", .shape = point_guide{} };
            special.attributes.set(intern("test.special"), true);
            guide pinned{ .name = "pinned", .shape = volume_guide{} };
            special.attributes.set(intern("test.special"), true);
            data->guides = { plain, vol, special, pinned };

            auto snap = reg.snapshot();
            evaluation_request req{ .data = data };
            evaluation e(*snap, req);
            e.interpret();
            const context ctx(e);
            auto value_of = [&](std::string_view path) {
                const element& el = ctx.get(*ctx.find(*element_path::parse(path)));
                auto view = ctx.policy<choice_hook>(el);
                return view ? view->value() : -1;
            };
            t.equal(value_of("plain"), 3, "policy: pack b (listed after a) beats pack a");
            t.equal(value_of("vol"), 4, "policy: kind scope beats pack scope");
            t.equal(value_of("special"), 5, "policy: attribute scope beats kind / pack");
            t.equal(value_of("pinned"), 6, "policy: element scope beats everything");
            {
                const element& el = ctx.get(*ctx.find(element_path::root("plain")));
                auto view = ctx.policy<choice_hook>(el);
                t.equal(view.matches().size(), size_t(4), "policy: all matching policies are listed (b, a, a, global)");
                t.equal(view.matches().back()->value(), 1, "policy: the global default comes last");
            }

            // Construct binding at construct level beats the packs; composition override per construct
            data->policies.push_back({ .policy = "construct_wide", .scope = std::nullopt });
            data->compositions.push_back({ "test.choice", composition::all });
            evaluation e2(*snap, req);
            e2.interpret();
            const context ctx2(e2);
            const element& plain2 = ctx2.get(*ctx2.find(element_path::root("plain")));
            auto view2 = ctx2.policy<choice_hook>(plain2);
            t.equal(view2->value(), 7, "policy: a construct binding beats pack defaults");
            t.check(view2.mode() == composition::all, "policy: the construct can change a hook's composition");
            const element& vol2 = ctx2.get(*ctx2.find(element_path::root("vol")));
            t.equal(ctx2.policy<choice_hook>(vol2)->value(), 4, "policy: a kind scope still beats a construct-wide binding");
        }


        void test_demo(runner& t) {

            auto snap = fresh_snapshot(true);
            auto data = make_demo_construct();
            const evaluation_result r = evaluate(*snap, { .data = data, .seed = 7 });
            t.check(r.ok(), "demo: evaluates without errors " + first_error(r));
            const output_result* render = r.output("render");
            t.check(render != nullptr, "demo: has the default render output");
            if (!render)
                return;
            t.equal(render->parts.size(), size_t(1), "demo: merged shells give one part");

            u32 posts = 0;
            bool mirror_found = false;
            for (const element_summary& s : r.elements) {
                if (s.kind == intern("demo.post")) ++posts;
                if (s.path.to_string() == "buoy/mirror") mirror_found = true;
            }
            t.check(posts >= 5 && posts <= 7, std::format("demo: posts every 0.8 m along a ~4.3 m rail (got {})", posts));
            t.check(mirror_found, "demo: mirror modifier made buoy/mirror");
            t.equal(render->markers.size(), size_t(1), "demo: the lamp point becomes one marker");

            u32 stripe_triangles = 0, metal_triangles = 0;
            for (const output_group& g : render->parts[0].groups) {
                if (g.material == intern("deck_stripe")) stripe_triangles += static_cast<u32>(g.mesh.indices.size() / 3);
                if (g.material == intern("metal")) metal_triangles += static_cast<u32>(g.mesh.indices.size() / 3);
            }
            t.equal(stripe_triangles, 2u, "demo: the middle deck strip (one quad after the loop cuts) has its own material");
            t.check(metal_triangles > 0, "demo: posts and rails are metal");

            // Determinism: same input -> same bytes; guide order does not matter
            const evaluation_result again = evaluate(*snap, { .data = data, .seed = 7 });
            t.equal(again.hash(), r.hash(), "demo: deterministic (same hash twice)");
            auto shuffled = std::make_shared<construct_data>(*data);
            std::ranges::reverse(shuffled->guides);
            t.equal(evaluate(*snap, { .data = shuffled, .seed = 7 }).hash(), r.hash(), "demo: guide order does not change the result");

            // Hiding an element by path removes its products only
            auto hidden = std::make_shared<construct_data>(*data);
            hidden->overrides.push_back({ .path = *element_path::parse("railing/post[0]"), .hidden = true });
            const evaluation_result rh = evaluate(*snap, { .data = hidden, .seed = 7 });
            t.check(rh.output("render") && rh.output("render")->triangle_count() == render->triangle_count() - 12,
                    "demo: hiding railing/post[0] removes exactly one post (12 triangles)");

            // LOD: dropping the primary pass removes the posts
            const evaluation_result lod = evaluate(*snap, { .data = data, .seed = 7, .lod = { .level = 1, .dropped_passes = { intern("primary") } } });
            t.check(std::ranges::none_of(lod.elements, [](const element_summary& s) { return s.kind == intern("demo.post"); }),
                    "demo: a LOD request that drops the primary pass has no posts");

            // Cancellation
            std::atomic<bool> cancel{ true };
            const evaluation_result rc = evaluate(*snap, { .data = data, .seed = 7, .cancel = &cancel });
            t.check(rc.cancelled && !rc.ok(), "demo: a cancelled request reports cancelled");
        }


        void test_stages_outputs_recursion(runner& t) {

            registry reg;
            register_core_pack(reg, "test");
            register_demo_pack(reg, "test");
            reg.add("test", "core", pass_desc{ .name = "test.recursive", .after = { "detail" }, .max_depth = 5 });
            register_pack<^^test_pack>(reg, "test", { .name = "test", .depends_on = { "demo" } });

            auto data = make_demo_construct();
            data->packs = { "test" };
            guide seed{ .name = "seed", .shape = point_guide{} };
            seed.attributes.set(attr_hidden(), true);
            data->guides.push_back(seed);
            auto snap = reg.snapshot();
            const evaluation_result r = evaluate(*snap, { .data = data });

            // Stages
            const std::string order = [&] { std::string s; for (const auto& n : r.stage_order) s += n + " | "; return s; }();
            t.check(std::ranges::find(r.stage_order, std::string("test.after_expand")) != r.stage_order.end(), "stages: inserted stage runs: " + order);
            const auto pos = [&](std::string_view prefix) {
                for (size_t i = 0; i < r.stage_order.size(); ++i)
                    if (r.stage_order[i].starts_with(prefix)) return static_cast<i32>(i);
                return -1;
            };
            t.check(pos("core.expand") < pos("test.after_expand") && pos("test.after_expand") < pos("core.realize"), "stages: insert respects after / before");
            t.check(has_diag(r, "test.wrap_realize", "before") && has_diag(r, "test.wrap_realize", "after"), "stages: wrapper runs around core.realize");
            t.check(has_diag(r, "test.replace_finish", "replaced"), "stages: core.finish replaced");

            // Recursion with a depth limit
            u32 nodes = 0;
            for (const element_summary& s : r.elements)
                if (s.kind == intern("test.node")) ++nodes;
            t.equal(nodes, 10u, "recursion: stops at the pass depth limit (5 levels for each of the 2 points)");
            t.check(has_diag(r, "test.recurse", "depth limit"), "recursion: the depth limit is reported");

            // Several outputs with different topologies
            t.check(r.output("render") == nullptr, "outputs: a pack's outputs replace the core default");
            const output_result* all = r.output("all");
            const output_result* posts = r.output("posts");
            const output_result* rails = r.output("rails");
            t.check(all && posts && rails, "outputs: three outputs at once");
            if (all && posts && rails) {
                t.equal(all->parts.size(), size_t(1), "outputs: merged shells -> one part");
                u32 post_count = 0;
                for (const element_summary& s : r.elements)
                    if (s.kind == intern("demo.post")) ++post_count;
                t.equal(posts->parts.size(), size_t(post_count), "outputs: shells per element -> one part per post");
                t.equal(rails->parts.size(), size_t(2), "outputs: as realized -> one part per rail product");
                t.check(rails->desc.purpose == output_purpose::collision, "outputs: purpose kept");
            }
        }


        void test_registry(runner& t) {

            registry reg;
            register_core_pack(reg, "core_owner");
            const u64 g0 = reg.generation();
            register_demo_pack(reg, "demo_owner");
            t.check(reg.generation() > g0, "registry: adding bumps the generation");
            const u64 g1 = reg.generation();
            reg.remove_owner("demo_owner");
            t.check(reg.generation() > g1, "registry: removing bumps the generation");
            auto snap = reg.snapshot();
            t.check(snap->find_pack("demo") == nullptr && snap->find_pack("core") != nullptr, "registry: remove_owner removes only that owner");

            // Conflicts: same production name in two unrelated packs is an error; with extends it is an override
            registry conflict;
            register_core_pack(conflict, "x");
            conflict.add_pack("x", { .name = "p1" });
            conflict.add_pack("x", { .name = "p2" });
            conflict.add_pack("x", { .name = "p3", .extends = { "p1" } });
            for (const char* pack : { "p1", "p2", "p3" })
                conflict.add("x", pack, production_desc{ .name = "same", .match = { .kinds = { intern("nothing") } } });
            auto data = std::make_shared<construct_data>();
            data->packs = { "p1", "p2" };
            t.check(has_diag(evaluate(*conflict.snapshot(), { .data = data }), "packs", "neither extends"), "registry: unrelated packs with the same name -> error");
            data->packs = { "p1", "p3" };
            t.check(!has_diag(evaluate(*conflict.snapshot(), { .data = data }), "packs", "neither extends"), "registry: an extending pack overrides silently");
            data->packs = { "missing" };
            t.check(has_diag(evaluate(*conflict.snapshot(), { .data = data }), "packs", "not loaded"), "registry: a missing pack is reported");
        }

    }


    namespace {

        void test_generators(runner& t) {

            // Profiles
            const profile_curve lin = profile_curve::linear(1.0f, 0.5f);
            t.check(std::abs(lin.evaluate(0.5f) - 0.75f) < 1e-4f, "stages: a linear profile interpolates");
            const profile_curve bump = profile_curve::of({ { 0.0f, 0.0f }, { 0.5f, 1.0f }, { 1.0f, 0.0f } });
            bool bounded = true;
            for (i32 i = 0; i <= 100; ++i)
                bounded = bounded && bump.evaluate(static_cast<f32>(i) / 100.0f) <= 1.0001f && bump.evaluate(static_cast<f32>(i) / 100.0f) >= -0.0001f;
            t.check(bounded, "stages: profiles never overshoot their points");

            {
                registry modules;
                register_core_pack(modules, "core");
                register_packs_in<^^test_module_root>(modules, "test_module");
                auto ms = modules.snapshot();
                const pack_desc* annotated = ms->find_pack("test.annotated");
                t.check(annotated && annotated->display == "Annotated" && annotated->version == semver{ 2, 3, 4 }
                        && annotated->depends_on == std::vector<std::string>{ "core" }, "modules: a [[=pack]] namespace registers as a pack");
                t.check(annotated && annotated->legacy_asset_types == std::vector<std::string>{ "annotated_legacy" },
                        "modules: customize_pack() of the namespace runs");
                t.check(ms->find_generator("test.annotated.one") != nullptr, "modules: the pack's stage types register");
                t.check(ms->find_generator("test.unpacked") == nullptr, "modules: types outside a [[=pack]] namespace do not");
                bool preset = false;
                for (const auto& p : ms->presets)
                    preset = preset || p.desc->name == "test.annotated.preset";
                t.check(preset, "modules: register_extras() of the namespace runs");
                modules.remove_owner("test_module");
                t.check(modules.snapshot()->find_pack("test.annotated") == nullptr, "modules: remove_owner(module id) removes it all");
            }

            registry reg;
            register_core_pack(reg, "core");
            construct::register_pack<^^test_stage_pack>(reg, "test", { .name = "test_stages" });
            auto snap = reg.snapshot();
            const auto* points = snap->find_generator("test.points");
            t.check(points != nullptr, "stages: a stage type registers by reflection");
            if (!points)
                return;
            const generator_desc& d = *points->desc;
            t.equal(d.settings.size(), size_t{ 5 }, "stages: one setting per member");
            if (const auto* kids = snap->find_generator("test.children"); kids && kids->desc->on_add) {
                generator_instance added{};
                kids->desc->on_add(added, construct_data{});
                t.check(added.label == "Added children", "stages: a type's on_add is registered by reflection");
            } else
                t.check(false, "stages: a type's on_add is registered by reflection");
            if (d.settings.size() == 5) {
                t.check(d.settings[0].kind == setting_kind::integer && d.settings[0].display == "Count" && d.settings[0].group == "Distribution",
                        "stages: setting annotations are read");
                t.check(d.settings[2].kind == setting_kind::choice && d.settings[2].choices == std::vector<std::string>{ "Line", "Ring" }
                        && d.settings[2].default_value == setting_value(i32(1)), "stages: enum members become choices");
                t.check(d.settings[3].kind == setting_kind::profile, "stages: profile members");
            }

            auto data = std::make_shared<construct_data>();
            data->packs = { "test_stages" };
            generator_instance a{ .id = 1, .type = "test.points", .label = "Points" };
            a.set("count", i32(4));
            a.set("arrangement", i32(0));
            generator_instance b{ .id = 2, .type = "test.children", .label = "Children", .inputs = { 1 } };
            data->generators = { a, b };

            const evaluation_result r1 = evaluate(*snap, { .data = data, .seed = 1, .editor_data = true });
            auto count_stage = [](const evaluation_result& r, u32 stage) {
                return static_cast<u32>(std::ranges::count_if(r.elements, [&](const element_summary& e) { return e.stage == stage; }));
            };
            t.equal(count_stage(r1, 1), 4u, "stages: a stage emits its elements (settings from the instance)");
            t.equal(count_stage(r1, 2), 8u, "stages: a stage grows on its input stage (defaults for unset settings)");
            t.check(r1.stages.size() == 2 && r1.stages[0].ran && r1.stages[0].elements == 4 && r1.stages[1].elements == 8,
                    "stages: per stage statistics");
            t.check(std::ranges::all_of(r1.elements, [](const element_summary& e) { return e.stage != 2 || e.source == "Children"; }),
                    "stages: elements carry the stage's label as source");

            const evaluation_result r2 = evaluate(*snap, { .data = data, .seed = 2, .editor_data = true });
            const evaluation_result r1b = evaluate(*snap, { .data = data, .seed = 1, .editor_data = true });
            auto first_point = [](const evaluation_result& r) {
                for (const element_summary& e : r.elements)
                    if (e.stage == 1 && !e.skeleton.empty() && e.path.to_string().ends_with("p[1]"))
                        return e.skeleton.front();
                return glm::vec3(-1.0f);
            };
            t.check(first_point(r1) != first_point(r2), "stages: the construct seed changes what a stage makes");
            t.check(first_point(r1) == first_point(r1b), "stages: the same seed makes the same result");
            auto reseeded = std::make_shared<construct_data>(*data);
            reseeded->generators[0].seed = 7;
            t.check(first_point(evaluate(*snap, { .data = reseeded, .seed = 1, .editor_data = true })) != first_point(r1),
                    "stages: a stage's own seed re-rolls it");

            auto disabled = std::make_shared<construct_data>(*data);
            disabled->generators[0].enabled = false;
            const evaluation_result r3 = evaluate(*snap, { .data = disabled, .seed = 1 });
            t.check(count_stage(r3, 1) == 0 && count_stage(r3, 2) == 0, "stages: a disabled stage (and what grows on it) is skipped");

            auto missing = std::make_shared<construct_data>(*data);
            missing->generators.push_back({ .id = 3, .type = "nobody.registered_this", .label = "Ghost" });
            const evaluation_result r4 = evaluate(*snap, { .data = missing, .seed = 1 });
            t.check(r4.stages.size() == 3 && !r4.stages[2].ran && std::ranges::any_of(r4.diagnostics, [](const diagnostic& x) {
                        return x.level == severity::warning && x.source == "Ghost"; }), "stages: an unknown stage type is reported, not fatal");

            const std::vector<std::byte> bytes = serialize(*data);
            const auto restored = deserialize(bytes);
            t.check(restored && restored->generators.size() == 2 && restored->generators[0].settings == data->generators[0].settings
                    && restored->generators[1].inputs == std::vector<u32>{ 1 }, "stages: serialized and restored");

            generator_instance pruned = a;
            pruned.set("spacing", 1.0f);
            prune_defaults(pruned, d);
            t.check(pruned.find("spacing") == nullptr && pruned.find("count") != nullptr, "stages: prune_defaults keeps only changed settings");
            t.equal(next_stage_id(data->generators), 3u, "stages: next stage id");

            // Stopping after a pipeline stage (editor: look at intermediate results without running the later stages)
            const evaluation_result early = evaluate(*snap, { .data = data, .seed = 1, .editor_data = true, .stop_after = "core.generate" });
            t.check(early.stopped_after == "core.generate" && early.outputs.empty() && count_stage(early, 1) == 4,
                    "pipeline: stop after Generate keeps the elements, builds no outputs");
            t.check(std::ranges::none_of(early.timings, [](const stage_timing& x) { return x.stage == "core.realize"; }),
                    "pipeline: the stages after the stop do not run");
            const evaluation_result full = evaluate(*snap, { .data = data, .seed = 1 });
            t.check(full.stopped_after.empty() && std::ranges::any_of(full.timings, [](const stage_timing& x) { return x.stage == "core.output"; }),
                    "pipeline: without a stop every stage runs");

            // Editor declarations of a pack
            registry editor_reg;
            register_core_pack(editor_reg, "core");
            editor_reg.add_pack("t", { .name = "decl" });
            editor_reg.add("t", "decl", role_desc{ .name = "decl.thing", .display = "Thing", .description = {}, .attributes = { attr_radius() },
                                                   .point_attributes = { attr_radius() } });
            editor_reg.add("t", "decl", preset_desc{ .name = "decl.preset", .display = "Preset", .description = {}, .make = [] { return construct_data{}; } });
            auto esnap = editor_reg.snapshot();
            const role_desc* role = esnap->find_role(intern("decl.thing"));
            t.check(role && role->display == "Thing" && esnap->presets.size() == 1, "editor declarations: roles and presets register");
            const attribute_desc* radius = esnap->find_attribute(attr_radius());
            t.check(radius && radius->display == "Radius", "editor declarations: attributes are found in the schemas");
            editor_reg.remove_owner("t");
            t.check(editor_reg.snapshot()->roles.empty() && editor_reg.snapshot()->presets.empty(), "editor declarations: removed with their owner");
        }

        void test_slice_and_serialize(runner& t) {

            using namespace geometry;
            const mesh box = make_primitive(primitive_kind::box, { .size = glm::vec3(2.0f, 1.0f, 4.0f) });
            const std::vector<contour> cs = slice(box, glm::vec4(0.0f, 0.0f, 1.0f, 0.5f));       // z = 0.5
            t.check(cs.size() == 1 && cs[0].closed && cs[0].points.size() == 4, "slice: a box gives one closed 4-point loop");
            if (cs.size() == 1) {
                const f32 area = signed_area(cs[0], glm::vec3(0, 0, 1));
                t.check(std::abs(std::abs(area) - 2.0f) < 1e-4f, "slice: loop area is the box section (2 x 1)");
                t.check(area > 0.0f, "slice: loop is counter-clockwise seen from +n");
                const contour fine = resample(cs[0], 0.1f);
                t.check(fine.points.size() >= 60 && fine.closed, "resample: ~0.1 m spacing along a 6 m loop");
            }
            t.check(slice(box, glm::vec4(0.0f, 0.0f, 1.0f, 3.0f)).empty(), "slice: a plane outside the mesh gives nothing");
            const mesh cyl = make_primitive(primitive_kind::cylinder, { .radius = 1.0f, .height = 2.0f, .segments = 24 });
            const auto cyl_cs = slice(cyl, glm::vec4(0.0f, 1.0f, 0.0f, 0.2f));
            t.check(cyl_cs.size() == 1 && cyl_cs[0].closed && cyl_cs[0].points.size() == 24, "slice: cylinder side gives a 24-gon");

            // Serialization round trip of the demo plus every guide / edit / value kind
            auto data = make_demo_construct();
            data->guides.push_back({ .name = "plane", .index = 3, .role = intern("x.leaves"), .shape = surface_guide{ { 2.0f, 1.0f }, { 3, 2 } } });
            data->guides.back().edits.push_back(edit_assign_group{ .faces = { .group = intern("top"), .normal = glm::vec3(0, 1, 0) }, .group = intern("g") });
            data->guides.back().attributes.set(intern("x.q"), glm::quat(0.5f, 0.5f, 0.5f, 0.5f), point_index{ 2 });
            data->guides.back().attributes.set(intern("x.s"), std::string("text"), curve_span{ 0.25, 0.75 });
            data->parameters.push_back({ "p", 3.5 });
            data->overrides.push_back({ .path = *element_path::parse("railing/post[2]"), .hidden = true, .attributes = {} });
            data->policies.push_back({ .policy = "pol", .scope = policy_scope{ .level = scope_level::kind, .kind = intern("k") } });
            data->compositions.push_back({ "core.seed", composition::chain });
            data->origin = glm::dvec3(1.0, 2.0, 3.0);
            const std::vector<std::byte> bytes = serialize(*data);
            auto back = deserialize(bytes);
            t.check(back.has_value(), "serialize: round trip parses" + (back ? std::string() : ": " + back.error()));
            if (back) {
                t.check(serialize(*back) == bytes, "serialize: round trip is byte-identical");
                t.equal(content_hash(*back), content_hash(*data), "serialize: content hash survives the round trip");
                auto snap = fresh_snapshot(true);
                auto restored = std::make_shared<construct_data>(std::move(*back));
                restored->guides.pop_back();             // the extra guide uses an unknown role: compare the demo only
                auto original = make_demo_construct();
                original->overrides = restored->overrides;
                original->parameters = restored->parameters;
                original->policies.clear();
                restored->policies.clear();
                restored->compositions.clear();
                original->origin = restored->origin;
                t.equal(evaluate(*snap, { .data = restored, .seed = 3 }).hash(), evaluate(*snap, { .data = original, .seed = 3 }).hash(),
                        "serialize: a loaded construct evaluates like the original");
            }
            std::vector<std::byte> broken = bytes;
            broken.resize(broken.size() / 2);
            t.check(!deserialize(broken).has_value(), "serialize: truncated data is rejected");
            broken = bytes;
            broken[4] = std::byte{ 99 };
            t.check(!deserialize(broken).has_value(), "serialize: a newer version is rejected");
        }

    }


    self_test_report run_self_test() {

        self_test_report report{};
        const auto start = std::chrono::steady_clock::now();
        runner t{ report };
        auto guarded = [&](std::string_view name, auto&& fn) {
            try {
                fn(t);
            } catch (const std::exception& e) {
                t.check(false, std::format("{}: exception: {}", name, e.what()));
            }
        };
        guarded("paths", test_paths);
        guarded("schema reflection", test_schema_reflection);
        guarded("mesh and kernel", test_mesh_and_kernel);
        guarded("two policies", test_two_policies);
        guarded("policies", test_policies);
        guarded("demo", test_demo);
        guarded("stages / outputs / recursion", test_stages_outputs_recursion);
        guarded("registry", test_registry);
        guarded("slice / serialize", test_slice_and_serialize);
        guarded("stages", test_generators);
        report.milliseconds = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count();
        return report;
    }

}
