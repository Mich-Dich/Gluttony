#include "util/pch.h"
#include "profiler.h"

#include <chrono>
#include <mutex>
#include <unordered_map>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::debug {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    application_stats                                   g_app_stats{};

    std::mutex                                          g_provider_mutex{};

    std::unordered_map<u64, stats_provider>             g_providers{};

    std::vector<u64>                                    g_provider_order{};         // preserves registration order

    u64                                                 g_next_provider_id = 1;

    std::mutex                                          g_scope_mutex{};            // Scope timing accumulators. Keyed by scope name.

    std::unordered_map<std::string, f32>                g_scope_accum{};

    std::chrono::steady_clock::time_point               g_last_frame_start{};       // Wall-clock for frame timing.

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    [[nodiscard]] u64 now_ns();

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    [[nodiscard]] u64 now_ns() {

        return static_cast<u64>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    void update_app_stats(const application_stats& stats) { g_app_stats = stats; }


    [[nodiscard]] application_stats get_application_stats() { return g_app_stats; }


    [[nodiscard]] u64 register_provider(const stats_provider provider) {

        std::lock_guard lock(g_provider_mutex);
        const u64 id = g_next_provider_id++;

        g_providers.emplace(id, std::move(provider));
        g_provider_order.push_back(id);

        return id;
    }


    void unregister_provider(const u64 id) {

        std::lock_guard lock(g_provider_mutex);

        g_providers.erase(id);
        g_provider_order.erase(
            std::remove(g_provider_order.begin(), g_provider_order.end(), id),
            g_provider_order.end());
    }


    [[nodiscard]] std::vector<std::pair<std::string, system_stats>> collect_systems() {

        std::vector<std::pair<std::string, system_stats>> out{};
        std::lock_guard lock(g_provider_mutex);
        out.reserve(g_provider_order.size());

        for (u64 id : g_provider_order) {

            auto it = g_providers.find(id);
            if (it == g_providers.end())
                continue;

            auto& provider = it->second;

            system_stats section{};
            if (provider.fill)
                provider.fill(section);

            out.emplace_back(provider.name, std::move(section));
        }

        return out;
    }

    // ---- scope timer -----------------------------------------------------------------------------------------------

    scope_timer::scope_timer(const char* n)
        : name(n), start_ns(now_ns()) {}


    scope_timer::~scope_timer() {

        const u64 end_ns = now_ns();
        const f32 ms = static_cast<f32>(end_ns - start_ns) * 1e-6f;

        std::lock_guard lock(g_scope_mutex);
        g_scope_accum[name] += ms;
    }


    [[nodiscard]] std::vector<std::pair<std::string, f32>> drain_scope_timings() {

        std::vector<std::pair<std::string, f32>> out{};

        std::lock_guard lock(g_scope_mutex);
        out.reserve(g_scope_accum.size());

        for (auto& [name, ms] : g_scope_accum)
            out.emplace_back(name, ms);

        g_scope_accum.clear();
        return out;
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
