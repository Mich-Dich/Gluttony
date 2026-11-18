#pragma once

#include "procedural/construct/evaluate.h"

// Procedural construction (internal): the state of one evaluation. Pack code only sees it through context / stage_context.



namespace GLT::construct {

    class policy_propagator;


    // @brief One run of the pipeline over one construct
    class evaluation {
    public:

        evaluation(const registry_snapshot& snapshot, const evaluation_request& request);
        ~evaluation();

        void run(evaluation_result& result);

        // setup -------------------------------------------------------------------------------------------------------
        const registry_snapshot&                        snap;
        const evaluation_request&                       request;
        const construct_data&                           data;
        std::vector<std::string>                        active_packs{};     // "core" first, then the construct's packs
        std::unordered_map<attribute_id, const attribute_desc*> attribute_descs{};

        // active descriptors (name conflicts resolved) ------------------------------------------------------------------
        std::vector<const registered<pass_desc>*>       passes{};           // in execution order
        std::vector<const registered<production_desc>*> productions{};
        std::vector<const registered<realizer_desc>*>   realizers{};        // most preferred first
        std::vector<const registered<finisher_desc>*>   finishers{};        // in execution order

        // state ---------------------------------------------------------------------------------------------------------
        std::vector<element>                            elements{};
        std::unordered_map<element_path, u32>           by_path{};
        std::vector<std::vector<u32>>                   children_of{};
        std::vector<relation>                           relations{};
        std::vector<std::vector<u32>>                   relations_of{};
        std::vector<std::optional<volume_guide>>        analytic_volume{};  // unedited primitive guides: exact distance
        std::vector<product>                            products{};
        std::vector<output_result>                      outputs{};
        std::vector<diagnostic>                         diagnostics{};
        std::vector<stage_timing>                       timings{};
        std::vector<std::string>                        stage_order{};
        std::vector<stage_stats>                        stage_results{};
        u64                                             construct_seed = 0;
        bool                                            interpreted = false;

        // helpers -------------------------------------------------------------------------------------------------------
        [[nodiscard]] bool pack_active(std::string_view pack) const noexcept;
        [[nodiscard]] i32 pack_rank(std::string_view pack) const noexcept;  // position in active_packs (-1: inactive)
        [[nodiscard]] bool cancelled() const noexcept;
        [[nodiscard]] const attribute_desc* describe(attribute_id id) const noexcept;
        [[nodiscard]] const element_override* override_for(const element_path& p) const noexcept;
        [[nodiscard]] bool hidden(const element& e) const noexcept;

        element_handle commit(element e, std::string_view stage);
        void add_relation(relation r);
        void diagnose(severity s, const element_path& on, std::string_view source, std::string message);

        void policies(std::string_view hook, const element* e, const region_selector& region, std::vector<const void*>& out,
                      std::vector<policy_scope>* scopes, composition& mode) const;
        [[nodiscard]] const propagator& propagator_for(const element& e) const;
        [[nodiscard]] const i_propagation* mode_impl(name_id mode) const;
        [[nodiscard]] u64 seed_of(const element& e, u64 salt) const;
        [[nodiscard]] attribute_value parameter(std::string_view name) const;

        // built-in stage work ---------------------------------------------------------------------------------------------
        void interpret();
        void run_generators();
        void run_pass(std::string_view pass);
        // Commits what [ec] staged (relations resolved, diagnostics moved); appends the new element indices
        void commit_staged(expand_context& ec, std::string_view stage, std::vector<u32>& committed);
        void run_all_passes();
        void realize_all();
        void finish_all();
        void build_outputs();

    private:

        void resolve_active();
        void interpret_guide(const guide& g);

        mutable std::mutex                              m_propagator_mutex{};
        mutable std::unordered_map<u32, std::unique_ptr<policy_propagator>> m_propagators{};
    };

    // CLASS DECLARATION (propagation) ===============================================================================================

    // @brief Propagator with the policies in effect for one element (hook core.propagation, then the attribute's mode)
    class policy_propagator final : public propagator {
    public:

        policy_propagator(const evaluation& eval, const element& target) : m_eval(eval) {
            // Only what scope matching reads (no geometry copy)
            m_element.path = target.path;
            m_element.kind = target.kind;
            m_element.role = target.role;
            m_element.shape = target.shape;
            m_element.attributes = target.attributes;
            std::vector<const void*> raw;
            composition mode = propagation_hook::default_composition;
            m_eval.policies(propagation_hook::name, &m_element, whole_element{}, raw, nullptr, mode);
            for (const void* p : raw)
                m_chain.push_back(static_cast<const i_propagation*>(p));
            if (mode == composition::first_match && m_chain.size() > 1)
                m_chain.resize(1);
        }

        [[nodiscard]] const attribute_desc& describe(attribute_id id, const attribute_value& column_default) const override {
            if (const attribute_desc* d = m_eval.describe(id))
                return *d;
            std::scoped_lock lock(m_mutex);
            auto& slot = m_synth[id];
            if (!slot) {
                slot = std::make_unique<attribute_desc>();
                slot->id = id;
                slot->default_value = column_default;
                slot->domains = attribute_domain::all;
                slot->propagation = intern("copy");
            }
            return *slot;
        }

        void apply(const attribute_desc& desc, const lineage_event& event, attribute_io& io) const override {
            const name_id mode = desc.propagation.empty() ? intern("copy") : desc.propagation;
            const next_propagation chain(m_chain, m_eval.mode_impl(mode), mode);
            chain(desc, event, io);
        }

    private:

        const evaluation&                               m_eval;
        element                                         m_element;
        std::vector<const i_propagation*>               m_chain{};
        mutable std::mutex                              m_mutex{};
        mutable std::unordered_map<attribute_id, std::unique_ptr<attribute_desc>> m_synth{};
    };

}
