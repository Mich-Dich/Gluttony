
#include "util/pch.h"
#include "self_test.h"

#include "graph/evaluator.h"
#include "graph/graph.h"
#include "expression/expression.h"



namespace GLT::terrain {

    namespace {

        [[nodiscard]] bool same_bits(f32 a, f32 b) noexcept { return std::bit_cast<u32>(a) == std::bit_cast<u32>(b); }


        // Graph: [feeder per input] -> node under test. Feeders are fBm with different seeds and amplitudes so every input
        // carries a distinct, varying signal (masks get values around 0..1)
        [[nodiscard]] terrain_graph build_test_graph(const i_node_registry& registry, const node_type& type, u32& test_node) {

            terrain_graph graph{};
            test_node = graph.add_node(type.type_name, &type);

            const std::shared_ptr<const node_type> fbm = registry.find("terrain.fbm");
            for (size_t i = 0; i < type.inputs.size(); ++i) {
                if (!fbm)
                    break;
                const u32 feeder = graph.add_node(fbm->type_name, fbm.get());
                graph_node* n = graph.find(feeder);
                n->params["Seed"] = static_cast<i32>(17 + i * 31);
                n->params["Frequency"] = 0.02f;
                n->params["Amplitude"] = type.inputs[i].type == pin_type::mask ? 1.0f : 40.0f + static_cast<f32>(i) * 10.0f;
                n->params["Octaves"] = 3;
                graph.connect(feeder, "out", test_node, type.inputs[i].name);
            }
            return graph;
        }

    }


    self_test_report run_self_test(const i_node_registry& registry) {

        const auto start = std::chrono::steady_clock::now();
        self_test_report report{};
        auto fail = [&](std::string message) {
            ++report.failed;
            report.failures.push_back(std::move(message));
        };

        // No cache: a chunk must be computed on its own, not cut out of the whole region's cached result
        evaluator eval(registry, 0);

        // Whole region (deliberately not aligned to anything) and its four quadrants
        region whole{};
        whole.x0 = -37;
        whole.y0 = 11;
        whole.width = 48;
        whole.height = 40;
        whole.spacing = 2.0;

        std::array<region, 4> quads{};
        for (u32 q = 0; q < 4; ++q) {
            quads[q] = whole;
            quads[q].width = whole.width / 2;
            quads[q].height = whole.height / 2;
            quads[q].x0 = whole.x0 + ((q & 1u) ? static_cast<i64>(whole.width / 2) : 0);
            quads[q].y0 = whole.y0 + ((q & 2u) ? static_cast<i64>(whole.height / 2) : 0);
        }

        // ---- seams, every node type ----------------------------------------------------------------------------------
        for (const auto& type : registry.all()) {

            if (type->is_sink || type->outputs.empty())
                continue;

            u32 test_node = 0;
            auto graph = std::make_shared<const terrain_graph>(build_test_graph(registry, *type, test_node));

            for (const auto& pin : type->outputs) {

                eval_request request{};
                request.graph = graph;
                request.node = test_node;
                request.pin = pin.name;
                request.seed = 0xC0FFEE;
                request.area = whole;

                const eval_outcome full = eval.evaluate(request);
                if (!full) {
                    fail(std::format("{}.{}: whole region failed: {}", type->type_name, pin.name, full.error()));
                    continue;
                }

                bool ok = true;
                for (const region& quad : quads) {
                    request.area = quad;
                    const eval_outcome part = eval.evaluate(request);
                    if (!part) {
                        fail(std::format("{}.{}: chunk failed: {}", type->type_name, pin.name, part.error()));
                        ok = false;
                        break;
                    }
                    for (i64 gy = quad.y0; gy < quad.y1() && ok; ++gy)
                        for (i64 gx = quad.x0; gx < quad.x1() && ok; ++gx) {
                            const f32 a = full->data->at(gx, gy);
                            const f32 b = part->data->at(gx, gy);
                            if (!same_bits(a, b)) {
                                fail(std::format("{}.{}: seam mismatch at ({}, {}): whole {} vs chunk {}", type->type_name, pin.name, gx, gy, a, b));
                                ok = false;
                            }
                        }
                    if (!ok)
                        break;
                }
                if (ok)
                    ++report.passed;
            }
        }

        // ---- graph round trip ----------------------------------------------------------------------------------------
        {
            terrain_graph graph{};
            const u32 a = graph.add_node("terrain.fbm", registry.find("terrain.fbm").get());
            const u32 b = graph.add_node("terrain.expression", registry.find("terrain.expression").get());
            graph.find(b)->params["Formula"] = std::string("a * 2 + sin(x)");
            graph.find(b)->params["Flag"] = true;
            graph.find(b)->params["Vec"] = glm::vec2(1.5f, -2.0f);
            graph.connect(a, "out", b, "a");
            graph.exposed.push_back({ "sea_level", 12.0f, 0.0f, 100.0f });

            const auto bytes = serialize_graph(graph);
            const auto back = deserialize_graph(bytes);
            if (!back)
                fail(std::format("graph round trip: {}", back.error()));
            else if (back->nodes.size() != 2 || back->links.size() != 1 || back->exposed.size() != 1
                || back->find(b) == nullptr || back->find(b)->params != graph.find(b)->params || back->links[0] != graph.links[0]
                || back->next_id != graph.next_id)
                fail("graph round trip: data differs after reload");
            else
                ++report.passed;

            // Truncated data must be rejected, not crash
            if (deserialize_graph(std::span(bytes).first(bytes.size() / 2)))
                fail("graph round trip: truncated data was accepted");
            else
                ++report.passed;
        }

        // ---- expressions ---------------------------------------------------------------------------------------------
        {
            struct expression_case { const char* source; f32 expected; };
            const expression_case cases[] = {
                { "1 + 2 * 3", 7.0f },
                { "(1 + 2) * 3", 9.0f },
                { "2 ^ 3 ^ 2", 512.0f },
                { "-2 ^ 2", -4.0f },
                { "a * 2 + b", 2.0f * 3.0f + 5.0f },
                { "out = max(a, b) - min(c, d)", 5.0f - 1.0f },
                { "a > b ? 10 : 20", 20.0f },
                { "clamp(a, 0, 1)", 1.0f },
                { "smoothstep(0, 10, b)", 0.5f },
                { "lerp(c, d, 0.25)", 1.0f + (2.0f - 1.0f) * 0.25f },
                { "x + z", 7.0f + 11.0f },
                { "!(a == 3) || 0", 0.0f },
            };
            const f32 inputs[expression::INPUT_COUNT] = { 3.0f, 5.0f, 1.0f, 2.0f };
            for (const auto& c : cases) {
                const auto compiled = expression::compile(c.source);
                if (!compiled) {
                    fail(std::format("expression '{}' did not compile: {}", c.source, compiled.error()));
                    continue;
                }
                const f32 v = compiled->evaluate(inputs, 7.0f, 11.0f);
                if (std::abs(v - c.expected) > 1e-4f * std::max(1.0f, std::abs(c.expected)))
                    fail(std::format("expression '{}' = {}, expected {}", c.source, v, c.expected));
                else
                    ++report.passed;
            }
            for (const char* bad : { "1 +", "foo(1)", "sin(1, 2)", "(1", "a b", "", "q" }) {
                if (expression::compile(bad))
                    fail(std::format("expression '{}' should not compile", bad));
                else
                    ++report.passed;
            }
        }

        report.milliseconds = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count();
        return report;
    }

}
