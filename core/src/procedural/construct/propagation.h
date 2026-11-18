#pragma once

#include "procedural/construct/attributes.h"

// Procedural construction: lineage events and attribute propagation.
// Geometry operations never write attributes themselves: for every new element (vertex, face, corner, edge, derived
// element) they report a lineage event, and a propagator decides the value. Which propagator answers is a policy
// decision (hook "core.propagation"), so a spaceship pack and a plant pack get different results from the same cut.
// See documentation/procedural_construction_interface.md §11



namespace GLT::construct {

    // TYPES ===========================================================================================================

    enum class lineage_kind : u8 {

        copy,               // target is an unchanged copy of one source (no new information)
        split,              // loop cut, slice, boolean cut, spline split
        merge,              // weld, dissolve, union
        extrude,            // extrude, inset, sweep caps
        subdivide,          // subdivision, tessellation
        mirror,             // symmetry
        instance,           // arrays
        boolean,            // any CSG
        derive,             // a production emits a child element
        transfer,           // a realizer reads a guide region near its output
        remesh,
    };


    struct lineage_source {

        u32                                             index = 0;          // element index in the source domain
        f32                                             weight = 1.0f;      // barycentric / area / length weight
    };


    // @brief How one target element came to be
    struct lineage_event {

        lineage_kind                                    kind = lineage_kind::copy;
        attribute_domain                                domain = attribute_domain::none;
        std::span<const lineage_source>                 sources{};
        u32                                             target = 0;
        bool                                            is_seam = false;    // target is a new seam (cut edge, boolean seam)
        u32                                             depth_delta = 0;    // derive: production depth step
        std::optional<f64>                              curve_t{};          // split / derive along a curve
    };


    // @brief Values of the sources, and the place to write the target's
    class attribute_io {
    public:

        attribute_io(std::span<const attribute_value> sources, std::span<const lineage_source> weights) noexcept
            : m_sources(sources), m_weights(weights) {}

        [[nodiscard]] u32 source_count() const noexcept { return static_cast<u32>(m_sources.size()); }
        [[nodiscard]] const attribute_value& source(u32 i) const noexcept { return m_sources[i]; }
        [[nodiscard]] f32 weight(u32 i) const noexcept { return i < m_weights.size() ? m_weights[i].weight : 1.0f; }
        // Index of the source with the largest weight (the first one on ties)
        [[nodiscard]] u32 dominant() const noexcept;

        void write(attribute_value value) { m_result = std::move(value); m_written = true; m_leave_default = false; }
        void leave_default() noexcept { m_written = false; m_leave_default = true; }

        [[nodiscard]] bool written() const noexcept { return m_written; }
        [[nodiscard]] bool left_default() const noexcept { return m_leave_default; }
        [[nodiscard]] const attribute_value& result() const noexcept { return m_result; }

    private:

        std::span<const attribute_value>                m_sources;
        std::span<const lineage_source>                 m_weights;
        attribute_value                                 m_result{};
        bool                                            m_written = false;
        bool                                            m_leave_default = false;
    };


    class i_propagation;

    // @brief The rest of a propagation chain (chain composition: a policy may handle an event or defer)
    class next_propagation {
    public:

        next_propagation() = default;
        // [fallback]: what answers when the chain is exhausted (the attribute's mode). Null: built-in mode [fallback_mode]
        next_propagation(std::span<const i_propagation* const> rest, const i_propagation* fallback, name_id fallback_mode) noexcept
            : m_rest(rest), m_fallback(fallback), m_fallback_mode(fallback_mode) {}

        // Runs the next policy, or the attribute's mode when the chain is exhausted
        void operator()(const attribute_desc& desc, const lineage_event& event, attribute_io& io) const;

    private:

        std::span<const i_propagation* const>           m_rest{};
        const i_propagation*                            m_fallback = nullptr;
        name_id                                         m_fallback_mode{};
    };


    // @brief A propagation policy (hook "core.propagation") or a named mode ("copy", "interpolate", ...)
    class i_propagation {
    public:

        virtual ~i_propagation() = default;
        virtual void propagate(const attribute_desc& desc, const lineage_event& event, attribute_io& io,
                               const next_propagation& next) const = 0;
    };


    // @brief What geometry operations call. Resolves descriptors and runs the right chain for the element being built
    class propagator {
    public:

        virtual ~propagator() = default;

        // Descriptor of [id]; unknown attributes get a synthesized one (copy mode, value default of the column)
        [[nodiscard]] virtual const attribute_desc& describe(attribute_id id, const attribute_value& column_default) const = 0;
        virtual void apply(const attribute_desc& desc, const lineage_event& event, attribute_io& io) const = 0;
    };


    // @brief Propagator without policies: every attribute uses its schema mode (or [default_mode])
    class mode_propagator final : public propagator {
    public:

        explicit mode_propagator(name_id default_mode = intern("copy"), std::span<const tag_schema_desc> schemas = {});

        [[nodiscard]] const attribute_desc& describe(attribute_id id, const attribute_value& column_default) const override;
        void apply(const attribute_desc& desc, const lineage_event& event, attribute_io& io) const override;

    private:

        name_id                                         m_default_mode;
        std::unordered_map<attribute_id, attribute_desc> m_descs{};
        mutable std::mutex                              m_mutex{};
        mutable std::unordered_map<attribute_id, std::unique_ptr<attribute_desc>> m_synth{};
    };

    // FUNCTION DECLARATION ============================================================================================

    // The built-in modes: copy, interpolate, reduce_min, reduce_max, reduce_sum, reduce_avg, decay, boundary, drop.
    // Returns false for an unknown mode name
    bool apply_builtin_mode(name_id mode, const attribute_desc& desc, const lineage_event& event, attribute_io& io);

    // Names of the built-in modes (for registration and the editor)
    [[nodiscard]] std::span<const std::string_view> builtin_mode_names() noexcept;

    // Value arithmetic used by the modes (numeric types and vectors; other types: the dominant source)
    [[nodiscard]] attribute_value weighted_sum(const attribute_io& io, bool normalize);

}
