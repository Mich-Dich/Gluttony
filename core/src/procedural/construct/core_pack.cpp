#include "procedural/construct/core_pack.h"
#include "procedural/construct/reflect.h"



namespace GLT::construct::core_pack {

    // TYPES ===========================================================================================================

    struct [[=tag_schema{ text("core") }]] core_tags {

        [[=attr{ .display = text("Radius"), .tooltip = text("Tube radius of curves (per control point)"), .domains = attribute_domain::element | attribute_domain::curve,
                 .propagation = text("interpolate"), .min = 0.0f, .max = 10.0f }]]
        f32 radius = 0.05f;

        [[=attr{ .tooltip = text("Not realized"), .propagation = text("copy") }]]
        bool hidden = false;

        [[=attr{ .tooltip = text("Named face group"), .domains = attribute_domain::face, .propagation = text("copy"), .user_editable = false }]]
        name_id group{};

        [[=attr{ .tooltip = text("Surface parameterization / texture coordinates"), .domains = attribute_domain::corner,
                 .propagation = text("interpolate"), .user_editable = false }]]
        glm::vec2 uv{ 0.0f };

        [[=attr{ .tooltip = text("Hard edge: normals are split across it"), .domains = attribute_domain::edge, .propagation = text("copy") }]]
        bool hard = false;

        [[=attr{ .tooltip = text("Material slot"), .domains = attribute_domain::element | attribute_domain::face, .propagation = text("copy") }]]
        name_id material{};
    };

    // Stages -----------------------------------------------------------------------------------------------------------

    struct [[=stage_info{ text("core.interpret") }]] interpret_stage final : stage {
        void run(stage_context& ctx) override { ctx.run_interpret(); }
    };

    struct [[=stage_info{ text("core.generate") }]] generate_stage final : stage {
        static std::vector<std::string> after() { return { "core.interpret" }; }
        void run(stage_context& ctx) override { ctx.run_generators(); }
    };

    struct [[=stage_info{ text("core.expand") }]] expand_stage final : stage {
        static std::vector<std::string> after() { return { "core.generate" }; }
        void run(stage_context& ctx) override { ctx.run_all_passes(); }
    };

    struct [[=stage_info{ text("core.realize") }]] realize_stage final : stage {
        static std::vector<std::string> after() { return { "core.expand" }; }
        void run(stage_context& ctx) override { ctx.run_realizers(); }
    };

    struct [[=stage_info{ text("core.finish") }]] finish_stage final : stage {
        static std::vector<std::string> after() { return { "core.realize" }; }
        void run(stage_context& ctx) override { ctx.run_finishers(); }
    };

    struct [[=stage_info{ text("core.output") }]] output_stage final : stage {
        static std::vector<std::string> after() { return { "core.finish" }; }
        void run(stage_context& ctx) override { ctx.build_outputs(); }
    };

    // Guide realizers --------------------------------------------------------------------------------------------------

    // Guides without a role are plain geometry; a role means some pack interprets them
    bool plain_guide(const element& e, const context&) { return e.role.empty(); }


    struct [[=realizer_info{ text("core.guide_volume"), -1000 }]] guide_volume final : realizer {
        static matcher match() { return { .kinds = { kind_guide_volume() }, .predicate = plain_guide }; }
        void realize(realize_context& ctx, const element& e) override {
            const auto& v = std::get<volume_shape>(e.data);
            ctx.mesh("cage", v.cage);
        }
    };

    struct [[=realizer_info{ text("core.guide_surface"), -1000 }]] guide_surface final : realizer {
        static matcher match() { return { .kinds = { kind_guide_surface() }, .predicate = plain_guide }; }
        void realize(realize_context& ctx, const element& e) override {
            ctx.mesh("surface", std::get<surface_shape>(e.data).surface);
        }
    };

    struct [[=realizer_info{ text("core.guide_curve"), -1000 }]] guide_curve final : realizer {
        static matcher match() { return { .kinds = { kind_guide_curve() }, .predicate = plain_guide }; }
        void realize(realize_context& ctx, const element& e) override {
            const auto& c = std::get<curve_shape>(e.data);
            geometry::sweep_settings s{};
            s.radius = ctx.query().attribute_as<f32>(e, attr_radius(), 0.05f);
            if (c.curve.find_point_column(attr_radius()))
                s.radius_attribute = attr_radius();
            ctx.mesh("tube", geometry::sweep_circle(c.curve, s));
        }
    };

    struct [[=realizer_info{ text("core.guide_point"), -1000 }]] guide_point final : realizer {
        static matcher match() { return { .kinds = { kind_guide_point() }, .predicate = plain_guide }; }
        void realize(realize_context& ctx, const element& e) override {
            const auto& p = std::get<point_shape>(e.data);
            for (const glm::vec3& at : p.points) {
                frame f{};
                f.origin = glm::dvec3(at);
                ctx.marker("point", f);
            }
        }
    };

    // Policies -----------------------------------------------------------------------------------------------------------

    struct [[=policy_info{ text("core.default_outputs") }]] default_outputs final : i_outputs {
        using hook = outputs_hook;
        void outputs(const output_query&, std::vector<output_desc>& out) const override {
            out.push_back({ .name = "render", .purpose = output_purpose::render, .include = {}, .topo = topology::merged_shells, .lods = true });
        }
    };

}


namespace GLT::construct {

    // FUNCTION IMPLEMENTATION =========================================================================================

    namespace {

        // A built-in mode as a registered mode (so packs can find, wrap or replace it by name)
        class builtin_mode final : public i_propagation {
        public:
            explicit builtin_mode(name_id mode) : m_mode(mode) {}
            void propagate(const attribute_desc& desc, const lineage_event& event, attribute_io& io, const next_propagation&) const override {
                apply_builtin_mode(m_mode, desc, event, io);
            }
        private:
            name_id                                     m_mode;
        };

    }


    void register_core_pack(i_registry& registry, std::string_view owner) {

        register_pack<^^core_pack>(registry, owner, {
            .name = std::string(CORE_PACK),
            .version = { 0, 1, 0 },
            .extends = {},
            .depends_on = {},
            .display = "Core",
            .description = "Guides, stages, passes, propagation modes and default outputs every construct uses",
        });

        registry.add(owner, CORE_PACK, pass_desc{ .name = "primary" });
        registry.add(owner, CORE_PACK, pass_desc{ .name = "secondary", .after = { "primary" } });
        registry.add(owner, CORE_PACK, pass_desc{ .name = "detail", .after = { "secondary" }, .lod_droppable = true });

        for (const std::string_view mode : builtin_mode_names())
            registry.add(owner, CORE_PACK, propagation_mode_desc{ std::string(mode), std::make_shared<const builtin_mode>(intern(mode)) });
    }

}
