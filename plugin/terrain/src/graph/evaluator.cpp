
#include "util/pch.h"
#include "evaluator.h"

#include "util/data_structures/thread_pool.inl"        // submit() is a template defined there (not part of the pch)



namespace GLT::terrain {

    // TYPES ===========================================================================================================

    namespace {

        // Per node of one request
        struct node_work {

            const graph_node*                           node = nullptr;
            std::shared_ptr<const node_type>            type{};             // null: unknown type (placeholder, outputs zero)
            std::unique_ptr<terrain::node>              instance{};
            region                                      area{};             // what this node must produce
            u64                                         signature = 0;
            u64                                         seed = 0;
            bool                                        needed = false;
        };


        [[nodiscard]] u64 region_bytes(const region& r) { return r.sample_count() * sizeof(f32); }

    }

    // CLASS IMPLEMENTATION ============================================================================================

    evaluator::evaluator(const i_node_registry& registry, u64 cache_budget_bytes)
        : m_registry(registry), m_cache_budget(cache_budget_bytes) {}


    eval_outcome evaluator::evaluate(const eval_request& request, const std::atomic<bool>* cancel) {

        const auto start = std::chrono::steady_clock::now();
        eval_result result{};

        if (!request.graph)
            return std::unexpected(std::string("no graph"));
        const terrain_graph& graph = *request.graph;
        if (request.area.empty() || request.area.width > MAX_REGION_SIDE || request.area.height > MAX_REGION_SIDE)
            return std::unexpected(std::format("invalid region {}x{}", request.area.width, request.area.height));
        if (!(request.area.spacing > 0.0))
            return std::unexpected(std::string("sample spacing must be positive"));

        // ---- resolve the target pin to (node, output index) ----------------------------------------------------------
        const graph_node* target = graph.find(request.node);
        if (!target)
            return std::unexpected(std::format("node {} not in the graph", request.node));

        u32 target_node = target->id;
        i32 target_output = -1;
        {
            std::shared_ptr<const node_type> type = m_registry.find(target->type);
            if (type && type->output_index(request.pin) >= 0) {
                target_output = type->output_index(request.pin);
            } else if (const graph_link* link = graph.source_of(target->id, request.pin)) {
                target_node = link->from_node;
                std::shared_ptr<const node_type> source_type = graph.find(target_node) ? m_registry.find(graph.find(target_node)->type) : nullptr;
                target_output = source_type ? source_type->output_index(link->from_pin) : -1;
                if (!graph.find(target_node))
                    return std::unexpected(std::format("link into node {} comes from missing node {}", target->id, target_node));
                if (source_type && target_output < 0)
                    return std::unexpected(std::format("node {} has no output '{}'", target_node, link->from_pin));
            } else if (type && type->input_index(request.pin) >= 0) {
                // Unconnected input of a sink: its default constant
                result.data = std::make_shared<const field>(request.area, type->inputs[static_cast<size_t>(type->input_index(request.pin))].default_value);
                return result;
            } else {
                return std::unexpected(std::format("node {} has no pin '{}'", target->id, request.pin));
            }
        }

        // ---- the upstream nodes, in evaluation order (inputs first) ---------------------------------------------------
        std::unordered_map<u32, node_work> work;
        std::vector<u32> order;
        {
            std::unordered_map<u32, u8> state;      // 0 new, 1 on the stack, 2 done
            std::string cycle_error;

            std::function<bool(u32)> visit = [&](u32 id) -> bool {
                u8& s = state[id];
                if (s == 2)
                    return true;
                if (s == 1) {
                    cycle_error = std::format("the graph has a cycle through node {}", id);
                    return false;
                }
                s = 1;
                const graph_node* node = graph.find(id);
                if (!node) {
                    cycle_error = std::format("link from missing node {}", id);
                    return false;
                }
                node_work& w = work[id];
                w.node = node;
                w.type = m_registry.find(node->type);
                if (w.type)
                    for (const auto& pin : w.type->inputs)
                        if (const graph_link* link = graph.source_of(id, pin.name))
                            if (!visit(link->from_node))
                                return false;
                state[id] = 2;
                order.push_back(id);
                return true;
            };
            if (!visit(target_node))
                return std::unexpected(cycle_error);
        }

        // ---- instances, parameters, seeds --------------------------------------------------------------------------
        u64 parameter_hash = 0;
        {
            std::vector<std::pair<std::string, f32>> sorted(request.parameters.begin(), request.parameters.end());
            std::sort(sorted.begin(), sorted.end());
            for (const auto& [name, value] : sorted)
                parameter_hash = hash_combine(hash_combine(parameter_hash, hash_string(name)), std::bit_cast<u32>(value));
        }

        for (u32 id : order) {
            node_work& w = work[id];
            w.seed = hash_combine(request.seed, id);
            if (!w.type) {
                result.warnings.push_back(std::format("node {}: unknown type '{}' (node pack not loaded?), outputs zero", id, w.node->type));
                continue;
            }
            w.instance = w.type->create();
            for (size_t p = 0; p < w.type->params.size(); ++p) {
                auto it = w.node->params.find(w.type->params[p].name);
                w.type->apply[p](*w.instance, it != w.node->params.end() ? it->second : w.type->params[p].default_value);
            }
        }

        // ---- regions: consumers first, each input grown by the consumer's apron ---------------------------------------
        work[target_node].area = request.area;
        work[target_node].needed = true;
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            node_work& w = work[*it];
            if (!w.needed || !w.type)
                continue;
            const u32 apron = w.instance ? w.instance->apron(request.area.spacing) : 0u;
            const region input_area = w.area.expanded(apron);
            for (const auto& pin : w.type->inputs)
                if (const graph_link* link = graph.source_of(*it, pin.name)) {
                    node_work& source = work[link->from_node];
                    source.area = source.needed ? source.area.united(input_area) : input_area;
                    source.needed = true;
                }
        }

        for (u32 id : order) {
            const region& r = work[id].area;
            if (work[id].needed && (r.width > MAX_REGION_SIDE || r.height > MAX_REGION_SIDE))
                return std::unexpected(std::format("node {} would need a {}x{} region (aprons too large for this request)", id, r.width, r.height));
        }

        // ---- signatures: type + parameters + seed + inputs ---------------------------------------------------------
        for (u32 id : order) {
            node_work& w = work[id];
            u64 sig = hash_combine(graph.node_hash(*w.node), w.seed);
            sig = hash_combine(sig, parameter_hash);
            // Node code changes (pack reloaded, placeholder resolved) must not reuse results of the old code
            sig = hash_combine(sig, m_registry.generation());
            sig = hash_combine(sig, w.type ? 1u : 0u);
            if (w.type)
                for (const auto& pin : w.type->inputs) {
                    sig = hash_combine(sig, hash_string(pin.name));
                    if (const graph_link* link = graph.source_of(id, pin.name))
                        sig = hash_combine(hash_combine(sig, work[link->from_node].signature), hash_string(link->from_pin));
                    else
                        sig = hash_combine(sig, std::bit_cast<u32>(pin.default_value));
                }
            w.signature = sig;
        }

        // ---- run ------------------------------------------------------------------------------------------------------
        std::unordered_map<u32, outputs> produced;
        std::vector<std::unique_ptr<field>> constants;      // unconnected inputs, placeholders

        for (u32 id : order) {

            if (cancel && cancel->load(std::memory_order_relaxed))
                return std::unexpected(std::string("cancelled"));

            node_work& w = work[id];
            if (!w.needed)
                continue;

            if (!w.type) {
                produced[id] = {};                          // placeholder: consumers read their pin's default constant
                continue;
            }

            if (auto cached = cache_find(w.signature, w.area)) {
                produced[id] = std::move(*cached);
                ++result.cache_hits;
                continue;
            }

            const u32 apron = w.instance->apron(request.area.spacing);
            const region input_area = w.area.expanded(apron);

            std::vector<const field*> inputs;
            inputs.reserve(w.type->inputs.size());
            for (const auto& pin : w.type->inputs) {
                const field* source_field = nullptr;
                if (const graph_link* link = graph.source_of(id, pin.name)) {
                    const node_work& source = work[link->from_node];
                    const outputs& out = produced[link->from_node];
                    const i32 index = source.type ? source.type->output_index(link->from_pin) : -1;
                    if (index >= 0 && static_cast<size_t>(index) < out.size())
                        source_field = out[static_cast<size_t>(index)].get();
                }
                if (!source_field) {
                    constants.push_back(std::make_unique<field>(input_area, pin.default_value));
                    source_field = constants.back().get();
                }
                inputs.push_back(source_field);
            }

            std::vector<field> out_fields(w.type->outputs.size());
            for (auto& f : out_fields)
                f.reset(w.area);

            context ctx(w.area, w.seed, inputs, out_fields, &request.parameters, cancel);
            w.instance->evaluate(ctx);
            ++result.nodes_evaluated;

            // A node may stop early when cancelled: its partial output must neither be cached nor returned
            if (cancel && cancel->load(std::memory_order_relaxed))
                return std::unexpected(std::string("cancelled"));

            outputs out;
            out.reserve(out_fields.size());
            for (auto& f : out_fields)
                out.push_back(std::make_shared<const field>(std::move(f)));
            cache_store(w.signature, w.area, out);
            produced[id] = std::move(out);
        }

        // ---- the requested pin, cut to the requested area ----------------------------------------------------------
        const node_work& target_work = work[target_node];
        const outputs& final_outputs = produced[target_node];
        std::shared_ptr<const field> data;
        if (target_work.type && target_output >= 0 && static_cast<size_t>(target_output) < final_outputs.size())
            data = final_outputs[static_cast<size_t>(target_output)];

        if (!data) {
            data = std::make_shared<const field>(request.area, 0.0f);
        } else if (data->area() != request.area) {
            auto cut = std::make_shared<field>(request.area);
            for (i64 gy = request.area.y0; gy < request.area.y1(); ++gy)
                for (i64 gx = request.area.x0; gx < request.area.x1(); ++gx)
                    cut->at(gx, gy) = data->at(gx, gy);
            data = std::move(cut);
        }

        result.data = std::move(data);
        result.milliseconds = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count();
        return result;
    }


    std::future<eval_outcome> evaluator::evaluate_async(eval_request request, std::shared_ptr<std::atomic<bool>> cancel) {

        return GLT::thread_pool::submit([this, request = std::move(request), cancel = std::move(cancel)]() {
            return evaluate(request, cancel.get());
        });
    }


    void evaluator::clear_cache() {

        std::lock_guard lock(m_cache_mutex);
        m_cache.clear();
        m_cache_bytes = 0;
    }


    u64 evaluator::cache_bytes() const {

        std::lock_guard lock(m_cache_mutex);
        return m_cache_bytes;
    }

    // CLASS PRIVATE ===================================================================================================

    std::optional<evaluator::outputs> evaluator::cache_find(u64 signature, const region& area) {

        std::lock_guard lock(m_cache_mutex);
        auto it = m_cache.find(signature);
        if (it == m_cache.end())
            return std::nullopt;
        for (auto& entry : it->second)
            if (entry.area.spacing == area.spacing && entry.area.contains(area)) {
                entry.stamp = ++m_cache_clock;
                return entry.data;
            }
        return std::nullopt;
    }


    void evaluator::cache_store(u64 signature, const region& area, const outputs& data) {

        const u64 bytes = region_bytes(area) * std::max<u64>(data.size(), 1);
        if (bytes > m_cache_budget / 4)
            return;                                         // one huge result would flush everything else

        std::lock_guard lock(m_cache_mutex);
        m_cache[signature].push_back({ area, data, bytes, ++m_cache_clock });
        m_cache_bytes += bytes;

        // Evict least recently used entries until under budget
        while (m_cache_bytes > m_cache_budget) {
            u64 oldest = std::numeric_limits<u64>::max();
            u64 oldest_signature = 0;
            size_t oldest_index = 0;
            for (const auto& [sig, entries] : m_cache)
                for (size_t i = 0; i < entries.size(); ++i)
                    if (entries[i].stamp < oldest) {
                        oldest = entries[i].stamp;
                        oldest_signature = sig;
                        oldest_index = i;
                    }
            if (oldest == std::numeric_limits<u64>::max())
                break;
            auto& entries = m_cache[oldest_signature];
            m_cache_bytes -= entries[oldest_index].bytes;
            entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(oldest_index));
            if (entries.empty())
                m_cache.erase(oldest_signature);
        }
    }

}
