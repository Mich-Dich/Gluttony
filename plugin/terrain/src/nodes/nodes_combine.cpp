
#include "util/pch.h"
#include "node_helpers.h"



namespace GLT::terrain::nodes {

    namespace {

        enum class binary_op : u8 { add, subtract, multiply, divide, min, max, average, difference, screen, overlay };

        template<binary_op Op>
        struct binary_node final : node {
            void evaluate(context& ctx) override {
                const field& a = ctx.input(0);
                const field& b = ctx.input(1);
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) {
                    const f32 x = a.at(gx, gy), y = b.at(gx, gy);
                    if constexpr (Op == binary_op::add)             out = x + y;
                    else if constexpr (Op == binary_op::subtract)   out = x - y;
                    else if constexpr (Op == binary_op::multiply)   out = x * y;
                    else if constexpr (Op == binary_op::divide)     out = y != 0.0f ? x / y : 0.0f;
                    else if constexpr (Op == binary_op::min)        out = std::min(x, y);
                    else if constexpr (Op == binary_op::max)        out = std::max(x, y);
                    else if constexpr (Op == binary_op::average)    out = (x + y) * 0.5f;
                    else if constexpr (Op == binary_op::difference) out = std::abs(x - y);
                    else if constexpr (Op == binary_op::screen)     out = 1.0f - (1.0f - x) * (1.0f - y);
                    else                                            out = x < 0.5f ? 2.0f * x * y : 1.0f - 2.0f * (1.0f - x) * (1.0f - y);
                });
            }
            static node_type describe(const char* type_name, const char* display, const char* text, f32 default_b) {
                return node_builder<binary_node>(type_name, display, "Combine")
                    .describe(text)
                    .input("a", pin_type::heightfield)
                    .input("b", pin_type::heightfield, default_b)
                    .output("out", pin_type::heightfield)
                    .build();
            }
        };


        struct blend_node final : node {
            void evaluate(context& ctx) override {
                const field& a = ctx.input(0);
                const field& b = ctx.input(1);
                const field& mask = ctx.input(2);
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) {
                    out = glm::mix(a.at(gx, gy), b.at(gx, gy), std::clamp(mask.at(gx, gy), 0.0f, 1.0f));
                });
            }
            static node_type describe() {
                return node_builder<blend_node>("terrain.blend", "Blend", "Combine")
                    .describe("a where the mask is 0, b where it is 1, mixed in between")
                    .input("a", pin_type::heightfield)
                    .input("b", pin_type::heightfield)
                    .input("mask", pin_type::mask, 0.5f)
                    .output("out", pin_type::heightfield)
                    .build();
            }
        };


        struct select_node final : node {
            f32 threshold = 0.5f;
            f32 softness = 0.0f;
            void evaluate(context& ctx) override {
                const field& a = ctx.input(0);
                const field& b = ctx.input(1);
                const field& selector = ctx.input(2);
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) {
                    const f32 t = smoothstep(threshold - softness, threshold + softness, selector.at(gx, gy));
                    out = glm::mix(a.at(gx, gy), b.at(gx, gy), t);
                });
            }
            static node_type describe() {
                return node_builder<select_node>("terrain.select", "Select", "Combine")
                    .describe("Switches between a (selector below the threshold) and b (above)")
                    .input("a", pin_type::heightfield)
                    .input("b", pin_type::heightfield)
                    .input("selector", pin_type::heightfield)
                    .output("out", pin_type::heightfield)
                    .number("Threshold", &select_node::threshold)
                    .number("Softness", &select_node::softness, 0.0f, 0.0f, "Width of the transition around the threshold")
                    .build();
            }
        };


        // Up to four layers over a base, each with a blend mode, strength and optional mask (like an image editor's layers)
        struct layer_stack_node final : node {
            static constexpr u32 LAYERS = 4;
            i32 mode_1 = 0, mode_2 = 0, mode_3 = 0, mode_4 = 0;         // add, multiply, max, min, replace
            f32 strength_1 = 1.0f, strength_2 = 1.0f, strength_3 = 1.0f, strength_4 = 1.0f;
            void evaluate(context& ctx) override {
                const field& base = ctx.input(0);
                const i32 modes[LAYERS] = { mode_1, mode_2, mode_3, mode_4 };
                const f32 strengths[LAYERS] = { strength_1, strength_2, strength_3, strength_4 };
                ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2, f32& out) {
                    f32 v = base.at(gx, gy);
                    for (u32 l = 0; l < LAYERS; ++l) {
                        const f32 layer = ctx.input(1 + l * 2).at(gx, gy);
                        const f32 w = std::clamp(ctx.input(2 + l * 2).at(gx, gy), 0.0f, 1.0f) * strengths[l];
                        f32 blended = v;
                        switch (modes[l]) {
                            case 1:  blended = v * layer; break;
                            case 2:  blended = std::max(v, layer); break;
                            case 3:  blended = std::min(v, layer); break;
                            case 4:  blended = layer; break;
                            default: blended = v + layer; break;
                        }
                        v = glm::mix(v, blended, w);
                    }
                    out = v;
                });
            }
            static node_type describe() {
                const std::vector<std::string> modes = { "Add", "Multiply", "Max", "Min", "Replace" };
                return node_builder<layer_stack_node>("terrain.layer_stack", "Layer Stack", "Combine")
                    .describe("A base with up to four layers on top, each blended by mode, strength and mask (unconnected layers add 0)")
                    .input("base", pin_type::heightfield)
                    .input("layer 1", pin_type::heightfield).input("mask 1", pin_type::mask, 1.0f)
                    .input("layer 2", pin_type::heightfield).input("mask 2", pin_type::mask, 1.0f)
                    .input("layer 3", pin_type::heightfield).input("mask 3", pin_type::mask, 1.0f)
                    .input("layer 4", pin_type::heightfield).input("mask 4", pin_type::mask, 1.0f)
                    .output("out", pin_type::heightfield)
                    .choice("Mode 1", &layer_stack_node::mode_1, modes).number("Strength 1", &layer_stack_node::strength_1, 0.0f, 1.0f)
                    .choice("Mode 2", &layer_stack_node::mode_2, modes).number("Strength 2", &layer_stack_node::strength_2, 0.0f, 1.0f)
                    .choice("Mode 3", &layer_stack_node::mode_3, modes).number("Strength 3", &layer_stack_node::strength_3, 0.0f, 1.0f)
                    .choice("Mode 4", &layer_stack_node::mode_4, modes).number("Strength 4", &layer_stack_node::strength_4, 0.0f, 1.0f)
                    .build();
            }
        };

    }

    void register_combine(i_node_registry& registry, std::string_view owner) {

        registry.add(owner, binary_node<binary_op::add>::describe("terrain.add", "Add", "a + b", 0.0f));
        registry.add(owner, binary_node<binary_op::subtract>::describe("terrain.subtract", "Subtract", "a - b", 0.0f));
        registry.add(owner, binary_node<binary_op::multiply>::describe("terrain.multiply", "Multiply", "a * b", 1.0f));
        registry.add(owner, binary_node<binary_op::divide>::describe("terrain.divide", "Divide", "a / b (0 where b is 0)", 1.0f));
        registry.add(owner, binary_node<binary_op::min>::describe("terrain.min", "Min", "The lower of a and b", 0.0f));
        registry.add(owner, binary_node<binary_op::max>::describe("terrain.max", "Max", "The higher of a and b", 0.0f));
        registry.add(owner, binary_node<binary_op::average>::describe("terrain.average", "Average", "(a + b) / 2", 0.0f));
        registry.add(owner, binary_node<binary_op::difference>::describe("terrain.difference", "Difference", "|a - b|", 0.0f));
        registry.add(owner, binary_node<binary_op::screen>::describe("terrain.screen", "Screen", "1 - (1 - a)(1 - b), for 0..1 inputs", 0.0f));
        registry.add(owner, binary_node<binary_op::overlay>::describe("terrain.overlay", "Overlay", "Image-editor overlay, for 0..1 inputs", 0.5f));
        registry.add(owner, blend_node::describe());
        registry.add(owner, select_node::describe());
        registry.add(owner, layer_stack_node::describe());
    }

}
