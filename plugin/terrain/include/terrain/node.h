
#pragma once

#include <variant>

#include "terrain/types.h"

// Terrain node SDK. A node is a small C++ class: pins and parameters are declared once in a node_builder (parameters
// bound to member variables, so the default value is the member's initializer and code and UI cannot diverge), the work
// happens in evaluate(). Built-in nodes and node packs (T5) use exactly this API.
//
//     struct ridged_mountains final : terrain::node {
//         f32 frequency = 0.004f;
//         f32 sharpness = 2.0f;
//
//         void evaluate(terrain::context& ctx) override {
//             const terrain::field& base = ctx.input(0);
//             ctx.for_each(ctx.output(0), [&](i64 gx, i64 gy, glm::dvec2 p, f32& out) {
//                 const f64 r = 1.0 - std::abs(terrain::noise::perlin(p * f64(frequency), ctx.seed()));
//                 out = base.at(gx, gy) + f32(std::pow(r, sharpness) * 120.0);
//             });
//         }
//
//         static terrain::node_type describe() {
//             return terrain::node_builder<ridged_mountains>("my_pack.ridged_mountains", "Ridged Mountains", "Noise")
//                 .input("base", terrain::pin_type::heightfield)
//                 .output("result", terrain::pin_type::heightfield)
//                 .number("Frequency", &ridged_mountains::frequency, 0.0001f, 0.1f)
//                 .number("Sharpness", &ridged_mountains::sharpness, 0.1f, 8.0f)
//                 .build();
//         }
//     };
//
// Rules: evaluate() is a pure function of (parameters, inputs, seed, sample positions). It runs on a worker thread, must
// write every sample of its outputs, and may read inputs only within apron() samples around an output sample.

namespace GLT::terrain {

    // TYPES ===========================================================================================================

    // What flows along a link. All three are fields over the evaluated region (a scalar is a constant field); the types
    // are for the editor's colours and connection checks. Points (scatter) follow in T7
    enum class pin_type : u8 { heightfield, mask, scalar };


    struct pin_desc {

        std::string                                     name;
        pin_type                                        type = pin_type::heightfield;
        f32                                             default_value = 0.0f;   // inputs: the constant used when unconnected
    };


    enum class param_kind : u8 { number, integer, toggle, vector2, choice, text };

    using param_value = std::variant<f32, i32, bool, glm::vec2, std::string>;


    struct param_desc {

        std::string                                     name;
        std::string                                     tooltip;
        param_kind                                      kind = param_kind::number;
        param_value                                     default_value{};
        f32                                             min = 0.0f;
        f32                                             max = 0.0f;             // min == max: unbounded
        std::vector<std::string>                        choices{};              // param_kind::choice
    };


    class node;

    // @brief Everything the plugin knows about a node class: identity, pins, parameters, how to make one
    struct node_type {

        std::string                                     type_name;              // stable id stored in graphs: "terrain.perlin"
        std::string                                     display_name;
        std::string                                     category;               // palette group: "Noise", "Combine", ...
        std::string                                     description;
        std::vector<pin_desc>                           inputs{};
        std::vector<pin_desc>                           outputs{};
        std::vector<param_desc>                         params{};
        bool                                            is_sink = false;        // output node (surface, material layers)

        std::function<std::unique_ptr<node>()>          create{};
        std::vector<std::function<void(node&, const param_value&)>> apply{};    // parallel to [params]

        [[nodiscard]] i32 input_index(std::string_view pin) const noexcept {
            for (size_t i = 0; i < inputs.size(); ++i)
                if (inputs[i].name == pin)
                    return static_cast<i32>(i);
            return -1;
        }

        [[nodiscard]] i32 output_index(std::string_view pin) const noexcept {
            for (size_t i = 0; i < outputs.size(); ++i)
                if (outputs[i].name == pin)
                    return static_cast<i32>(i);
            return -1;
        }

        [[nodiscard]] i32 param_index(std::string_view param) const noexcept {
            for (size_t i = 0; i < params.size(); ++i)
                if (params[i].name == param)
                    return static_cast<i32>(i);
            return -1;
        }
    };


    // @brief What a node sees while it evaluates
    //
    // area() is the region the node must fill (already grown by what its consumers read around it). Every input covers at
    // least area() grown by the node's apron(); an unconnected input is a constant field of the pin's default value
    class context {
    public:

        context(const region& area, u64 seed, std::span<const field* const> inputs, std::span<field> outputs,
            const std::unordered_map<std::string, f32>* parameters, const std::atomic<bool>* cancel)
            : m_area(area), m_seed(seed), m_inputs(inputs), m_outputs(outputs), m_parameters(parameters), m_cancel(cancel) {}

        [[nodiscard]] const region& area() const noexcept { return m_area; }

        // Stable per (graph seed, node): two nodes of the same type in one graph get different noise
        [[nodiscard]] u64 seed() const noexcept { return m_seed; }

        [[nodiscard]] const field& input(u32 index) const noexcept { return *m_inputs[index]; }
        [[nodiscard]] u32 input_count() const noexcept { return static_cast<u32>(m_inputs.size()); }
        [[nodiscard]] field& output(u32 index) noexcept { return m_outputs[index]; }

        // Exposed graph parameter (world overrides first, then the parameter node's default)
        [[nodiscard]] f32 parameter(std::string_view name, f32 fallback) const {
            if (m_parameters)
                if (auto it = m_parameters->find(std::string(name)); it != m_parameters->end())
                    return it->second;
            return fallback;
        }

        // The request was abandoned (the user moved on): long nodes may return early, their result is discarded
        [[nodiscard]] bool cancelled() const noexcept { return m_cancel && m_cancel->load(std::memory_order_relaxed); }

        // Calls f(gx, gy, world_position, value&) for every sample of [out] (row by row)
        template<typename F>
        void for_each(field& out, F&& f) const {
            const region& r = out.area();
            for (i64 gy = r.y0; gy < r.y1(); ++gy)
                for (i64 gx = r.x0; gx < r.x1(); ++gx)
                    f(gx, gy, r.position(gx, gy), out.at(gx, gy));
        }

    private:

        region                                          m_area;
        u64                                             m_seed;
        std::span<const field* const>                   m_inputs;
        std::span<field>                                m_outputs;
        const std::unordered_map<std::string, f32>*     m_parameters;
        const std::atomic<bool>*                        m_cancel;
    };


    // @brief Base class of every terrain node
    class node {
    public:

        virtual ~node() = default;

        // Fill every output over ctx.area()
        virtual void evaluate(context& ctx) = 0;

        // How many samples around an output sample this node reads from its inputs, at the given spacing (a blur of
        // 10 world units reads 10 samples at spacing 1, 2 at spacing 5). Inputs are evaluated that much larger
        [[nodiscard]] virtual u32 apron(f64 /*spacing*/) const { return 0; }
    };

    // FUNCTION DECLARATION ============================================================================================

    namespace detail {

        template<typename T>
        [[nodiscard]] T convert_param(const param_value& value) {

            if constexpr (std::is_same_v<T, f32>) {
                if (auto* v = std::get_if<f32>(&value)) return *v;
                if (auto* v = std::get_if<i32>(&value)) return static_cast<f32>(*v);
                if (auto* v = std::get_if<bool>(&value)) return *v ? 1.0f : 0.0f;
            } else if constexpr (std::is_same_v<T, i32>) {
                if (auto* v = std::get_if<i32>(&value)) return *v;
                if (auto* v = std::get_if<f32>(&value)) return static_cast<i32>(std::lround(*v));
                if (auto* v = std::get_if<bool>(&value)) return *v ? 1 : 0;
            } else if constexpr (std::is_same_v<T, bool>) {
                if (auto* v = std::get_if<bool>(&value)) return *v;
                if (auto* v = std::get_if<i32>(&value)) return *v != 0;
                if (auto* v = std::get_if<f32>(&value)) return *v != 0.0f;
            } else if constexpr (std::is_same_v<T, glm::vec2>) {
                if (auto* v = std::get_if<glm::vec2>(&value)) return *v;
                if (auto* v = std::get_if<f32>(&value)) return glm::vec2(*v);
            } else if constexpr (std::is_same_v<T, std::string>) {
                if (auto* v = std::get_if<std::string>(&value)) return *v;
            }
            return T{};
        }

    }

    // CLASS DECLARATION ===============================================================================================

    // @brief Declares a node class: pins, parameters (bound to members), identity
    template<typename N>
    requires std::derived_from<N, node> && std::default_initializable<N>
    class node_builder {
    public:

        node_builder(std::string type_name, std::string display_name, std::string category) {
            m_type.type_name = std::move(type_name);
            m_type.display_name = std::move(display_name);
            m_type.category = std::move(category);
            m_type.create = []() -> std::unique_ptr<node> { return std::make_unique<N>(); };
        }

        node_builder& describe(std::string text) { m_type.description = std::move(text); return *this; }

        node_builder& input(std::string name, pin_type type, f32 default_value = 0.0f) {
            m_type.inputs.push_back({ std::move(name), type, default_value });
            return *this;
        }

        node_builder& output(std::string name, pin_type type) {
            m_type.outputs.push_back({ std::move(name), type, 0.0f });
            return *this;
        }

        node_builder& sink() { m_type.is_sink = true; return *this; }

        node_builder& number(std::string name, f32 N::* member, f32 min = 0.0f, f32 max = 0.0f, std::string tooltip = {}) {
            return add(std::move(name), std::move(tooltip), param_kind::number, member, min, max);
        }

        node_builder& integer(std::string name, i32 N::* member, i32 min = 0, i32 max = 0, std::string tooltip = {}) {
            return add(std::move(name), std::move(tooltip), param_kind::integer, member, static_cast<f32>(min), static_cast<f32>(max));
        }

        node_builder& toggle(std::string name, bool N::* member, std::string tooltip = {}) {
            return add(std::move(name), std::move(tooltip), param_kind::toggle, member, 0.0f, 0.0f);
        }

        node_builder& vector2(std::string name, glm::vec2 N::* member, f32 min = 0.0f, f32 max = 0.0f, std::string tooltip = {}) {
            return add(std::move(name), std::move(tooltip), param_kind::vector2, member, min, max);
        }

        node_builder& choice(std::string name, i32 N::* member, std::vector<std::string> options, std::string tooltip = {}) {
            add(std::move(name), std::move(tooltip), param_kind::choice, member, 0.0f, static_cast<f32>(options.size()) - 1.0f);
            m_type.params.back().choices = std::move(options);
            return *this;
        }

        node_builder& text(std::string name, std::string N::* member, std::string tooltip = {}) {
            return add(std::move(name), std::move(tooltip), param_kind::text, member, 0.0f, 0.0f);
        }

        [[nodiscard]] node_type build() { return std::move(m_type); }

    private:

        template<typename T>
        node_builder& add(std::string name, std::string tooltip, param_kind kind, T N::* member, f32 min, f32 max) {

            const N prototype{};                    // the member initializer is the default
            param_desc desc{};
            desc.name = std::move(name);
            desc.tooltip = std::move(tooltip);
            desc.kind = kind;
            desc.default_value = param_value(prototype.*member);
            desc.min = min;
            desc.max = max;
            m_type.params.push_back(std::move(desc));
            m_type.apply.push_back([member](node& target, const param_value& value) {
                static_cast<N&>(target).*member = detail::convert_param<T>(value);
            });
            return *this;
        }

        node_type                                       m_type{};
    };


    // @brief The node types known to the plugin (built-in and node packs). Thread-safe
    class i_node_registry {
    public:

        virtual ~i_node_registry() = default;

        // Adds or replaces a type. [owner]: the registering plugin (GLT_MODULE_NAME), for remove_owner() on unload
        virtual void add(std::string_view owner, node_type type) = 0;

        virtual void remove_owner(std::string_view owner) = 0;

        // Shared so an evaluation in flight keeps a type alive while its pack is reloaded
        [[nodiscard]] virtual std::shared_ptr<const node_type> find(std::string_view type_name) const = 0;

        [[nodiscard]] virtual std::vector<std::shared_ptr<const node_type>> all() const = 0;

        // Bumped by every add / remove: cached results computed with older node code are not reused
        [[nodiscard]] virtual u64 generation() const noexcept = 0;
    };


    // @brief The terrain plugin, as seen by node packs: plugin_manager::get_plugin_base("terrain")->as<i_terrain>()
    class i_terrain {
    public:

        virtual ~i_terrain() = default;

        [[nodiscard]] virtual i_node_registry& nodes() = 0;
    };

}
