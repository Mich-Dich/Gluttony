#include "util/pch.h"
#include "trace.h"
#include "profiler.h"

#include <chrono>
#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <format>
#include <fstream>
#include <ctime>

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
    std::atomic<u32>                            g_capture_remaining{ 0 };
    std::atomic<u32>                            g_main_thread{ 0 };

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
        g_main_thread.store(main.index, std::memory_order_relaxed);
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

        {
            std::lock_guard lock(g_history_mutex);
            g_history.push_back(std::move(frame));
            while (g_history.size() > HISTORY_FRAMES)
                g_history.pop_front();
        }

        // Fixed-length capture: pause after the requested number of frames
        if (u32 remaining = g_capture_remaining.load(std::memory_order_relaxed); remaining > 0) {
            if (--remaining == 0)
                set_paused(true);
            g_capture_remaining.store(remaining, std::memory_order_relaxed);
        }
    }


    void capture_frames(u32 frames) noexcept {

        clear_history();
        set_capture(true);
        set_paused(false);
        g_capture_remaining.store(std::min(frames, HISTORY_FRAMES), std::memory_order_relaxed);
    }


    u32 capture_frames_remaining() noexcept { return g_capture_remaining.load(std::memory_order_relaxed); }


    u32 main_thread_index() noexcept { return g_main_thread.load(std::memory_order_relaxed); }


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


    const char* intern(std::string_view name) {

        static std::mutex mutex;
        static std::unordered_set<std::string> strings;         // node based: element addresses are stable
        std::lock_guard lock(mutex);
        return strings.emplace(name).first->c_str();
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



    // EXPORT ==========================================================================================================

    namespace {

        struct tree_node {
            std::string_view                    name;
            u32                                 parent = UINT32_MAX;
            u32                                 thread = 0;
            std::vector<u32>                    children;
            f64                                 inclusive_ms = 0.0;
            f64                                 self_ms = 0.0;
            f64                                 max_ms = 0.0;
            u64                                 calls = 0;
            zone_kind                           kind = zone_kind::work;
        };

        struct analysis {
            std::vector<tree_node>              nodes;
            std::vector<u32>                    roots;          // per thread / top level nodes
        };

        std::string_view zone_name(const zone_record& z) { return z.name && *z.name ? std::string_view(z.name) : "<unnamed>"; }

        u32 find_or_add_child(analysis& a, u32 parent, u32 thread, std::string_view name, zone_kind kind) {

            auto& list = parent == UINT32_MAX ? a.roots : a.nodes[parent].children;
            for (u32 id : list)
                if (a.nodes[id].name == name && a.nodes[id].thread == thread)
                    return id;
            const u32 id = static_cast<u32>(a.nodes.size());
            tree_node n{};
            n.name = name;
            n.parent = parent;
            n.thread = thread;
            n.kind = kind;
            a.nodes.push_back(std::move(n));
            (parent == UINT32_MAX ? a.roots : a.nodes[parent].children).push_back(id);
            return id;
        }

        // Rebuilds the nesting of one frame's zones (per thread, by depth + time) and folds it into the aggregate tree.
        // [instance_parent] receives, per zone, the index of its parent zone in [frame.zones] (or UINT32_MAX)
        void fold_frame(analysis& a, const frame_record& frame, std::vector<u32>* instance_parent = nullptr) {

            const auto& zones = frame.zones;
            std::vector<u32> order(zones.size());
            for (u32 i = 0; i < zones.size(); ++i)
                order[i] = i;
            std::sort(order.begin(), order.end(), [&](u32 l, u32 r) {
                if (zones[l].thread != zones[r].thread) return zones[l].thread < zones[r].thread;
                if (zones[l].start_ns != zones[r].start_ns) return zones[l].start_ns < zones[r].start_ns;
                return zones[l].depth < zones[r].depth;
            });

            std::vector<u32> node_of(zones.size(), UINT32_MAX);
            std::vector<f64> child_ms(zones.size(), 0.0);
            if (instance_parent)
                instance_parent->assign(zones.size(), UINT32_MAX);

            std::vector<u32> stack;
            u32 thread = UINT32_MAX;
            for (const u32 i : order) {
                const auto& z = zones[i];
                if (z.thread != thread) {
                    stack.clear();
                    thread = z.thread;
                }
                while (!stack.empty() && (zones[stack.back()].depth >= z.depth || zones[stack.back()].end_ns <= z.start_ns))
                    stack.pop_back();

                const u32 parent_zone = stack.empty() ? UINT32_MAX : stack.back();
                const u32 parent_node = parent_zone == UINT32_MAX ? UINT32_MAX : node_of[parent_zone];
                const u32 node = find_or_add_child(a, parent_node, z.thread, zone_name(z), z.kind);
                node_of[i] = node;
                if (instance_parent)
                    (*instance_parent)[i] = parent_zone;

                const f64 ms = static_cast<f64>(z.end_ns - z.start_ns) * 1e-6;
                if (parent_zone != UINT32_MAX)
                    child_ms[parent_zone] += ms;
                auto& n = a.nodes[node];
                n.inclusive_ms += ms;
                n.max_ms = std::max(n.max_ms, ms);
                n.calls++;
                stack.push_back(i);
            }

            for (u32 i = 0; i < zones.size(); ++i)
                a.nodes[node_of[i]].self_ms += std::max(static_cast<f64>(zones[i].end_ns - zones[i].start_ns) * 1e-6 - child_ms[i], 0.0);
        }

        const char* kind_tag(zone_kind k) { return k == zone_kind::wait ? " [wait]" : k == zone_kind::idle ? " [idle]" : ""; }

        void print_node(std::string& out, const analysis& a, u32 id, int depth, f64 frames, f64 min_ms) {

            const auto& n = a.nodes[id];
            const f64 avg = n.inclusive_ms / frames;
            if (avg < min_ms && depth > 0)
                return;
            out += std::format("{:>9.3f} {:>9.3f} {:>9.3f} {:>8.1f}  {}{}{}\n", avg, n.self_ms / frames, n.max_ms,
                static_cast<f64>(n.calls) / frames, std::string(static_cast<size_t>(depth) * 2, ' '), n.name, kind_tag(n.kind));
            if (depth >= 24)
                return;
            std::vector<u32> kids = n.children;
            std::sort(kids.begin(), kids.end(), [&](u32 l, u32 r) { return a.nodes[l].inclusive_ms > a.nodes[r].inclusive_ms; });
            for (u32 c : kids)
                print_node(out, a, c, depth + 1, frames, min_ms);
        }

        f64 percentile(std::vector<f64> v, f64 p) {
            if (v.empty()) return 0.0;
            std::sort(v.begin(), v.end());
            const size_t i = std::min(v.size() - 1, static_cast<size_t>(p * static_cast<f64>(v.size() - 1) + 0.5));
            return v[i];
        }

        void json_escape(std::string& out, std::string_view s) {
            for (char c : s) {
                switch (c) {
                    case '"':  out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (static_cast<unsigned char>(c) < 0x20) out += std::format("\\u{:04x}", static_cast<unsigned>(c));
                        else out += c;
                }
            }
        }
    }


    std::string summary_text(const std::vector<frame_record>& frames) {

        std::string out;
        if (frames.empty())
            return "No frames recorded (open the Tracing window and enable Record / Capture).\n";

        const auto names = thread_names();
        const u32 main_thread = main_thread_index();
        auto thread_name = [&](u32 t) { return t < names.size() ? names[t] : std::format("thread {}", t); };

        // ---- per-frame statistics ---------------------------------------------------------------------------------
        std::vector<f64> total, wait, idle, work;
        size_t worst = 0;
        for (size_t i = 0; i < frames.size(); ++i) {
            const auto& f = frames[i];
            f64 idle_ms = 0.0;
            for (const auto& z : f.zones)
                if (z.kind == zone_kind::idle && z.thread == main_thread && z.depth == 0)
                    idle_ms += static_cast<f64>(z.end_ns - z.start_ns) * 1e-6;
            const f64 t = f.duration_ms();
            total.push_back(t);
            wait.push_back(f.wait_ms());
            idle.push_back(idle_ms);
            work.push_back(std::max(t - f.wait_ms() - idle_ms, 0.0));
            if (t > total[worst]) worst = i;
        }
        auto avg = [](const std::vector<f64>& v) { f64 s = 0; for (f64 x : v) s += x; return v.empty() ? 0.0 : s / static_cast<f64>(v.size()); };

        out += std::format("=== Gluttony trace summary ===\n");
        out += std::format("frames {} (#{} .. #{}), tracy {} ({})\n", frames.size(), frames.front().index, frames.back().index,
            tracy_version(), tracy_connected() ? "connected" : "not connected");
        out += std::format("frame ms   avg {:.3f}  p50 {:.3f}  p95 {:.3f}  p99 {:.3f}  max {:.3f} (frame #{})\n",
            avg(total), percentile(total, 0.5), percentile(total, 0.95), percentile(total, 0.99),
            total[worst], frames[worst].index);
        out += std::format("main thread per frame: work {:.3f}  wait (GPU/swapchain/vsync) {:.3f}  limiter {:.3f} ms\n",
            avg(work), avg(wait), avg(idle));
        out += "threads:";
        for (u32 t = 0; t < names.size(); ++t)
            out += std::format(" [{}] {}{}", t, names[t], t == main_thread ? " (main)" : "");
        out += "\n\n";

        // ---- aggregate tree ---------------------------------------------------------------------------------------
        analysis a;
        for (const auto& f : frames)
            fold_frame(a, f);
        const f64 n = static_cast<f64>(frames.size());

        // hottest by self time (merged by name over all threads)
        struct flat { std::string_view name; f64 self = 0, incl = 0, max = 0; u64 calls = 0; zone_kind kind = zone_kind::work; };
        std::vector<flat> flat_list;
        for (const auto& node : a.nodes) {
            auto it = std::find_if(flat_list.begin(), flat_list.end(), [&](const flat& f) { return f.name == node.name; });
            if (it == flat_list.end()) {
                flat_list.push_back({ node.name, 0, 0, 0, 0, node.kind });
                it = flat_list.end() - 1;
            }
            it->self += node.self_ms;
            it->incl += node.inclusive_ms;
            it->max = std::max(it->max, node.max_ms);
            it->calls += node.calls;
        }
        std::sort(flat_list.begin(), flat_list.end(), [](const flat& l, const flat& r) { return l.self > r.self; });

        const f64 frame_avg = std::max(avg(total), 1e-6);
        out += "--- hottest zones by SELF time (ms per frame) ---\n";
        out += "     self      incl       max  calls/f   %frame  zone\n";
        for (size_t i = 0; i < std::min<size_t>(flat_list.size(), 40); ++i) {
            const auto& f = flat_list[i];
            out += std::format("{:>9.3f} {:>9.3f} {:>9.3f} {:>8.1f} {:>7.1f}%  {}{}\n", f.self / n, f.incl / n, f.max,
                static_cast<f64>(f.calls) / n, 100.0 * (f.self / n) / frame_avg, f.name, kind_tag(f.kind));
        }

        // call tree per thread (main first)
        std::vector<u32> threads;
        for (u32 r : a.roots)
            if (std::find(threads.begin(), threads.end(), a.nodes[r].thread) == threads.end())
                threads.push_back(a.nodes[r].thread);
        std::sort(threads.begin(), threads.end(), [&](u32 l, u32 r) { return (l == main_thread) > (r == main_thread) || ((l == main_thread) == (r == main_thread) && l < r); });

        out += "\n--- call tree, avg per frame (zones < 0.01 ms omitted) ---\n";
        out += "     incl      self       max  calls/f  zone\n";
        for (u32 t : threads) {
            out += std::format("## {}\n", thread_name(t));
            std::vector<u32> roots;
            for (u32 r : a.roots)
                if (a.nodes[r].thread == t)
                    roots.push_back(r);
            std::sort(roots.begin(), roots.end(), [&](u32 l, u32 r) { return a.nodes[l].inclusive_ms > a.nodes[r].inclusive_ms; });
            for (u32 r : roots)
                print_node(out, a, r, 0, n, 0.01);
        }

        // ---- slowest frame, every zone -----------------------------------------------------------------------------
        const auto& wf = frames[worst];
        out += std::format("\n--- slowest frame #{}: {:.3f} ms (wait {:.3f} ms), zones >= 0.01 ms in start order ---\n",
            wf.index, wf.duration_ms(), wf.wait_ms());
        out += "  start ms     dur ms  zone\n";
        {
            std::vector<u32> order(wf.zones.size());
            for (u32 i = 0; i < order.size(); ++i) order[i] = i;
            std::sort(order.begin(), order.end(), [&](u32 l, u32 r) {
                const auto& zl = wf.zones[l]; const auto& zr = wf.zones[r];
                if ((zl.thread == main_thread) != (zr.thread == main_thread)) return zl.thread == main_thread;
                if (zl.thread != zr.thread) return zl.thread < zr.thread;
                if (zl.start_ns != zr.start_ns) return zl.start_ns < zr.start_ns;
                return zl.depth < zr.depth;
            });
            u32 thread = UINT32_MAX;
            u32 lines = 0;
            for (u32 i : order) {
                const auto& z = wf.zones[i];
                const f64 ms = static_cast<f64>(z.end_ns - z.start_ns) * 1e-6;
                if (ms < 0.01) continue;
                if (z.thread != thread) {
                    thread = z.thread;
                    out += std::format("## {}\n", thread_name(thread));
                }
                const f64 start = (static_cast<f64>(z.start_ns) - static_cast<f64>(wf.start_ns)) * 1e-6;
                out += std::format("{:>10.3f} {:>10.3f}  {}{}{}\n", start, ms, std::string(z.depth * 2u, ' '), zone_name(z), kind_tag(z.kind));
                if (++lines >= 600) { out += "  ... (truncated)\n"; break; }
            }
        }

        // ---- application stats + system providers (GPU pass timings, ...) ------------------------------------------
        const auto app = get_application_stats();
        out += std::format("\n--- application stats (latest frame) ---\nframe {:.3f} ms, fps {:.1f}, cpu work {:.3f}, cpu wait {:.3f}, "
            "limiter {:.3f}, gpu {:.3f} ms\ndraw calls {}, triangles {}, passes {}, vram {} MB, ram {} MB\n",
            app.frame_time_ms, app.fps, app.cpu_time_ms, app.cpu_wait_ms, app.limiter_ms, app.render.gpu_time_ms,
            app.render.draw_calls, app.render.triangles, app.render.render_passes, app.render.vram_bytes >> 20, app.ram_bytes >> 20);
        for (const auto& [section, stats] : collect_systems()) {
            out += std::format("[{}]\n", section);
            for (const auto& v : stats.custom)
                out += std::format("  {:<40} {:.3f}\n", v.name, v.value);
        }
        return out;
    }


    std::string chrome_trace_json(const std::vector<frame_record>& frames) {

        std::string out;
        out.reserve(1 << 20);
        out += "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[\n";
        if (frames.empty()) {
            out += "]}\n";
            return out;
        }

        const u64 origin = frames.front().start_ns;
        auto us = [&](u64 ns) { return (static_cast<f64>(ns) - static_cast<f64>(origin)) * 1e-3; };
        bool first = true;
        auto sep = [&]() { if (!first) out += ",\n"; first = false; };

        const auto names = thread_names();
        for (u32 t = 0; t < names.size(); ++t) {
            sep();
            out += std::format("{{\"ph\":\"M\",\"pid\":1,\"tid\":{},\"name\":\"thread_name\",\"args\":{{\"name\":\"", t);
            json_escape(out, names[t]);
            out += "\"}}";
        }

        for (const auto& f : frames) {
            sep();
            out += std::format("{{\"ph\":\"i\",\"s\":\"g\",\"pid\":1,\"tid\":{},\"ts\":{:.3f},\"name\":\"frame {} ({:.3f} ms, wait {:.3f})\"}}",
                main_thread_index(), us(f.start_ns), f.index, f.duration_ms(), f.wait_ms());
            for (const auto& z : f.zones) {
                sep();
                out += "{\"ph\":\"X\",\"pid\":1,\"name\":\"";
                json_escape(out, zone_name(z));
                out += std::format("\",\"cat\":\"{}\",\"tid\":{},\"ts\":{:.3f},\"dur\":{:.3f}}}",
                    z.kind == zone_kind::wait ? "wait" : z.kind == zone_kind::idle ? "idle" : "work", z.thread, us(z.start_ns),
                    static_cast<f64>(z.end_ns - z.start_ns) * 1e-3);
            }
        }
        out += "\n]}\n";
        return out;
    }


    std::filesystem::path export_to_directory(const std::vector<frame_record>& frames, const std::filesystem::path& dir) {

        std::error_code ec;
        std::filesystem::create_directories(dir, ec);

        const auto now = std::chrono::system_clock::now();
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#if defined(PLATFORM_WINDOWS)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tm);

        const auto json_path = dir / (std::string("trace_") + stamp + ".json");
        const auto summary_path = dir / (std::string("trace_") + stamp + "_summary.txt");

        {
            std::ofstream json(json_path, std::ios::binary);
            if (!json)
                return {};
            json << chrome_trace_json(frames);
        }
        {
            std::ofstream summary(summary_path, std::ios::binary);
            if (!summary)
                return {};
            summary << summary_text(frames);
        }
        return summary_path;
    }

}
