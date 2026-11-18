#include "procedural/construct/demo_pack.h"
#include "procedural/construct/reflect.h"

#include <glm/gtc/matrix_transform.hpp>



namespace GLT::construct::demo_pack {

    // TYPES ===========================================================================================================

    struct [[=tag_schema{ text("demo") }]] demo_tags {

        [[=attr{ .display = text("Post spacing"), .min = 0.1f, .max = 10.0f, .affects_topology = true }]]
        f32 spacing = 1.0f;

        [[=attr{ .display = text("Railing height"), .min = 0.2f, .max = 3.0f }]]
        f32 height = 1.0f;
    };


    struct [[=kind_info{ text("demo.post"), shape_kind::frame, text("Railing post") }]] post {

        [[=param{ .display = text("Height"), .min = 0.1f, .max = 5.0f }]] f32 height = 1.0f;
        [[=param{ .display = text("Width"), .min = 0.01f, .max = 0.5f }]]  f32 width = 0.06f;
    };


    role_id railing_role() { static const role_id id = intern("demo.railing"); return id; }


    // A post every [spacing] metres along the railing curve, both ends included
    struct [[=production_info{ text("demo.posts") }]] posts final : production {

        static matcher match() { return { .kinds = { kind_guide_curve() }, .roles = { railing_role() } }; }
        static std::vector<kind_id> emits() { return { intern("demo.post") }; }

        void expand(expand_context& ctx, const element& rail) override {
            const auto& c = std::get<curve_shape>(rail.data).curve;
            const f32 spacing = std::max(0.1f, ctx.query().attribute_as<f32>(rail, attribute_of<^^demo_tags::spacing>(), 1.0f));
            const f32 height = ctx.query().attribute_as<f32>(rail, attribute_of<^^demo_tags::height>(), 1.0f);
            const f64 length = c.length();
            const i64 count = static_cast<i64>(std::floor(length / spacing)) + 1;
            for (i64 i = 0; i < count; ++i) {
                const f64 t = count > 1 ? static_cast<f64>(i) / static_cast<f64>(count - 1) : 0.0;
                const geometry::curve_sample s = c.sample(t);
                frame local{};
                local.origin = glm::dvec3(s.position);
                ctx.emit(rail, "post", i, intern("demo.post"))
                    .placement(rail.placement.compose(local))
                    .shape(frame_shape{})
                    .params(post{ .height = height, .width = 0.06f });
            }
        }
    };


    struct [[=realizer_info{ text("demo.post") }]] post_realizer final : realizer {

        static matcher match() { return { .kinds = { intern("demo.post") } }; }

        void realize(realize_context& ctx, const element& e) override {
            const post* p = e.params.get<post>();
            const post params = p ? *p : post{};
            geometry::mesh m = geometry::make_primitive(geometry::primitive_kind::box,
                                                        { .size = glm::vec3(params.width, params.height, params.width) });
            geometry::transform(m, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, params.height * 0.5f, 0.0f)));
            for (u32 f = 0; f < m.face_count(); ++f)
                m.set(attribute_domain::face, geometry::attr_material(), f, intern("metal"));
            ctx.mesh("post", std::move(m));
        }
    };


    // Top rail and middle rail: the guide curve lifted to the post heights, swept as tubes
    struct [[=realizer_info{ text("demo.railing") }]] railing_realizer final : realizer {

        static matcher match() { return { .kinds = { kind_guide_curve() }, .roles = { railing_role() } }; }

        void realize(realize_context& ctx, const element& e) override {
            const auto& c = std::get<curve_shape>(e.data).curve;
            const f32 height = ctx.query().attribute_as<f32>(e, attribute_of<^^demo_tags::height>(), 1.0f);
            for (const auto& [name, h] : { std::pair{ "top_rail", height }, std::pair{ "mid_rail", height * 0.5f } }) {
                std::vector<glm::vec3> lifted(c.points().begin(), c.points().end());
                for (glm::vec3& p : lifted)
                    p.y += h;
                geometry::mesh tube = geometry::sweep_circle(geometry::curve(c.type(), std::move(lifted), c.closed()),
                                                             { .sides = 10, .radius = 0.025f, .radius_attribute = {}, .caps = true, .segments_per_span = 12 });
                for (u32 f = 0; f < tube.face_count(); ++f)
                    tube.set(attribute_domain::face, geometry::attr_material(), f, intern("metal"));
                ctx.mesh(name, std::move(tube));
            }
        }
    };

}


namespace GLT::construct {

    // FUNCTION IMPLEMENTATION =========================================================================================

    void register_demo_pack(i_registry& registry, std::string_view owner) {

        register_pack<^^demo_pack>(registry, owner, {
            .name = std::string(DEMO_PACK),
            .version = { 0, 1, 0 },
            .extends = {},
            .depends_on = {},
            .display = "Demo",
            .description = "Example pack: railings with posts along a spline",
        });
    }


    std::shared_ptr<construct_data> make_demo_construct() {

        auto data = std::make_shared<construct_data>();
        data->packs = { std::string(DEMO_PACK) };

        // Deck: a box, cut twice along its length; the middle strip gets another material
        guide deck{};
        deck.name = "deck";
        deck.placement.origin = glm::dvec3(0.0, 0.05, 0.0);
        deck.shape = volume_guide{ geometry::primitive_kind::box, { .size = glm::vec3(4.0f, 0.1f, 1.2f) }, orientation::solid };
        deck.edits.push_back(edit_loop_cut{ .near_point = glm::vec3(0.0f, 0.05f, 0.6f), .positions = { 0.3f, 0.7f } });
        deck.edits.push_back(edit_set_attribute{ .faces = { .inside = std::pair{ glm::vec3(-0.5f, 0.04f, -0.1f), glm::vec3(0.5f, 0.06f, 0.1f) } },
                                                 .id = geometry::attr_material(), .value = intern("deck_stripe"), .domain = attribute_domain::face });
        data->guides.push_back(std::move(deck));

        // Railing along the far edge of the deck, curving up at the end
        guide rail{};
        rail.name = "railing";
        rail.role = intern("demo.railing");
        rail.placement.origin = glm::dvec3(0.0, 0.1, -0.55);
        rail.shape = curve_guide{ geometry::curve_type::catmull_rom,
                                  { { -1.9f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.6f, 0.0f, 0.1f }, { 1.9f, 0.0f, 0.5f } }, false, {} };
        rail.attributes.set(intern("demo.spacing"), 0.8f);
        data->guides.push_back(std::move(rail));

        // A buoy on one side, mirrored to the other
        guide buoy{};
        buoy.name = "buoy";
        buoy.placement.origin = glm::dvec3(0.0, 0.0, 0.0);
        buoy.shape = volume_guide{ geometry::primitive_kind::capsule, { .radius = 0.15f, .height = 0.3f, .segments = 16, .rings = 8 }, orientation::solid };
        buoy.edits.push_back(edit_transform{ glm::translate(glm::mat4(1.0f), glm::vec3(1.5f, 0.1f, 0.8f)) });
        buoy.modifiers.mirror_plane = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        data->guides.push_back(std::move(buoy));

        // A lamp position (marker for game code)
        guide lamp{};
        lamp.name = "lamp";
        lamp.shape = point_guide{ { glm::vec3(-1.9f, 1.3f, -0.55f) } };
        data->guides.push_back(std::move(lamp));

        return data;
    }

}
