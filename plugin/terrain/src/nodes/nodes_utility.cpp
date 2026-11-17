
#include "util/pch.h"
#include "node_helpers.h"

#include "expression/expression.h"



namespace GLT::terrain::nodes {

    namespace {

        struct exposed_parameter_node final : node {
            std::string name = "parameter";
            f32 value = 0.0f;
            void evaluate(context& ctx) override {
                const f32 v = ctx.parameter(name, value);
                auto out = ctx.output(0).values();
                std::fill(out.begin(), out.end(), v);
            }
            static node_type describe() {
                return node_builder<exposed_parameter_node>("terrain.parameter", "Exposed Parameter", "Utility")
                    .describe("A named value the world can override (terrain component) without editing the graph")
                    .output("out", pin_type::scalar)
                    .text("Name", &exposed_parameter_node::name)
                    .number("Default", &exposed_parameter_node::value)
                    .build();
            }
        };


        struct world_position_node final : node {
            i32 axis = 0;               // x, z, distance from origin
            f32 scale = 1.0f;
            void evaluate(context& ctx) override {
                ctx.for_each(ctx.output(0), [&](i64, i64, glm::dvec2 p, f32& out) {
                    const f64 v = axis == 0 ? p.x : (axis == 1 ? p.y : glm::length(p));
                    out = static_cast<f32>(v * scale);
                });
            }
            static node_type describe() {
                return node_builder<world_position_node>("terrain.world_position", "World Position", "Utility")
                    .describe("The world coordinate of each sample as a field")
                    .output("out", pin_type::heightfield)
                    .choice("Axis", &world_position_node::axis, { "X", "Z", "Distance from origin" })
                    .number("Scale", &world_position_node::scale)
                    .build();
            }
        };


        struct expression_node final : node {
            std::string formula = "a";
            void evaluate(context& ctx) override {
                field& out = ctx.output(0);
                auto compiled = expression::compile(formula);
                if (!compiled) {
                    auto values = out.values();
                    std::fill(values.begin(), values.end(), 0.0f);      // the editor shows the error (T2)
                    return;
                }
                const field* inputs[expression::INPUT_COUNT] = { &ctx.input(0), &ctx.input(1), &ctx.input(2), &ctx.input(3) };
                ctx.for_each(out, [&](i64 gx, i64 gy, glm::dvec2 p, f32& value) {
                    f32 in[expression::INPUT_COUNT];
                    for (u32 i = 0; i < expression::INPUT_COUNT; ++i)
                        in[i] = inputs[i]->at(gx, gy);
                    value = compiled->evaluate(in, static_cast<f32>(p.x), static_cast<f32>(p.y));
                });
            }
            static node_type describe() {
                return node_builder<expression_node>("terrain.expression", "Expression", "Utility")
                    .describe("A formula per sample: a * smoothstep(0.3, 0.7, b) + c. Inputs a..d, position x and z")
                    .input("a", pin_type::heightfield).input("b", pin_type::heightfield)
                    .input("c", pin_type::heightfield).input("d", pin_type::heightfield)
                    .output("out", pin_type::heightfield)
                    .text("Formula", &expression_node::formula,
                        "+ - * / ^, comparisons, && || !, cond ? a : b, sin cos sqrt abs min max clamp lerp smoothstep step pow ...")
                    .build();
            }
        };


        // Pass-through nodes: Cache (a hint for the evaluator to keep the result, T2) and Inspect (a named debug output
        // for the map's view list, T2). Both are plain copies for evaluation
        struct pass_through_node final : node {
            std::string name;
            void evaluate(context& ctx) override {
                const field& in = ctx.input(0);
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) { out = in.at(gx, gy); });
            }
            static node_type describe(const char* type_name, const char* display, const char* text, bool named) {
                auto builder = node_builder<pass_through_node>(type_name, display, "Utility");
                builder.describe(text).input("in", pin_type::heightfield).output("out", pin_type::heightfield);
                if (named)
                    builder.text("Name", &pass_through_node::name);
                return builder.build();
            }
        };


        // Outputs: sinks. The evaluator returns what flows into their inputs (request pin = input name)
        struct surface_node final : node {
            void evaluate(context&) override {}
            static node_type describe() {
                return node_builder<surface_node>("terrain.surface", "Surface", "Output")
                    .describe("The terrain's height: becomes chunk meshes (live) or baked meshes")
                    .input("height", pin_type::heightfield)
                    .sink()
                    .build();
            }
        };


        struct material_layers_node final : node {
            void evaluate(context&) override {}
            static node_type describe() {
                return node_builder<material_layers_node>("terrain.material_layers", "Material Layers", "Output")
                    .describe("Up to four masks: the strongest one picks the material per triangle (v1)")
                    .input("layer 1", pin_type::mask).input("layer 2", pin_type::mask)
                    .input("layer 3", pin_type::mask).input("layer 4", pin_type::mask)
                    .sink()
                    .build();
            }
        };

    }

    void register_utility(i_node_registry& registry, std::string_view owner) {

        registry.add(owner, exposed_parameter_node::describe());
        registry.add(owner, world_position_node::describe());
        registry.add(owner, expression_node::describe());
        registry.add(owner, pass_through_node::describe("terrain.cache", "Cache", "Keeps its result for reuse (evaluation hint)", false));
        registry.add(owner, pass_through_node::describe("terrain.inspect", "Inspect", "Named debug output, listed in the map's views", true));
        registry.add(owner, surface_node::describe());
        registry.add(owner, material_layers_node::describe());
    }


    void register_builtin(i_node_registry& registry, std::string_view owner) {

        register_generators(registry, owner);
        register_combine(registry, owner);
        register_modify(registry, owner);
        register_masks(registry, owner);
        register_utility(registry, owner);
    }

}
