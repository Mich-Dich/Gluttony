#include "util/pch.h"
#include "trace.h"

#include <chrono>
#include <deque>
#include <memory>
#include <unordered_map>

#if defined(GLT_TRACY_ENABLED)
    #include <common/TracyVersion.hpp>
#endif



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::debug::trace {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Per-thread recording state. Owned by the global registry (never freed while the process runs: worker threads may
    // exit while the main thread is collecting, so the state outlives the thread and is just marked dead)
    struct thread_state {
        std::mutex                              mutex;              // zones: thread appends, main thread swaps out
        std::vector<zone_record>                zones;
        u32                                     index = 0;
        u16                                     depth = 0;          // only touched by the owning thread
        u16                                     wait_depth = 0;     // only touched by the owning thread
        std::atomic<u64>                        wait_ns{ 0 };       // outermost waits since the last frame_mark
    };

    struct registry {
        std::mutex                              mutex;
        std::vector<std::unique_ptr<thread_state>> threads;
        std::vector<std::string>                names;
    };

    // STATIC VARIABLES ================================================================================================

    std::atomic<bool>                           g_capture{ false };
    std::atomic<bool>                           g_paused{ false };
    std::atomic<f32>                            g_last_wait_ms{ 0.f };

    std::mutex                                  g_history_mutex{};
    std::deque<frame_record>                    g_history{};
    u64                                         g_frame_index = 0;
    u64                                         g_frame_start_ns = 0;

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    registry& get_registry() noexcept;
    thread_state& this_thread() noexcept;

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    registry& get_registry() noexcept {
        static registry r;
        return r;
    }


    thread_state& this_thread() noexcept {

        thread_local thread_state* state = nullptr;
        if (!state) {
            auto& r = get_registry();
            std::lock_guard lock(r.mutex);
            auto owned = std::make_unique<thread_state>();
            owned->index = static_cast<u32>(r.threads.size());
            state = owned.get();
            r.threads.push_back(std::move(owned));
            r.names.push_back("thread " + std::to_string(state->index));
        }
        return *state;
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    u64 now_ns() noexcept {
        return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }


    void set_capture(bool enabled) noexcept {

        const bool was = g_capture.exchange(enabled, std::memory_order_relaxed);
        if (enabled && !was) {
            // Drop zones recorded by threads before capture was (re)enabled
            auto& r = get_registry();
            std::lock_guard lock(r.mutex);
            for (auto& t : r.threads) {
                std::lock_guard tl(t->mutex);
                t->zones.clear();
            }
        }
    }

    bool is_capturing() noexcept { return g_capture.load(std::memory_order_relaxed); }

    void set_paused(bool paused) noexcept { g_paused.store(paused, std::memory_order_relaxed); }

    bool is_paused() noexcept { return g_paused.load(std::memory_order_relaxed); }


    void frame_mark() noexcept {

#if defined(GLT_TRACY_ENABLED)
        FrameMark;
#endif

        const u64 end = now_ns();
        thread_state& main = this_thread();
        const u64 main_wait = main.wait_ns.exchange(0, std::memory_order_relaxed);
        g_last_wait_ms.store(static_cast<f32>(main_wait) * 1e-6f, std::memory_order_relaxed);

        if (g_frame_start_ns == 0)
            g_frame_start_ns = end;

        frame_record frame{};
        frame.index = g_frame_index++;
        frame.start_ns = g_frame_start_ns;
        frame.end_ns = end;
        frame.main_wait_ns = main_wait;
        g_frame_start_ns = end;

        const bool keep = is_capturing() && !is_paused();

        // Always drain the per-thread buffers (so stale zones never pile up), keep them only when recording
        {
            auto& r = get_registry();
            std::lock_guard lock(r.mutex);
            for (auto& t : r.threads) {
                std::lock_guard tl(t->mutex);
                if (t->zones.empty())
                    continue;
                if (keep)
                    frame.zones.insert(frame.zones.end(), t->zones.begin(), t->zones.end());
                t->zones.clear();
            }
        }

        if (!keep)
            return;

        std::lock_guard lock(g_history_mutex);
        g_history.push_back(std::move(frame));
        while (g_history.size() > HISTORY_FRAMES)
            g_history.pop_front();
    }


    f32 last_frame_wait_ms() noexcept { return g_last_wait_ms.load(std::memory_order_relaxed); }


    f32 current_frame_wait_ms() noexcept {
        return static_cast<f32>(this_thread().wait_ns.load(std::memory_order_relaxed)) * 1e-6f;
    }


    std::vector<frame_record> history() {

        std::lock_guard lock(g_history_mutex);
        return { g_history.begin(), g_history.end() };
    }


    void clear_history() {

        std::lock_guard lock(g_history_mutex);
        g_history.clear();
    }


    void set_thread_name(const char* name) {

#if defined(GLT_TRACY_ENABLED)
        tracy::SetThreadName(name);
#endif
        thread_state& state = this_thread();
        auto& r = get_registry();
        std::lock_guard lock(r.mutex);
        r.names[state.index] = name ? name : "";
    }


    std::vector<std::string> thread_names() {

        auto& r = get_registry();
        std::lock_guard lock(r.mutex);
        return r.names;
    }


    bool tracy_compiled() noexcept {
#if defined(GLT_TRACY_ENABLED)
        return true;
#else
        return false;
#endif
    }


    bool tracy_connected() noexcept {
#if defined(GLT_TRACY_ENABLED)
        return TracyIsConnected;
#else
        return false;
#endif
    }


    const char* tracy_version() noexcept {
#if defined(GLT_TRACY_ENABLED)
        static const std::string version = std::to_string(tracy::Version::Major) + "." + std::to_string(tracy::Version::Minor)
            + "." + std::to_string(tracy::Version::Patch);
        return version.c_str();
#else
        return "not compiled in";
#endif
    }

    // CLASS IMPLEMENTATION ============================================================================================

    scoped_zone::scoped_zone(const char* name, zone_kind kind) noexcept
        : m_name(name), m_kind(kind) {

        m_record = g_capture.load(std::memory_order_relaxed);
        if (!m_record && kind != zone_kind::wait)
            return;

        thread_state& state = this_thread();
        m_depth = state.depth++;
        if (kind == zone_kind::wait)
            m_outer_wait = (state.wait_depth++ == 0);
        m_start_ns = now_ns();
    }


    scoped_zone::~scoped_zone() noexcept {

        if (!m_record && m_kind != zone_kind::wait)
            return;

        const u64 end = now_ns();
        thread_state& state = this_thread();
        state.depth = m_depth;
        if (m_kind == zone_kind::wait) {
            --state.wait_depth;
            if (m_outer_wait)
                state.wait_ns.fetch_add(end - m_start_ns, std::memory_order_relaxed);
        }

        if (!m_record)
            return;

        std::lock_guard lock(state.mutex);
        state.zones.push_back({ m_name, m_start_ns, end, state.index, m_depth, m_kind });
    }

}
