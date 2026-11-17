
#pragma once

#include <terrain/node.h>

#include "graph.h"



namespace GLT::terrain {

    // TYPES ===========================================================================================================

    // @brief "Give me pin [pin] of node [node] over [area]"
    //
    // [pin] is an output of the node, or an input of a sink node (surface, material layers): then the value flowing into
    // that input is returned. The result depends only on (graph, seed, parameters, area): evaluating two adjacent areas
    // gives exactly the samples of evaluating their union
    struct eval_request {

        std::shared_ptr<const terrain_graph>            graph{};        // immutable snapshot: edits make a new one
        u32                                             node = INVALID_NODE_ID;
        std::string                                     pin{};
        region                                          area{};
        u64                                             seed = 0;
        std::unordered_map<std::string, f32>            parameters{};   // exposed parameter overrides
    };


    struct eval_result {

        std::shared_ptr<const field>                    data{};
        f64                                             milliseconds = 0.0;
        u32                                             nodes_evaluated = 0;
        u32                                             cache_hits = 0;
        std::vector<std::string>                        warnings{};     // missing node types, ...
    };

    using eval_outcome = std::expected<eval_result, std::string>;

    // CLASS DECLARATION ===============================================================================================

    // @brief Evaluates terrain graphs for regions
    //
    // Per request: the upstream nodes of the target are sorted, each node's region is grown by the aprons of everything
    // downstream of it, then the nodes run in order. Results are cached per (node signature, region): the signature hashes
    // the node's type, parameters, seed and the signatures of its inputs, so changing one parameter only re-runs that node
    // and what depends on it. A cached region that contains the requested one is reused (fields are indexed by lattice
    // coordinates). Thread-safe: several requests may run at once
    class evaluator {
    public:

        explicit evaluator(const i_node_registry& registry, u64 cache_budget_bytes = 256ull * 1024 * 1024);

        [[nodiscard]] eval_outcome evaluate(const eval_request& request, const std::atomic<bool>* cancel = nullptr);

        // On GLT::thread_pool. [cancel] may be set from any thread to abandon the request
        [[nodiscard]] std::future<eval_outcome> evaluate_async(eval_request request, std::shared_ptr<std::atomic<bool>> cancel = {});

        void clear_cache();

        [[nodiscard]] u64 cache_bytes() const;

    private:

        using outputs = std::vector<std::shared_ptr<const field>>;

        struct cache_entry {
            region                                      area{};
            outputs                                     data{};
            u64                                         bytes = 0;
            u64                                         stamp = 0;          // last use, for eviction
        };

        [[nodiscard]] std::optional<outputs> cache_find(u64 signature, const region& area);
        void cache_store(u64 signature, const region& area, const outputs& data);

        const i_node_registry&                          m_registry;
        const u64                                       m_cache_budget;

        mutable std::mutex                              m_cache_mutex{};
        std::unordered_map<u64, std::vector<cache_entry>> m_cache{};
        u64                                             m_cache_bytes = 0;
        u64                                             m_cache_clock = 0;
    };

}
