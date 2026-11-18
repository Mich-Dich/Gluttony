#pragma once

#include "procedural/construct/output.h"

// Procedural construction: what pack code implements (productions, realizers, finishers, stages) and the contexts it receives.
// See documentation/procedural_construction_interface.md §5, §6, §8, §9, §12



namespace GLT::construct {

    // FORWARD DECLARATIONS ============================================================================================

    class evaluation;
    class context;

    // TYPES ===========================================================================================================

    // @brief Which elements a production / realizer applies to
    struct matcher {

        std::vector<kind_id>                            kinds{};            // empty: any
        std::vector<shape_kind>                         shapes{};
        std::vector<role_id>                            roles{};            // empty: any (an element without role too)
        std::vector<std::pair<attribute_id, attribute_value>> require{};    // attribute equals value
        std::vector<attribute_id>                       require_present{};
        std::vector<attribute_id>                       exclude_present{};
        std::optional<origin_kind>                      origin{};
        std::optional<u32>                              max_depth{};
        std::function<bool(const element&, const context&)> predicate{};    // optional, must be pure

        [[nodiscard]] bool matches(const element& e, const context& ctx) const;
    };


    struct lod_request {

        u32                                             level = 0;
        std::vector<pass_id>                            dropped_passes{};   // productions of these passes do not run
    };


    // @brief Read-only, thread-safe queries over an evaluation (§12)
    class context {
    public:

        explicit context(const evaluation& e) noexcept : m_eval(&e) {}

        // Elements and relations
        [[nodiscard]] const element& get(element_handle h) const;
        [[nodiscard]] std::optional<element_handle> find(const element_path& p) const;
        [[nodiscard]] std::vector<element_handle> elements(const matcher& m) const;
        [[nodiscard]] std::vector<element_handle> children(element_handle h) const;
        [[nodiscard]] std::vector<const relation*> relations(element_handle h, relation_id kind = {}) const;
        [[nodiscard]] u32 element_count() const noexcept;

        // Space: signed distance to volume elements (analytic for primitive guides, bounds of the cage otherwise)
        [[nodiscard]] f64 distance(const glm::dvec3& p, const matcher& filter = {}) const;
        [[nodiscard]] bool inside(const glm::dvec3& p, const matcher& filter = {}) const { return distance(p, filter) < 0.0; }

        // Curves
        [[nodiscard]] geometry::curve_sample sample_curve(element_handle h, f64 t) const;      // in the element's frame

        // Attributes: exact region, whole element, then the schema default
        [[nodiscard]] attribute_value attribute(const element& e, attribute_id id, const region_selector& where = whole_element{}) const;
        template<typename T>
        [[nodiscard]] T attribute_as(const element& e, attribute_id id, T fallback, const region_selector& where = whole_element{}) const {
            const attribute_value v = attribute(e, id, where);
            if (const T* t = std::get_if<T>(&v))
                return *t;
            return fallback;
        }

        // Policies
        template<hook H>
        [[nodiscard]] policy_view<H> policy(const element& e, const region_selector& where = whole_element{}) const;
        // Raw: matching policy instances (most specific first) and the composition in effect
        void policies(std::string_view hook_name, const element* e, const region_selector& where,
                      std::vector<const void*>& out, std::vector<policy_scope>* scopes, composition& mode) const;

        // Construct parameters (exposed parameters, request overrides applied) and seeds
        [[nodiscard]] attribute_value parameter(std::string_view name, attribute_value fallback = {}) const;
        [[nodiscard]] u64 construct_seed() const noexcept;
        [[nodiscard]] u64 seed_of(const element& e, u64 salt = 0) const;
        [[nodiscard]] rng random(const element& e, u64 salt = 0) const { return rng(seed_of(e, salt)); }

        [[nodiscard]] const lod_request& lod() const noexcept;
        [[nodiscard]] bool cancelled() const noexcept;

        // Propagator with the policies in effect for [e]
        [[nodiscard]] const propagator& propagation_for(const element& e) const;

    protected:

        const evaluation*                               m_eval;
    };


    // Productions ------------------------------------------------------------------------------------------------------

    class expand_context;

    // @brief Fluent setter for an element being emitted. Changes apply to the staged element directly
    class element_builder {
    public:

        element_builder(expand_context& ctx, u32 slot) noexcept : m_ctx(&ctx), m_slot(slot) {}

        element_builder& placement(const frame& f);
        element_builder& shape(shape_data data);
        element_builder& role(role_id r);
        element_builder& set(attribute_id id, attribute_value value, const region_selector& where = whole_element{});
        template<typename P> element_builder& params(P value) { params_block(param_block::make(std::move(value))); return *this; }
        element_builder& params_block(param_block block);

        // Usable in relate() during the same expand call; becomes a real handle when the stage commits
        [[nodiscard]] element_handle handle() const noexcept;
        [[nodiscard]] element& staged() const;

    private:

        expand_context*                                 m_ctx;
        u32                                             m_slot;
    };


    class expand_context {
    public:

        expand_context(const evaluation& e, std::string producer, pass_id pass) noexcept
            : m_query(e), m_producer(std::move(producer)), m_pass(pass) {}

        [[nodiscard]] const context& query() const noexcept { return m_query; }
        [[nodiscard]] rng random(const element& e, u64 salt = 0) const { return m_query.random(e, salt); }

        // New element as child of [parent]: path = parent.path / name[index]. Placement defaults to the parent's;
        // attributes are inherited from the parent through derive lineage events (core.propagation)
        element_builder emit(const element& parent, std::string_view name, i64 index, kind_id kind);
        // New element without a parent at [path] (no inherited attributes, construct placement)
        element_builder emit_at(const element_path& path, kind_id kind);
        // A relation between two elements (handles from emit() or committed elements)
        void relate(relation_id kind, name_id type, element_handle a, element_handle b, attribute_set params = {});
        void diagnose(severity s, const element& on, std::string message);

        // Evaluator side ----------------------------------------------------------------------------------------------
        struct staged_element { element value{}; element_handle parent{}; };
        struct staged_relation { relation value{}; };
        [[nodiscard]] std::vector<staged_element>& staged_elements() noexcept { return m_elements; }
        [[nodiscard]] std::vector<staged_relation>& staged_relations() noexcept { return m_relations; }
        [[nodiscard]] std::vector<diagnostic>& diagnostics() noexcept { return m_diagnostics; }

        static constexpr u32                            STAGED_BIT = 0x80000000u;

    private:

        context                                         m_query;
        std::string                                     m_producer;
        pass_id                                         m_pass;
        std::vector<staged_element>                     m_elements{};
        std::vector<staged_relation>                    m_relations{};
        std::vector<diagnostic>                         m_diagnostics{};
    };


    enum class expand_mode : u8 {
        per_element,     // expand() once per match
        batch,           // expand_all() once with every match of the round (global layouts, space colonization)
    };


    class production {
    public:
        virtual ~production() = default;
        virtual void expand(expand_context& /*ctx*/, const element& /*match*/) {}
        virtual void expand_all(expand_context& ctx, std::span<const element* const> matches) {
            for (const element* e : matches)
                expand(ctx, *e);
        }
    };


    struct production_desc {

        std::string                                     name{};
        matcher                                         match{};
        std::string                                     pass{ "primary" };
        expand_mode                                     mode = expand_mode::per_element;
        std::vector<kind_id>                            emits{};            // empty: anything; else emitting others is an error
        i32                                             priority = 0;       // several productions on one element: higher first
        bool                                            exclusive = false;  // lower-priority productions skip the element
        std::function<std::unique_ptr<production>()>    create{};
    };


    struct pass_desc {

        std::string                                     name{};             // "primary", "secondary", "detail", pack passes
        std::vector<std::string>                        after{}, before{};
        u32                                             max_depth = 64;     // fixpoint limit inside this pass
        bool                                            lod_droppable = false;
    };

    // Realizers ----------------------------------------------------------------------------------------------------------

    class realize_context {
    public:

        realize_context(const evaluation& e, const element& target, std::string realizer) noexcept
            : m_query(e), m_element(&target), m_realizer(std::move(realizer)) {}

        [[nodiscard]] const context& query() const noexcept { return m_query; }
        [[nodiscard]] const element& target() const noexcept { return *m_element; }
        [[nodiscard]] const propagator& propagation() const { return m_query.propagation_for(*m_element); }
        [[nodiscard]] rng random(u64 salt = 0) const { return m_query.random(*m_element, salt); }
        [[nodiscard]] const lod_request& lod() const noexcept { return m_query.lod(); }

        // Products, in the element's frame. The element's attributes are copied into each product's tags
        void mesh(std::string_view name, geometry::mesh m, f32 smooth_angle_degrees = 40.0f);
        void instances(std::string_view name, std::string asset, std::vector<frame> placements);
        void marker(std::string_view name, const frame& at, attribute_set tags = {});
        void diagnose(severity s, std::string message);

        [[nodiscard]] std::vector<product>& products() noexcept { return m_products; }
        [[nodiscard]] std::vector<diagnostic>& diagnostics() noexcept { return m_diagnostics; }

    private:

        context                                         m_query;
        const element*                                  m_element;
        std::string                                     m_realizer;
        std::vector<product>                            m_products{};
        std::vector<diagnostic>                         m_diagnostics{};
    };


    class realizer {
    public:
        virtual ~realizer() = default;
        virtual void realize(realize_context& ctx, const element& e) = 0;
    };


    struct realizer_desc {

        std::string                                     name{};
        matcher                                         match{};
        i32                                             priority = 0;       // the first matching realizer by priority realizes the element
        std::function<std::unique_ptr<realizer>()>      create{};
    };

    // Finishers ----------------------------------------------------------------------------------------------------------

    class finish_context {
    public:

        finish_context(const evaluation& e, std::vector<product>& products) noexcept : m_query(e), m_products(&products) {}

        [[nodiscard]] const context& query() const noexcept { return m_query; }
        [[nodiscard]] std::vector<product>& products() noexcept { return *m_products; }
        void diagnose(severity s, std::string message) { m_diagnostics.push_back({ s, {}, {}, std::move(message) }); }
        [[nodiscard]] std::vector<diagnostic>& diagnostics() noexcept { return m_diagnostics; }

    private:

        context                                         m_query;
        std::vector<product>*                           m_products;
        std::vector<diagnostic>                         m_diagnostics{};
    };


    class finisher {
    public:
        virtual ~finisher() = default;
        virtual void finish(finish_context& ctx) = 0;
    };


    struct finisher_desc {

        std::string                                     name{};
        std::vector<std::string>                        after{}, before{};
        std::function<std::unique_ptr<finisher>()>      create{};
    };

    // Stages -------------------------------------------------------------------------------------------------------------

    // @brief What a stage sees: queries, plus the machinery of the built-in stages so custom stages can reuse it
    class stage_context {
    public:

        explicit stage_context(evaluation& e) noexcept : m_eval(&e) {}

        [[nodiscard]] context query() const noexcept;

        void run_interpret();
        void run_generators();                                  // the construct's stages, in order
        void run_productions(std::string_view pass);
        void run_all_passes();
        void run_realizers();
        void run_finishers();
        void build_outputs();

        // Direct edits for custom stages (deterministic order is the stage's responsibility)
        element_handle add_element(element e);
        void add_relation(relation r);
        [[nodiscard]] std::vector<product>& products();
        [[nodiscard]] std::vector<output_result>& outputs();

        [[nodiscard]] bool cancelled() const noexcept;
        void diagnose(severity s, std::string source, std::string message);

    private:

        evaluation*                                     m_eval;
    };


    class stage {
    public:
        virtual ~stage() = default;
        virtual void run(stage_context& ctx) = 0;
    };


    // A wrapping stage gets the stage it wraps
    class stage_wrapper : public stage {
    public:
        void set_next(stage* next) noexcept { m_next = next; }
    protected:
        void run_next(stage_context& ctx) { if (m_next) m_next->run(ctx); }
    private:
        stage*                                          m_next = nullptr;
    };


    enum class stage_mode : u8 {
        insert,         // a new stage, placed by after / before
        wrap,           // decorates [target]
        replace,        // takes the place of [target] (core stages included)
    };


    struct stage_desc {

        std::string                                     name{};
        stage_mode                                      mode = stage_mode::insert;
        std::string                                     target{};           // wrap / replace
        std::vector<std::string>                        after{}, before{};  // insert
        std::function<std::unique_ptr<stage>()>         create{};
    };

    // TEMPLATE IMPLEMENTATION =========================================================================================

    template<hook H>
    policy_view<H> context::policy(const element& e, const region_selector& where) const {

        std::vector<const void*> raw;
        std::vector<policy_scope> scopes;
        composition mode = H::default_composition;
        policies(H::name, &e, where, raw, &scopes, mode);
        std::vector<const typename H::interface*> typed;
        typed.reserve(raw.size());
        for (const void* p : raw)
            typed.push_back(static_cast<const typename H::interface*>(p));
        if constexpr (requires { H::merge(std::span<const typename H::interface* const>{}); }) {
            if (mode == composition::merge) {
                auto merged = H::merge(std::span<const typename H::interface* const>(typed));
                return policy_view<H>(std::move(typed), std::move(scopes), mode, std::move(merged));
            }
        }
        return policy_view<H>(std::move(typed), std::move(scopes), mode);
    }

}
