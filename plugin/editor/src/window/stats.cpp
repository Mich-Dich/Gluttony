
#include "util/pch.h"
#include "stats.h"

#include <imgui.h>
#include <implot.h>

#include <debug/profiler.h>

#include "util/ui/pannel_collection.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    constexpr f64                        MB = 1024.0 * 1024.0;

    constexpr f32                           GRAPH_HEIGHT = 180.0f;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    void draw_stat_row(const char* label, const char* fmt, ...);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    void draw_stat_row(const char* label, const char* fmt, ...) {

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);

        va_list args;
        va_start(args, fmt);
        ImGui::TextV(fmt, args);
        va_end(args);
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    stats_window::stats_window() {

        make_window_name("Debug Statistics");

        m_time_axis.reserve(HISTORY_SIZE);
        m_frame_time_history.reserve(HISTORY_SIZE);
        m_gpu_time_history.reserve(HISTORY_SIZE);
    }


    stats_window::~stats_window() { }

    // CLASS PUBLIC ====================================================================================================

    void stats_window::window(const f32 /*delta_time*/) {

        if (!m_show_window)
            return;

        apply_pending_dock();
        ImGui::SetNextWindowSizeConstraints(ImVec2(320.0f, 260.0f),
            ImVec2(std::numeric_limits<f32>::max(), std::numeric_limits<f32>::max()));

        if (ImGui::Begin(m_window_id.c_str(), &m_show_window)) {

            if (ImGui::Button(m_paused ? "Resume" : "Pause"))
                m_paused = !m_paused;

            ImGui::SameLine();
            if (ImGui::Button("Reset history"))
                clear_history();

            ImGui::SameLine();
            ImGui::Checkbox("GPU", &m_show_gpu);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Show GPU frame time on the graph");

            ImGui::SameLine();
            ImGui::Checkbox("Target", &m_show_target_line);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Show a horizontal reference line at %.1f ms (60 FPS)", m_target_frame_ms);

            ImGui::Separator();

            draw_core_section();
            draw_rendering_section();
            draw_memory_section();
            draw_resources_section();
            draw_system_sections();
        }

        ImGui::End();
    }


    void stats_window::update(const f32 /*delta_time*/) {

        if (m_paused)
            return;

        // Snapshot the app stats so the pause button freezes a coherent view.
        m_app_snapshot = GLT::debug::get_application_stats();
        push_history_sample(m_app_snapshot);
        recompute_window_stats();

        // Collect every registered system's custom values.
        m_system_sections = GLT::debug::collect_systems();
    }


    bool stats_window::serialize(const std::filesystem::path& /*project_file*/, const GLT::serializer::option /*option*/) { return false; }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void stats_window::draw_core_section() {

        if (!ImGui::CollapsingHeader("Frame", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        if (UI::begin_table("stats_core", false)) {

            draw_stat_row("frame time", "%.2f ms", m_app_snapshot.frame_time_ms);
            draw_stat_row("fps", "%.1f", m_app_snapshot.fps);

            if (m_gpu_available && m_show_gpu)
                draw_stat_row("gpu time", "%.2f ms", m_app_snapshot.render.gpu_time_ms);

            if (m_app_snapshot.cpu_time_ms > 0.0f)
                draw_stat_row("cpu time", "%.2f ms", m_app_snapshot.cpu_time_ms);

            UI::end_table();
        }

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        draw_frame_graph();

        // Summary line under the graph. Uses the same window the graph shows
        // so the numbers always agree with what's visually on screen.
        if (!m_frame_time_history.empty()) {

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.65f, 1.0f));
            ImGui::Text("frame  min %.2f  avg %.2f  max %.2f ms", m_frame_stats.min, m_frame_stats.avg, m_frame_stats.max);
            ImGui::PopStyleColor();

            if (m_gpu_available && m_show_gpu) {

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.65f, 1.0f));
                ImGui::Text("gpu    min %.2f  avg %.2f  max %.2f ms", m_gpu_stats.min, m_gpu_stats.avg, m_gpu_stats.max);
                ImGui::PopStyleColor();
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }


    void stats_window::draw_frame_graph() {

        if (m_time_axis.empty())
            return;

        const int count = static_cast<int>(m_time_axis.size());

        // Auto-fit Y, but never below the target line so the reference
        // stays visible even on a machine that's comfortably above 60 fps.
        f32 y_max = m_show_target_line ? m_target_frame_ms * 1.25f : 0.0f;
        y_max = std::max(y_max, m_frame_stats.max);
        if (m_gpu_available && m_show_gpu)
            y_max = std::max(y_max, m_gpu_stats.max);

        y_max = std::max(y_max, 1.0f);      // never collapse to zero height

        const f32 x_min = m_time_axis.front();
        const f32 x_max = m_time_axis.back();

        if (ImPlot::BeginPlot("##frame_time_plot", ImVec2(-1.0f, GRAPH_HEIGHT), ImPlotFlags_NoMenus | ImPlotFlags_NoMouseText)) {

            ImPlot::SetupAxes("time (s)", "ms", ImPlotAxisFlags_NoHighlight, ImPlotAxisFlags_NoHighlight);
            ImPlot::SetupAxesLimits(x_min, std::max(x_max, x_min + 0.001f), 0.0, static_cast<f64>(y_max), ImPlotCond_Always);
            ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal);

            // Target line first so it renders under the data lines.
            if (m_show_target_line) {

                const f64 target = static_cast<f64>(m_target_frame_ms);
                ImPlot::PlotInfLines("60 FPS", &target, 1, ImPlotSpec(ImPlotProp_LineColor, ImVec4(1.0f, 0.45f, 0.45f, 0.65f), 
                    ImPlotProp_LineWeight, 1.0f, ImPlotProp_Flags, ImPlotInfLinesFlags_Horizontal));
            }

            // Frame time.
            ImPlot::PlotLine("Frame", m_time_axis.data(), m_frame_time_history.data(), count,
                ImPlotSpec(ImPlotProp_LineColor,  ImVec4(0.40f, 0.85f, 0.40f, 1.0f), ImPlotProp_LineWeight, 1.5f));

            // GPU time (only if the renderer has produced any).
            if (m_gpu_available && m_show_gpu) {

                ImPlot::PlotLine("GPU", m_time_axis.data(), m_gpu_time_history.data(), count,
                    ImPlotSpec(ImPlotProp_LineColor,  ImVec4(0.40f, 0.60f, 1.00f, 1.0f), ImPlotProp_LineWeight, 1.5f));
            }

            ImPlot::EndPlot();
        }
    }


    void stats_window::draw_rendering_section() {

        if (!ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        if (UI::begin_table("stats_rendering", false)) {

            draw_stat_row("draw calls", "%u", m_app_snapshot.render.draw_calls);
            draw_stat_row("triangles",  "%u", m_app_snapshot.render.triangles);
            draw_stat_row("vertices",   "%u", m_app_snapshot.render.vertices);
            draw_stat_row("passes",     "%u", m_app_snapshot.render.render_passes);
            UI::end_table();
        }
    }


    void stats_window::draw_memory_section() {

        if (!ImGui::CollapsingHeader("Memory", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        if (UI::begin_table("stats_memory", false)) {

            draw_stat_row("vram", "%.2f MB", m_app_snapshot.render.vram_bytes / MB);
            draw_stat_row("ram",  "%.2f MB", m_app_snapshot.render.ram_bytes  / MB);
            UI::end_table();
        }
    }


    void stats_window::draw_resources_section() {

        if (!ImGui::CollapsingHeader("Resources", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        if (UI::begin_table("stats_resources", false)) {

            draw_stat_row("textures",        "%u", m_app_snapshot.render.texture_count);
            draw_stat_row("buffers",         "%u", m_app_snapshot.render.buffer_count);
            draw_stat_row("descriptor sets", "%u", m_app_snapshot.render.descriptor_set_count);
            draw_stat_row("pipelines",       "%u", m_app_snapshot.render.pipeline_count);
            UI::end_table();
        }
    }


    void stats_window::draw_system_sections() {

        for (const auto& [name, stats] : m_system_sections) {

            if (stats.custom.empty())
                continue;

            // System section headers use a distinct style so the user can
            // tell at a glance which sections come from plugins vs. the
            // fixed built-in set.
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.80f, 0.55f, 1.0f));
            const bool open = ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::PopStyleColor();

            if (!open)
                continue;

            const std::string table_id = "stats_sys_" + name;
            if (UI::begin_table(table_id, false)) {

                for (const auto& v : stats.custom)
                    draw_stat_row(v.name.c_str(), v.format, v.value);

                UI::end_table();
            }
        }
    }


    void stats_window::push_history_sample(const GLT::debug::application_stats& s) {

        m_elapsed_time_s += ImGui::GetIO().DeltaTime;

        m_time_axis.push_back(m_elapsed_time_s);
        m_frame_time_history.push_back(s.frame_time_ms);
        m_gpu_time_history.push_back(s.render.gpu_time_ms);

        if (m_time_axis.size() > HISTORY_SIZE) {

            m_time_axis.erase(m_time_axis.begin());
            m_frame_time_history.erase(m_frame_time_history.begin());
            m_gpu_time_history.erase(m_gpu_time_history.begin());
        }

        // Latch on once the renderer starts contributing GPU timings; we
        // don't want a transient zero to hide the line forever.
        if (!m_gpu_available && s.render.gpu_time_ms > 0.0f)
            m_gpu_available = true;
    }


    void stats_window::clear_history() {

        m_time_axis.clear();
        m_frame_time_history.clear();
        m_gpu_time_history.clear();
        m_elapsed_time_s = 0.0f;
        m_frame_stats = {};
        m_gpu_stats   = {};
    }


    void stats_window::recompute_window_stats() {

        auto compute = [](const std::vector<f32>& v) -> window_stats {

            window_stats out{};
            if (v.empty())
                return out;

            out.min = std::numeric_limits<f32>::max();
            out.max = std::numeric_limits<f32>::lowest();
            f64 sum = 0.0;

            for (const f32 x : v) {
                out.min = std::min(out.min, x);
                out.max = std::max(out.max, x);
                sum += static_cast<f64>(x);
            }

            out.avg = static_cast<f32>(sum / static_cast<f64>(v.size()));
            return out;
        };

        m_frame_stats = compute(m_frame_time_history);
        m_gpu_stats   = compute(m_gpu_time_history);
    }

}
