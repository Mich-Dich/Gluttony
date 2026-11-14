#include "util/pch.h"
#include "tracing.h"

#include <imgui.h>

#include <util/ui/panel_collection.h>
#include <config/imgui_config.h>
#include <application.h>

#if defined(PLATFORM_LINUX)
    #include <spawn.h>
    #include <sys/wait.h>
    #include <unistd.h>
    extern char** environ;
#endif



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    constexpr ImU32                             COLOR_WORK = IM_COL32(255, 166, 102, 255);
    constexpr ImU32                             COLOR_WAIT = IM_COL32(217, 77, 77, 255);
    constexpr ImU32                             COLOR_IDLE = IM_COL32(110, 110, 110, 255);
    constexpr ImU32                             COLOR_BUDGET = IM_COL32(255, 115, 115, 170);
    constexpr ImU32                             COLOR_SELECTED = IM_COL32(255, 255, 255, 60);

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    [[nodiscard]] ImU32 zone_color(std::string_view name, GLT::debug::trace::zone_kind kind);
    [[nodiscard]] f32 to_ms(u64 ns);
    [[nodiscard]] std::string find_in_path(const std::string& name);
    [[nodiscard]] std::string default_tracy_path();
    [[nodiscard]] std::vector<f64> self_times(const GLT::debug::trace::frame_record& frame);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // Stable pastel colour per zone name; waits always red, idle grey
    ImU32 zone_color(std::string_view name, GLT::debug::trace::zone_kind kind) {

        if (kind == GLT::debug::trace::zone_kind::wait)
            return COLOR_WAIT;
        if (kind == GLT::debug::trace::zone_kind::idle)
            return COLOR_IDLE;

        u32 h = 2166136261u;
        for (char c : name)
            h = (h ^ static_cast<u8>(c)) * 16777619u;
        const f32 hue = static_cast<f32>(h % 360u) / 360.f;
        f32 r, g, b;
        ImGui::ColorConvertHSVtoRGB(hue, 0.45f, 0.85f, r, g, b);
        return ImGui::GetColorU32(ImVec4(r, g, b, 1.f));
    }


    f32 to_ms(u64 ns) { return static_cast<f32>(ns) * 1e-6f; }


    std::string find_in_path(const std::string& name) {

        if (name.empty())
            return {};
        if (name.find('/') != std::string::npos)
            return std::filesystem::exists(name) ? name : std::string{};

        const char* path_env = std::getenv("PATH");
        if (!path_env)
            return {};

        std::string_view paths = path_env;
        while (!paths.empty()) {
            const size_t sep = paths.find(':');
            const std::string_view dir = paths.substr(0, sep);
            if (!dir.empty()) {
                const std::filesystem::path candidate = std::filesystem::path(dir) / name;
                std::error_code ec;
                if (std::filesystem::is_regular_file(candidate, ec))
                    return candidate.string();
            }
            if (sep == std::string_view::npos)
                break;
            paths.remove_prefix(sep + 1);
        }
        return {};
    }


    // The profiler app must match the client's protocol version: prefer the one built from the vendored sources
    std::string default_tracy_path() {

#if defined(GLT_TRACY_SOURCE_DIR)
        for (const char* rel : { "profiler/build/tracy-profiler", "build/profiler/tracy-profiler" }) {
            const std::filesystem::path p = std::filesystem::path(GLT_TRACY_SOURCE_DIR) / rel;
            std::error_code ec;
            if (std::filesystem::is_regular_file(p, ec))
                return p.string();
        }
#endif
        for (const char* name : { "tracy-profiler", "tracy" })
            if (auto p = find_in_path(name); !p.empty())
                return p;
        return "tracy-profiler";
    }


    // Self time of every zone in [frame.zones] (inclusive minus direct children on the same thread)
    std::vector<f64> self_times(const GLT::debug::trace::frame_record& frame) {

        const auto& zones = frame.zones;
        std::vector<f64> self(zones.size());
        std::vector<u32> order(zones.size());
        for (u32 i = 0; i < zones.size(); ++i) {
            order[i] = i;
            self[i] = static_cast<f64>(zones[i].end_ns - zones[i].start_ns) * 1e-6;
        }

        std::sort(order.begin(), order.end(), [&](u32 a, u32 b) {
            if (zones[a].thread != zones[b].thread) return zones[a].thread < zones[b].thread;
            if (zones[a].start_ns != zones[b].start_ns) return zones[a].start_ns < zones[b].start_ns;
            return zones[a].depth < zones[b].depth;
        });

        std::vector<u32> stack;
        u32 thread = UINT32_MAX;
        for (const u32 i : order) {
            const auto& z = zones[i];
            if (z.thread != thread) {
                stack.clear();
                thread = z.thread;
            }
            while (!stack.empty() && zones[stack.back()].end_ns <= z.start_ns)
                stack.pop_back();
            if (!stack.empty() && zones[stack.back()].depth + 1 == z.depth)
                self[stack.back()] -= static_cast<f64>(z.end_ns - z.start_ns) * 1e-6;
            stack.push_back(i);
        }

        for (auto& s : self)
            s = std::max(s, 0.0);
        return self;
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    tracing_window::tracing_window() {

        make_window_name("Tracing");

        const std::string path = default_tracy_path();
        std::snprintf(m_tracy_path, sizeof(m_tracy_path), "%s", path.c_str());
        GLT::debug::trace::set_capture(m_record);
    }


    tracing_window::~tracing_window() { GLT::debug::trace::set_capture(false); }

    // CLASS PUBLIC ====================================================================================================

    void tracing_window::update(const f32 /*delta_time*/) {

        if (GLT::debug::trace::is_paused())
            return;

        m_frames = GLT::debug::trace::history();
        m_thread_names = GLT::debug::trace::thread_names();
        m_system_sections = GLT::debug::collect_systems();

        m_main_thread = 0;
        for (u32 i = 0; i < m_thread_names.size(); ++i)
            if (m_thread_names[i] == "Main") {
                m_main_thread = i;
                break;
            }

        rebuild_summaries();
        m_zone_stats_dirty = true;
        if (m_follow_latest)
            m_selected = m_frames.empty() ? -1 : static_cast<i32>(m_frames.size()) - 1;
    }


    void tracing_window::window(const f32 /*delta_time*/) {

        if (!m_show_window)
            return;

        apply_pending_dock();
        ImGui::SetNextWindowSizeConstraints(ImVec2(640.0f, 360.0f),
            ImVec2(std::numeric_limits<f32>::max(), std::numeric_limits<f32>::max()));

        if (ImGui::Begin(m_window_id.c_str(), &m_show_window)) {

            draw_tracy_bar();
            ImGui::Separator();
            draw_frame_history();
            ImGui::Separator();

            if (ImGui::BeginTabBar("##tracing_tabs")) {

                if (ImGui::BeginTabItem("Timeline")) {
                    draw_timeline();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Zones")) {
                    draw_zone_table();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("GPU passes")) {
                    draw_gpu_passes();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }


    bool tracing_window::serialize(const std::filesystem::path& /*project_file*/, const GLT::serializer::option /*option*/) { return false; }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    // ---- Tracy status / controls ------------------------------------------------------------------------------------

    void tracing_window::draw_tracy_bar() {

        using namespace GLT::debug;

        if (trace::tracy_compiled()) {

            const bool connected = trace::tracy_connected();
            ImGui::TextUnformatted("Tracy");
            ImGui::SameLine();
            ImGui::TextDisabled("v%s", trace::tracy_version());
            ImGui::SameLine();
            ImGui::TextColored(connected ? ImVec4(0.4f, 0.9f, 0.4f, 1.f) : ImVec4(0.8f, 0.8f, 0.4f, 1.f),
                connected ? "connected" : "waiting for profiler");

            ImGui::SameLine();
            if (ImGui::Button("Launch Tracy"))
                launch_tracy();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Starts the Tracy profiler and connects it to this process (127.0.0.1).\n"
                    "The profiler must be the same version as the client (v%s).\n"
                    "Build it from vendor/tracy:  cmake -B profiler/build -S profiler -DCMAKE_BUILD_TYPE=Release\n"
                    "                             cmake --build profiler/build -j", trace::tracy_version());

            ImGui::SameLine();
            ImGui::SetNextItemWidth(std::max(160.f, ImGui::GetContentRegionAvail().x * 0.35f));
            ImGui::InputText("##tracy_path", m_tracy_path, sizeof(m_tracy_path));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Tracy profiler executable (name in PATH or full path)");

        } else {
            ImGui::TextDisabled("Tracy not compiled in (configure with -DGLT_ENABLE_TRACY=ON). In-engine recording only.");
        }

        ImGui::SameLine();
        if (ImGui::Checkbox("Record", &m_record))
            trace::set_capture(m_record);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Record zones into the in-editor history (Tracy collects independently while connected)");

        ImGui::SameLine();
        bool paused = trace::is_paused();
        if (ImGui::Button(paused ? "Resume" : "Pause"))
            trace::set_paused(!paused);

        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            trace::clear_history();
            m_frames.clear();
            m_summaries.clear();
            m_zone_stats.clear();
            m_selected = -1;
        }

        // ---- capture + export (for sharing exact timings) ----
        ImGui::SetNextItemWidth(90.f);
        ImGui::InputInt("##capture_frames", &m_capture_frames, 0, 0);
        m_capture_frames = std::clamp(m_capture_frames, 1, static_cast<int>(trace::HISTORY_FRAMES));
        ImGui::SameLine();
        if (ImGui::Button("Capture frames")) {
            m_record = true;
            m_follow_latest = true;
            trace::capture_frames(static_cast<u32>(m_capture_frames));
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Clears the history, records exactly this many frames, then pauses");
        if (const u32 remaining = trace::capture_frames_remaining(); remaining > 0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.f), "capturing... %u left", remaining);
        }

        ImGui::SameLine();
        if (ImGui::Button("Export")) {
            const auto dir = GLT::util::get_executable_path() / "traces";
            const auto path = trace::export_to_directory(trace::history(), dir);
            m_export_status = path.empty() ? "Export failed (" + dir.string() + ")"
                : "Exported " + path.string() + " (+ .json for ui.perfetto.dev / chrome://tracing)";
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Writes <exe dir>/traces/trace_<time>_summary.txt (analysis: hottest zones, call tree,\n"
                "slowest frame) and trace_<time>.json (every zone, Chrome trace format)");

        ImGui::SameLine();
        if (ImGui::Button("Copy summary")) {
            const std::string summary = trace::summary_text(trace::history());
            ImGui::SetClipboardText(summary.c_str());
            m_export_status = "Summary copied to the clipboard (" + std::to_string(summary.size() / 1024) + " KB)";
        }

        if (!m_launch_status.empty())
            ImGui::TextDisabled("%s", m_launch_status.c_str());
        if (!m_export_status.empty())
            ImGui::TextDisabled("%s", m_export_status.c_str());
    }

    // ---- frame history ----------------------------------------------------------------------------------------------

    void tracing_window::draw_frame_history() {

        const auto app = GLT::debug::get_application_stats();
        ImGui::Text("last frame  %.2f ms   cpu work %.2f   cpu wait %.2f   limiter %.2f   gpu %.2f ms",
            app.frame_time_ms, app.cpu_time_ms, app.cpu_wait_ms, app.limiter_ms, app.render.gpu_time_ms);

        if (!m_summaries.empty()) {
            f32 work = 0.f, wait = 0.f, total = 0.f, worst = 0.f;
            for (const auto& s : m_summaries) {
                work += s.work_ms;
                wait += s.wait_ms;
                total += s.total_ms;
                worst = std::max(worst, s.total_ms);
            }
            const f32 n = static_cast<f32>(m_summaries.size());
            ImGui::TextDisabled("%zu frames   avg %.2f ms (work %.2f, wait %.2f)   worst %.2f ms   %s",
                m_summaries.size(), total / n, work / n, wait / n, worst,
                wait > work ? "-> mostly waiting: GPU-bound or vsync/limiter-capped" : "-> CPU-bound");
        }

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const f32 width = std::max(ImGui::GetContentRegionAvail().x, 50.f);
        const ImVec2 size(width, HISTORY_HEIGHT);
        ImGui::InvisibleButton("##frame_history", size);
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg));

        if (m_summaries.empty()) {
            dl->AddText(ImVec2(origin.x + 8.f, origin.y + 8.f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                m_record ? "Recording... (frames appear here)" : "Recording is off");
            return;
        }

        const auto budget = GLT::application::get().get_fps_controller().get_target_interval_duration();
        const f32 budget_ms = static_cast<f32>(std::chrono::duration<f64, std::milli>(budget).count());
        f32 y_max = budget_ms * 1.5f;
        for (const auto& s : m_summaries)
            y_max = std::max(y_max, s.total_ms);

        const u32 count = static_cast<u32>(m_summaries.size());
        const f32 bar_w = width / static_cast<f32>(GLT::debug::trace::HISTORY_FRAMES);
        const f32 x0 = origin.x + width - bar_w * static_cast<f32>(count);     // newest frame at the right edge
        auto y_of = [&](f32 ms) { return origin.y + size.y - (ms / y_max) * size.y; };

        for (u32 i = 0; i < count; ++i) {
            const auto& s = m_summaries[i];
            const f32 l = x0 + bar_w * static_cast<f32>(i);
            const f32 r = std::max(l + bar_w - 1.f, l + 1.f);
            f32 base = 0.f;
            auto band = [&](f32 ms, ImU32 color) {
                if (ms <= 0.f) return;
                dl->AddRectFilled(ImVec2(l, y_of(base + ms)), ImVec2(r, y_of(base)), color);
                base += ms;
            };
            band(s.work_ms, COLOR_WORK);
            band(s.wait_ms, COLOR_WAIT);
            band(s.idle_ms, COLOR_IDLE);
            if (static_cast<i32>(i) == m_selected)
                dl->AddRectFilled(ImVec2(l, origin.y), ImVec2(r, origin.y + size.y), COLOR_SELECTED);
        }

        const f32 budget_y = y_of(budget_ms);
        dl->AddLine(ImVec2(origin.x, budget_y), ImVec2(origin.x + width, budget_y), COLOR_BUDGET);

        if (hovered) {
            const f32 mx = ImGui::GetIO().MousePos.x;
            const i32 index = static_cast<i32>(std::floor((mx - x0) / bar_w));
            if (index >= 0 && index < static_cast<i32>(count)) {
                const auto& s = m_summaries[static_cast<u32>(index)];
                ImGui::SetTooltip("frame %llu\n%.2f ms total\nwork %.2f  wait %.2f  limiter %.2f\nclick to inspect",
                    static_cast<unsigned long long>(m_frames[static_cast<u32>(index)].index), s.total_ms, s.work_ms, s.wait_ms, s.idle_ms);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    m_selected = index;
                    m_follow_latest = false;
                    m_zone_stats_dirty = true;
                }
            }
        }

        ImGui::Checkbox("Follow latest frame", &m_follow_latest);
        ImGui::SameLine();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COLOR_WORK), "work");
        ImGui::SameLine();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COLOR_WAIT), "wait (GPU / swapchain / vsync)");
        ImGui::SameLine();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COLOR_IDLE), "frame limiter");
        ImGui::SameLine();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COLOR_BUDGET), "budget %.1f ms", budget_ms);
    }

    // ---- timeline -----------------------------------------------------------------------------------------------------

    void tracing_window::draw_timeline() {

        const auto* frame = selected_frame();
        if (!frame) {
            ImGui::TextDisabled("No frame recorded yet");
            return;
        }

        ImGui::Text("frame %llu  -  %.3f ms", static_cast<unsigned long long>(frame->index), frame->duration_ms());
        ImGui::SameLine();
        ImGui::TextDisabled("(ctrl + wheel: zoom, drag: pan, double click: reset)");

        // lanes: one per thread that has zones in this frame, height = deepest nesting
        std::vector<u32> threads;
        std::vector<u16> depth;
        for (const auto& z : frame->zones) {
            auto it = std::find(threads.begin(), threads.end(), z.thread);
            if (it == threads.end()) {
                threads.push_back(z.thread);
                depth.push_back(z.depth);
            } else {
                auto& d = depth[static_cast<size_t>(it - threads.begin())];
                d = std::max(d, z.depth);
            }
        }
        // main thread first
        for (size_t i = 0; i < threads.size(); ++i)
            if (threads[i] == m_main_thread && i != 0) {
                std::swap(threads[i], threads[0]);
                std::swap(depth[i], depth[0]);
            }

        ImGui::BeginChild("##timeline", ImVec2(0.f, 0.f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const f32 label_w = 140.f;
        const f32 width = std::max(ImGui::GetContentRegionAvail().x - label_w, 50.f);
        f32 total_h = 0.f;
        for (const u16 d : depth)
            total_h += (static_cast<f32>(d) + 1.f) * LANE_ROW_HEIGHT + 6.f;

        ImGui::InvisibleButton("##timeline_area", ImVec2(label_w + width, std::max(total_h, 40.f)));
        const bool hovered = ImGui::IsItemHovered();
        ImGuiIO& io = ImGui::GetIO();

        if (hovered) {
            if (io.KeyCtrl && io.MouseWheel != 0.f) {
                const f32 mouse_t = (io.MousePos.x - origin.x - label_w) / (width * m_timeline_zoom) + m_timeline_pan;
                m_timeline_zoom = std::clamp(m_timeline_zoom * (io.MouseWheel > 0.f ? 1.25f : 0.8f), 1.f, 2000.f);
                m_timeline_pan = mouse_t - (io.MousePos.x - origin.x - label_w) / (width * m_timeline_zoom);
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                m_timeline_pan -= io.MouseDelta.x / (width * m_timeline_zoom);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                m_timeline_zoom = 1.f;
                m_timeline_pan = 0.f;
            }
        }
        m_timeline_pan = std::clamp(m_timeline_pan, 0.f, std::max(0.f, 1.f - 1.f / m_timeline_zoom));

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const f64 frame_ns = static_cast<f64>(std::max<u64>(frame->end_ns - frame->start_ns, 1));
        auto x_of = [&](u64 t) {
            const f64 rel = (static_cast<f64>(t) - static_cast<f64>(frame->start_ns)) / frame_ns;
            return origin.x + label_w + static_cast<f32>((rel - m_timeline_pan) * width * m_timeline_zoom);
        };
        const f32 clip_l = origin.x + label_w;
        const f32 clip_r = origin.x + label_w + width;
        dl->PushClipRect(ImVec2(clip_l, origin.y), ImVec2(clip_r, origin.y + total_h), true);

        // millisecond grid
        const f32 frame_ms = std::max(frame->duration_ms(), 1e-3f);
        const f32 visible_ms = frame_ms / m_timeline_zoom;
        const f32 raw_step = visible_ms / 8.f;
        const f32 decade = std::pow(10.f, std::floor(std::log10(raw_step)));
        const f32 step = raw_step / decade < 2.f ? decade * 2.f : raw_step / decade < 5.f ? decade * 5.f : decade * 10.f;
        const f32 first_ms = std::floor(m_timeline_pan * frame_ms / step) * step;
        for (f32 ms = first_ms; ms <= std::min(frame_ms, first_ms + visible_ms + step); ms += step) {
            const f32 x = x_of(frame->start_ns + static_cast<u64>(ms * 1e6f));
            dl->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + total_h), IM_COL32(255, 255, 255, 18));
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%.2f", ms);
            dl->AddText(ImVec2(x + 2.f, origin.y), IM_COL32(255, 255, 255, 70), buf);
        }

        const GLT::debug::trace::zone_record* hovered_zone = nullptr;
        f32 lane_y = origin.y;
        for (size_t lane = 0; lane < threads.size(); ++lane) {

            const u32 thread = threads[lane];
            const f32 lane_h = (static_cast<f32>(depth[lane]) + 1.f) * LANE_ROW_HEIGHT;

            for (const auto& z : frame->zones) {
                if (z.thread != thread)
                    continue;
                const f32 l = std::max(x_of(z.start_ns), clip_l - 2.f);
                const f32 r = std::min(x_of(z.end_ns), clip_r + 2.f);
                if (r < clip_l || l > clip_r)
                    continue;
                const f32 top = lane_y + static_cast<f32>(z.depth) * LANE_ROW_HEIGHT + 12.f;
                const ImVec2 a(l, top), b(std::max(r, l + 1.f), top + LANE_ROW_HEIGHT - 2.f);
                const std::string_view name = z.name ? z.name : "?";
                dl->AddRectFilled(a, b, zone_color(name, z.kind), 2.f);

                const f32 ms = to_ms(z.end_ns - z.start_ns);
                if (b.x - a.x > 30.f) {
                    char label[160];
                    std::snprintf(label, sizeof(label), "%.*s %.2f", static_cast<int>(name.size()), name.data(), ms);
                    dl->PushClipRect(a, b, true);
                    dl->AddText(ImVec2(a.x + 3.f, a.y + 2.f), IM_COL32(20, 20, 20, 255), label);
                    dl->PopClipRect();
                }
                if (hovered && ImGui::IsMouseHoveringRect(a, b))
                    hovered_zone = &z;
            }
            lane_y += lane_h + 6.f;
        }
        dl->PopClipRect();

        // lane labels (outside the clip rect)
        lane_y = origin.y;
        for (size_t lane = 0; lane < threads.size(); ++lane) {
            const u32 thread = threads[lane];
            const std::string& name = thread < m_thread_names.size() ? m_thread_names[thread] : std::string("?");
            dl->AddText(ImVec2(origin.x + 4.f, lane_y + 14.f), ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
            lane_y += (static_cast<f32>(depth[lane]) + 1.f) * LANE_ROW_HEIGHT + 6.f;
            dl->AddLine(ImVec2(origin.x, lane_y - 3.f), ImVec2(clip_r, lane_y - 3.f), IM_COL32(255, 255, 255, 25));
        }

        if (hovered_zone) {
            const char* kind = hovered_zone->kind == GLT::debug::trace::zone_kind::wait ? "wait"
                : hovered_zone->kind == GLT::debug::trace::zone_kind::idle ? "idle" : "work";
            ImGui::SetTooltip("%s\n%.3f ms (%s)\nstarts at %.3f ms", hovered_zone->name ? hovered_zone->name : "?",
                to_ms(hovered_zone->end_ns - hovered_zone->start_ns), kind,
                (static_cast<f64>(hovered_zone->start_ns) - static_cast<f64>(frame->start_ns)) * 1e-6);
        }

        ImGui::EndChild();
    }

    // ---- zone table -------------------------------------------------------------------------------------------------

    void tracing_window::draw_zone_table() {

        ImGui::SetNextItemWidth(160.f);
        int frames = static_cast<int>(m_zone_frames);
        if (ImGui::SliderInt("frames aggregated", &frames, 1, static_cast<int>(GLT::debug::trace::HISTORY_FRAMES))) {
            m_zone_frames = static_cast<u32>(frames);
            m_zone_stats_dirty = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(ending at the selected frame; times are per frame)");

        if (m_zone_stats_dirty)
            rebuild_zone_stats();

        constexpr ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
            | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

        if (!ImGui::BeginTable("##zones", 6, flags))
            return;

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("zone", ImGuiTableColumnFlags_WidthStretch, 3.f);
        ImGui::TableSetupColumn("self ms/frame", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending, 1.f);
        ImGui::TableSetupColumn("incl. ms/frame", ImGuiTableColumnFlags_PreferSortDescending, 1.f);
        ImGui::TableSetupColumn("max ms", ImGuiTableColumnFlags_PreferSortDescending, 1.f);
        ImGui::TableSetupColumn("calls/frame", ImGuiTableColumnFlags_PreferSortDescending, 1.f);
        ImGui::TableSetupColumn("% frame", ImGuiTableColumnFlags_PreferSortDescending, 1.f);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* sort = ImGui::TableGetSortSpecs(); sort && sort->SpecsCount > 0) {
            const auto& spec = sort->Specs[0];
            const bool asc = spec.SortDirection == ImGuiSortDirection_Ascending;
            std::sort(m_zone_stats.begin(), m_zone_stats.end(), [&](const zone_stats& a, const zone_stats& b) {
                auto key = [&](const zone_stats& z) -> f64 {
                    switch (spec.ColumnIndex) {
                        case 2:  return z.inclusive_ms;
                        case 3:  return z.max_ms;
                        case 4:  return static_cast<f64>(z.calls);
                        case 5:  return z.inclusive_ms;
                        default: return z.self_ms;
                    }
                };
                if (spec.ColumnIndex == 0)
                    return asc ? a.name < b.name : a.name > b.name;
                return asc ? key(a) < key(b) : key(a) > key(b);
            });
        }

        // average frame time over the aggregated frames
        f64 frame_ms = 0.0;
        u32 frame_count = 0;
        const i32 last = m_selected >= 0 ? m_selected : static_cast<i32>(m_frames.size()) - 1;
        for (i32 i = last; i >= 0 && frame_count < m_zone_frames; --i, ++frame_count)
            frame_ms += m_frames[static_cast<u32>(i)].duration_ms();
        const f64 frames_f = std::max<f64>(frame_count, 1.0);
        frame_ms /= frames_f;

        for (const auto& z : m_zone_stats) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (z.wait)
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COLOR_WAIT), "%.*s", static_cast<int>(z.name.size()), z.name.data());
            else
                ImGui::Text("%.*s", static_cast<int>(z.name.size()), z.name.data());
            ImGui::TableNextColumn(); ImGui::Text("%.3f", z.self_ms / frames_f);
            ImGui::TableNextColumn(); ImGui::Text("%.3f", z.inclusive_ms / frames_f);
            ImGui::TableNextColumn(); ImGui::Text("%.3f", z.max_ms);
            ImGui::TableNextColumn(); ImGui::Text("%.1f", static_cast<f64>(z.calls) / frames_f);
            ImGui::TableNextColumn(); ImGui::Text("%.1f%%", frame_ms > 0.0 ? 100.0 * (z.inclusive_ms / frames_f) / frame_ms : 0.0);
        }

        ImGui::EndTable();
    }

    // ---- GPU passes (renderer stats provider) -------------------------------------------------------------------------

    void tracing_window::draw_gpu_passes() {

        bool any = false;
        for (const auto& [name, section] : m_system_sections) {

            if (name.find("GPU") == std::string::npos)
                continue;
            any = true;
            ImGui::SeparatorText(name.c_str());

            f32 max_value = 0.f;
            for (const auto& v : section.custom)
                max_value = std::max(max_value, v.value);

            if (ImGui::BeginTable(name.c_str(), 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                for (const auto& v : section.custom) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(v.name.c_str());
                    ImGui::TableNextColumn();
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), v.format ? v.format : "%.3f", v.value);
                    ImGui::ProgressBar(max_value > 0.f ? v.value / max_value : 0.f, ImVec2(-1.f, 0.f), buf);
                }
                ImGui::EndTable();
            }
        }

        if (!any)
            ImGui::TextDisabled("The renderer reports no GPU pass timings");
        ImGui::Spacing();
        ImGui::TextDisabled("Per-pass GPU zones are also sent to Tracy (one GPU context per scene renderer).");
    }

    // ---- helpers ------------------------------------------------------------------------------------------------------

    void tracing_window::rebuild_summaries() {

        m_summaries.clear();
        m_summaries.reserve(m_frames.size());
        for (const auto& f : m_frames) {
            frame_summary s{};
            s.total_ms = f.duration_ms();
            s.wait_ms = f.wait_ms();
            for (const auto& z : f.zones)
                if (z.kind == GLT::debug::trace::zone_kind::idle && z.thread == m_main_thread && z.depth == 0)
                    s.idle_ms += to_ms(z.end_ns - z.start_ns);
            s.work_ms = std::max(s.total_ms - s.wait_ms - s.idle_ms, 0.f);
            m_summaries.push_back(s);
        }
    }


    void tracing_window::rebuild_zone_stats() {

        m_zone_stats_dirty = false;
        m_zone_stats.clear();
        if (m_frames.empty())
            return;

        std::unordered_map<std::string_view, size_t> index;
        const i32 last = m_selected >= 0 ? m_selected : static_cast<i32>(m_frames.size()) - 1;
        u32 aggregated = 0;

        for (i32 fi = last; fi >= 0 && aggregated < m_zone_frames; --fi, ++aggregated) {

            const auto& frame = m_frames[static_cast<u32>(fi)];
            const auto self = self_times(frame);
            std::unordered_set<size_t> seen;

            for (size_t i = 0; i < frame.zones.size(); ++i) {
                const auto& z = frame.zones[i];
                const std::string_view name = z.name ? z.name : "?";
                auto [it, inserted] = index.try_emplace(name, m_zone_stats.size());
                if (inserted)
                    m_zone_stats.push_back({ .name = name });
                auto& st = m_zone_stats[it->second];
                const f64 ms = static_cast<f64>(z.end_ns - z.start_ns) * 1e-6;
                st.calls++;
                st.inclusive_ms += ms;
                st.self_ms += self[i];
                st.max_ms = std::max(st.max_ms, ms);
                st.wait = st.wait || z.kind == GLT::debug::trace::zone_kind::wait;
                if (seen.insert(it->second).second)
                    st.frames++;
            }
        }
    }


    // Spawns the Tracy profiler detached (via /bin/sh ... &) and points it at this process
    void tracing_window::launch_tracy() {

#if defined(PLATFORM_LINUX)
        std::string path = m_tracy_path;
        if (path.empty())
            path = default_tracy_path();
        const std::string resolved = find_in_path(path);
        if (resolved.empty()) {
            m_launch_status = "Tracy profiler not found: '" + path + "'. Build it from vendor/tracy/profiler or install Tracy v"
                + GLT::debug::trace::tracy_version();
            return;
        }

        std::string quoted = "'";
        for (char c : resolved) {
            if (c == '\'') quoted += "'\\''";
            else quoted += c;
        }
        quoted += "'";
        const std::string command = "exec " + quoted + " -a 127.0.0.1 >/dev/null 2>&1 &";

        const char* argv[] = { "/bin/sh", "-c", command.c_str(), nullptr };
        pid_t pid = 0;
        const int rc = posix_spawn(&pid, "/bin/sh", nullptr, nullptr, const_cast<char* const*>(argv), environ);
        if (rc != 0) {
            m_launch_status = "Failed to start Tracy (" + std::string(std::strerror(rc)) + ")";
            return;
        }
        int status = 0;
        waitpid(pid, &status, 0);                       // the shell returns immediately (profiler runs in the background)
        m_launch_status = "Started " + resolved + " (connects to 127.0.0.1)";
#else
        m_launch_status = "Launching Tracy is only implemented on Linux - start the profiler manually and connect to this machine";
#endif
    }


    const GLT::debug::trace::frame_record* tracing_window::selected_frame() const {

        if (m_frames.empty())
            return nullptr;
        const i32 index = (m_selected >= 0 && m_selected < static_cast<i32>(m_frames.size())) ? m_selected
            : static_cast<i32>(m_frames.size()) - 1;
        return &m_frames[static_cast<u32>(index)];
    }

}
