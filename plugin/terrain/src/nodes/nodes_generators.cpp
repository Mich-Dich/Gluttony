
#include "util/pch.h"
#include "node_helpers.h"



namespace GLT::terrain::nodes {

    namespace {

        using noise::basis;

        // constant / gradients ----------------------------------------------------------------------------------------

        struct constant_node final : node {
            f32 value = 0.0f;
            void evaluate(context& ctx) override {
                auto out = ctx.output(0).values();
                std::fill(out.begin(), out.end(), value);
            }
            static node_type describe() {
                return node_builder<constant_node>("terrain.constant", "Constant", "Generators")
                    .describe("The same value everywhere")
                    .output("out", pin_type::scalar)
                    .number("Value", &constant_node::value)
                    .build();
            }
        };


        struct linear_gradient_node final : node {
            f32 angle = 0.0f;       // degrees, 0 = +x
            f32 slope = 0.01f;      // height per world unit
            f32 offset = 0.0f;
            void evaluate(context& ctx) override {
                const f64 a = glm::radians(static_cast<f64>(angle));
                const glm::dvec2 dir(std::cos(a), std::sin(a));
                ctx.for_each(ctx.output(0), [&](i64, i64, glm::dvec2 p, f32& out) {
                    out = static_cast<f32>(glm::dot(p, dir) * slope + offset);
                });
            }
            static node_type describe() {
                return node_builder<linear_gradient_node>("terrain.linear_gradient", "Linear Gradient", "Generators")
                    .describe("A tilted plane: height rises along a direction")
                    .output("out", pin_type::heightfield)
                    .number("Angle", &linear_gradient_node::angle, -180.0f, 180.0f, "Direction of the rise in degrees (0 = +X)")
                    .number("Slope", &linear_gradient_node::slope, -1.0f, 1.0f, "Height gained per world unit")
                    .number("Offset", &linear_gradient_node::offset)
                    .build();
            }
        };


        struct radial_gradient_node final : node {
            glm::vec2 center{ 0.0f };
            f32 radius = 1000.0f;
            f32 inner = 1.0f;
            f32 outer = 0.0f;
            void evaluate(context& ctx) override {
                const glm::dvec2 c(center);
                const f64 r = std::max(static_cast<f64>(radius), 1e-6);
                ctx.for_each(ctx.output(0), [&](i64, i64, glm::dvec2 p, f32& out) {
                    const f32 t = static_cast<f32>(std::clamp(glm::distance(p, c) / r, 0.0, 1.0));
                    out = glm::mix(inner, outer, t);
                });
            }
            static node_type describe() {
                return node_builder<radial_gradient_node>("terrain.radial_gradient", "Radial Gradient", "Generators")
                    .describe("Inner value at the center, outer value at the radius and beyond")
                    .output("out", pin_type::heightfield)
                    .vector2("Center", &radial_gradient_node::center)
                    .number("Radius", &radial_gradient_node::radius, 1.0f, 100000.0f)
                    .number("Inner", &radial_gradient_node::inner)
                    .number("Outer", &radial_gradient_node::outer)
                    .build();
            }
        };

        // single noise --------------------------------------------------------------------------------------------------

        template<basis B>
        struct noise_node final : node {
            f32 frequency = 0.002f;
            f32 amplitude = 100.0f;
            glm::vec2 offset{ 0.0f };
            i32 seed = 0;
            void evaluate(context& ctx) override {
                const u64 s = node_seed(ctx, seed);
                const glm::dvec2 o(offset);
                const f64 freq = frequency;
                ctx.for_each(ctx.output(0), [&](i64, i64, glm::dvec2 p, f32& out) {
                    out = static_cast<f32>(noise::evaluate(B, p * freq + o, s) * amplitude);
                });
            }
            static node_type describe(const char* type_name, const char* display, const char* text) {
                return node_builder<noise_node>(type_name, display, "Generators")
                    .describe(text)
                    .output("out", pin_type::heightfield)
                    .number("Frequency", &noise_node::frequency, 0.00001f, 1.0f, "Features per world unit (1 / feature size)")
                    .number("Amplitude", &noise_node::amplitude, 0.0f, 10000.0f, "Height of the features (output is about [-amplitude, amplitude])")
                    .vector2("Offset", &noise_node::offset, 0.0f, 0.0f, "Shifts the pattern (in noise space)")
                    .integer("Seed", &noise_node::seed, 0, 0, "Added to the graph seed: different pattern, same settings")
                    .build();
            }
        };

        // fractal noise -------------------------------------------------------------------------------------------------

        enum class fractal : u8 { fbm, ridged, billow, swiss };

        template<fractal F>
        struct fractal_node final : node {
            i32 basis_kind = 0;
            f32 frequency = 0.001f;
            f32 amplitude = 200.0f;
            i32 octaves = 6;
            f32 lacunarity = 2.0f;
            f32 gain = 0.5f;
            f32 warp = 0.15f;
            i32 seed = 0;
            void evaluate(context& ctx) override {
                const u64 s = node_seed(ctx, seed);
                const auto b = static_cast<basis>(std::clamp(basis_kind, 0, 2));
                const i32 oct = std::clamp(octaves, 1, 16);
                const f64 freq = frequency;
                ctx.for_each(ctx.output(0), [&](i64, i64, glm::dvec2 p, f32& out) {
                    const glm::dvec2 q = p * freq;
                    f64 n = 0.0;
                    if constexpr (F == fractal::fbm)         n = noise::fbm(b, q, s, oct, lacunarity, gain);
                    else if constexpr (F == fractal::ridged) n = noise::ridged(b, q, s, oct, lacunarity, gain);
                    else if constexpr (F == fractal::billow) n = noise::billow(b, q, s, oct, lacunarity, gain);
                    else                                     n = noise::swiss(b, q, s, oct, lacunarity, gain, warp);
                    out = static_cast<f32>(n * amplitude);
                });
            }
            static node_type describe(const char* type_name, const char* display, const char* text) {
                auto builder = node_builder<fractal_node>(type_name, display, "Generators");
                builder.describe(text)
                    .output("out", pin_type::heightfield)
                    .choice("Basis", &fractal_node::basis_kind, { "Perlin", "Simplex", "Value" })
                    .number("Frequency", &fractal_node::frequency, 0.00001f, 1.0f, "Base frequency (1 / largest feature size)")
                    .number("Amplitude", &fractal_node::amplitude, 0.0f, 10000.0f)
                    .integer("Octaves", &fractal_node::octaves, 1, 16, "Detail layers")
                    .number("Lacunarity", &fractal_node::lacunarity, 1.0f, 4.0f, "Frequency multiplier per octave")
                    .number("Gain", &fractal_node::gain, 0.0f, 1.0f, "Amplitude multiplier per octave (roughness)");
                if constexpr (F == fractal::swiss)
                    builder.number("Warp", &fractal_node::warp, 0.0f, 1.0f, "How strongly earlier octaves bend later ones");
                builder.integer("Seed", &fractal_node::seed);
                return builder.build();
            }
        };

        // voronoi -------------------------------------------------------------------------------------------------------

        struct voronoi_node final : node {
            f32 frequency = 0.002f;
            f32 amplitude = 100.0f;
            f32 jitter = 1.0f;
            i32 metric = 0;
            i32 mode = 0;           // F1, F2, F2 - F1, cell value
            i32 seed = 0;
            void evaluate(context& ctx) override {
                const u64 s = node_seed(ctx, seed);
                const f64 freq = frequency;
                const f64 j = std::clamp(static_cast<f64>(jitter), 0.0, 1.0);
                const auto m = static_cast<noise::distance_metric>(std::clamp(metric, 0, 2));
                ctx.for_each(ctx.output(0), [&](i64, i64, glm::dvec2 p, f32& out) {
                    const noise::worley_result w = noise::worley(p * freq, s, j, m);
                    f64 v = 0.0;
                    switch (mode) {
                        case 1:  v = w.f2; break;
                        case 2:  v = w.f2 - w.f1; break;
                        case 3:  v = w.cell_value; break;
                        default: v = w.f1; break;
                    }
                    out = static_cast<f32>(v * amplitude);
                });
            }
            static node_type describe() {
                return node_builder<voronoi_node>("terrain.voronoi", "Voronoi", "Generators")
                    .describe("Worley cellular noise: distances to random feature points, or a value per cell")
                    .output("out", pin_type::heightfield)
                    .number("Frequency", &voronoi_node::frequency, 0.00001f, 1.0f, "Cells per world unit")
                    .number("Amplitude", &voronoi_node::amplitude, 0.0f, 10000.0f)
                    .number("Jitter", &voronoi_node::jitter, 0.0f, 1.0f, "0 = regular grid, 1 = fully random points")
                    .choice("Metric", &voronoi_node::metric, { "Euclidean", "Manhattan", "Chebyshev" })
                    .choice("Mode", &voronoi_node::mode, { "F1", "F2", "F2 - F1", "Cell value" })
                    .integer("Seed", &voronoi_node::seed)
                    .build();
            }
        };

        // domain warp ---------------------------------------------------------------------------------------------------

        struct domain_warp_node final : node {
            f32 strength = 50.0f;       // world units
            f32 frequency = 0.003f;
            i32 octaves = 3;
            i32 seed = 0;
            [[nodiscard]] u32 apron(f64 spacing) const override { return samples_for(std::abs(strength), spacing) + 1; }
            void evaluate(context& ctx) override {
                const field& in = ctx.input(0);
                const u64 sx = node_seed(ctx, seed), sy = hash_mix(sx);
                const f64 freq = frequency;
                const i32 oct = std::clamp(octaves, 1, 8);
                const f64 limit = std::abs(static_cast<f64>(strength));
                ctx.for_each(ctx.output(0), [&](i64, i64, glm::dvec2 p, f32& out) {
                    const glm::dvec2 q = p * freq;
                    glm::dvec2 d(noise::fbm(basis::simplex, q, sx, oct, 2.0, 0.5), noise::fbm(basis::simplex, q, sy, oct, 2.0, 0.5));
                    d = glm::clamp(d * static_cast<f64>(strength), glm::dvec2(-limit), glm::dvec2(limit));     // stay inside the apron
                    out = in.sample(p + d);
                });
            }
            static node_type describe() {
                return node_builder<domain_warp_node>("terrain.domain_warp", "Domain Warp", "Generators")
                    .describe("Displaces the input sideways by two noise fields: organic, swirled shapes")
                    .input("in", pin_type::heightfield)
                    .output("out", pin_type::heightfield)
                    .number("Strength", &domain_warp_node::strength, 0.0f, 2000.0f, "Largest displacement in world units")
                    .number("Frequency", &domain_warp_node::frequency, 0.00001f, 1.0f)
                    .integer("Octaves", &domain_warp_node::octaves, 1, 8)
                    .integer("Seed", &domain_warp_node::seed)
                    .build();
            }
        };

        // shapes --------------------------------------------------------------------------------------------------------

        struct mountain_node final : node {
            glm::vec2 center{ 0.0f };
            f32 radius = 500.0f;
            f32 height = 300.0f;
            f32 exponent = 1.5f;
            void evaluate(context& ctx) override {
                const glm::dvec2 c(center);
                const f64 r = std::max(static_cast<f64>(radius), 1e-6);
                ctx.for_each(ctx.output(0), [&](i64, i64, glm::dvec2 p, f32& out) {
                    const f64 t = std::max(0.0, 1.0 - glm::distance(p, c) / r);
                    out = static_cast<f32>(std::pow(t, static_cast<f64>(exponent)) * height);
                });
            }
            static node_type describe() {
                return node_builder<mountain_node>("terrain.mountain", "Mountain", "Shapes")
                    .describe("A cone at a world position; the exponent shapes the flanks (< 1 domed, > 1 sharp)")
                    .output("out", pin_type::heightfield)
                    .vector2("Center", &mountain_node::center)
                    .number("Radius", &mountain_node::radius, 1.0f, 100000.0f)
                    .number("Height", &mountain_node::height, -10000.0f, 10000.0f)
                    .number("Exponent", &mountain_node::exponent, 0.1f, 8.0f)
                    .build();
            }
        };


        struct island_falloff_node final : node {
            glm::vec2 center{ 0.0f };
            f32 radius = 4000.0f;
            f32 falloff = 1500.0f;
            void evaluate(context& ctx) override {
                const glm::dvec2 c(center);
                ctx.for_each(ctx.output(0), [&](i64, i64, glm::dvec2 p, f32& out) {
                    const f32 d = static_cast<f32>(glm::distance(p, c));
                    out = 1.0f - smoothstep(radius - falloff, radius, d);
                });
            }
            static node_type describe() {
                return node_builder<island_falloff_node>("terrain.island_falloff", "Island Falloff", "Shapes")
                    .describe("Mask: 1 inside the island, fading to 0 at the radius. Multiply a heightfield with it")
                    .output("mask", pin_type::mask)
                    .vector2("Center", &island_falloff_node::center)
                    .number("Radius", &island_falloff_node::radius, 1.0f, 1000000.0f)
                    .number("Falloff", &island_falloff_node::falloff, 0.0f, 1000000.0f, "Width of the coast transition")
                    .build();
            }
        };


        struct crater_node final : node {
            glm::vec2 center{ 0.0f };
            f32 radius = 300.0f;
            f32 depth = 80.0f;
            f32 rim_height = 30.0f;
            f32 rim_width = 120.0f;
            void evaluate(context& ctx) override {
                const glm::dvec2 c(center);
                const f32 r = std::max(radius, 1e-3f);
                const f32 w = std::max(rim_width, 1e-3f);
                ctx.for_each(ctx.output(0), [&](i64, i64, glm::dvec2 p, f32& out) {
                    const f32 d = static_cast<f32>(glm::distance(p, c));
                    const f32 bowl = d < r ? -depth * (1.0f - (d / r) * (d / r)) : 0.0f;
                    const f32 x = (d - r) / w;                                  // rim centred on the radius
                    const f32 rim = rim_height * std::exp(-x * x * 4.0f);
                    out = bowl + rim;
                });
            }
            static node_type describe() {
                return node_builder<crater_node>("terrain.crater", "Crater", "Shapes")
                    .describe("A bowl with a raised rim")
                    .output("out", pin_type::heightfield)
                    .vector2("Center", &crater_node::center)
                    .number("Radius", &crater_node::radius, 1.0f, 100000.0f)
                    .number("Depth", &crater_node::depth, 0.0f, 10000.0f)
                    .number("Rim Height", &crater_node::rim_height, 0.0f, 10000.0f)
                    .number("Rim Width", &crater_node::rim_width, 1.0f, 100000.0f)
                    .build();
            }
        };


        struct ridge_line_node final : node {
            glm::vec2 start{ -1000.0f, 0.0f };
            glm::vec2 end{ 1000.0f, 0.0f };
            f32 width = 300.0f;
            f32 height = 200.0f;
            void evaluate(context& ctx) override {
                const glm::dvec2 a(start), b(end);
                const glm::dvec2 ab = b - a;
                const f64 len2 = std::max(glm::dot(ab, ab), 1e-9);
                const f64 w = std::max(static_cast<f64>(width), 1e-6);
                ctx.for_each(ctx.output(0), [&](i64, i64, glm::dvec2 p, f32& out) {
                    const f64 t = std::clamp(glm::dot(p - a, ab) / len2, 0.0, 1.0);
                    const f64 d = glm::distance(p, a + ab * t);
                    const f64 k = std::max(0.0, 1.0 - d / w);
                    out = static_cast<f32>(k * k * (3.0 - 2.0 * k) * height);
                });
            }
            static node_type describe() {
                return node_builder<ridge_line_node>("terrain.ridge_line", "Ridge Line", "Shapes")
                    .describe("A ridge (or valley with negative height) along a segment")
                    .output("out", pin_type::heightfield)
                    .vector2("Start", &ridge_line_node::start)
                    .vector2("End", &ridge_line_node::end)
                    .number("Width", &ridge_line_node::width, 1.0f, 100000.0f, "Half width of the ridge")
                    .number("Height", &ridge_line_node::height, -10000.0f, 10000.0f)
                    .build();
            }
        };


        struct plateau_node final : node {
            glm::vec2 center{ 0.0f };
            f32 radius = 400.0f;
            f32 height = 100.0f;
            f32 edge = 100.0f;
            void evaluate(context& ctx) override {
                const glm::dvec2 c(center);
                ctx.for_each(ctx.output(0), [&](i64, i64, glm::dvec2 p, f32& out) {
                    const f32 d = static_cast<f32>(glm::distance(p, c));
                    out = height * (1.0f - smoothstep(radius, radius + edge, d));
                });
            }
            static node_type describe() {
                return node_builder<plateau_node>("terrain.plateau", "Plateau", "Shapes")
                    .describe("A flat-topped disc")
                    .output("out", pin_type::heightfield)
                    .vector2("Center", &plateau_node::center)
                    .number("Radius", &plateau_node::radius, 1.0f, 100000.0f)
                    .number("Height", &plateau_node::height, -10000.0f, 10000.0f)
                    .number("Edge", &plateau_node::edge, 0.0f, 100000.0f, "Width of the slope around the top")
                    .build();
            }
        };

    }

    void register_generators(i_node_registry& registry, std::string_view owner) {

        registry.add(owner, constant_node::describe());
        registry.add(owner, linear_gradient_node::describe());
        registry.add(owner, radial_gradient_node::describe());

        registry.add(owner, noise_node<basis::perlin>::describe("terrain.perlin", "Perlin Noise", "Smooth gradient noise"));
        registry.add(owner, noise_node<basis::simplex>::describe("terrain.simplex", "Simplex Noise", "Gradient noise with fewer directional artefacts"));
        registry.add(owner, noise_node<basis::value>::describe("terrain.value_noise", "Value Noise", "Interpolated random values (blocky)"));

        registry.add(owner, fractal_node<fractal::fbm>::describe("terrain.fbm", "fBm", "Fractal Brownian motion: octaves of noise, the classic terrain base"));
        registry.add(owner, fractal_node<fractal::ridged>::describe("terrain.ridged", "Ridged Multifractal", "Sharp mountain ridges, detail concentrated on the crests (0..amplitude)"));
        registry.add(owner, fractal_node<fractal::billow>::describe("terrain.billow", "Billow", "Rounded, puffy hills (0..amplitude)"));
        registry.add(owner, fractal_node<fractal::swiss>::describe("terrain.swiss_turbulence", "Swiss Turbulence", "Ridges bent by their own slope: eroded-looking mountains"));

        registry.add(owner, voronoi_node::describe());
        registry.add(owner, domain_warp_node::describe());

        registry.add(owner, mountain_node::describe());
        registry.add(owner, island_falloff_node::describe());
        registry.add(owner, crater_node::describe());
        registry.add(owner, ridge_line_node::describe());
        registry.add(owner, plateau_node::describe());
    }

}
