#include "util/pch.h"
#include "stats.h"

#include <imgui.h>
#include <implot.h>

#include <debug/profiler.h>

#include <config/imgui_config.h>
#include "util/ui/pannel_collection.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    constexpr f64                               MB = 1024.0 * 1024.0;

    constexpr const char*                       STATS_TABLE_NAME = "STATS_TABLE_NAME";

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    stats_window::stats_window() {

        make_window_name("Debug Statistics");

        m_time_axis.reserve(HISTORY_SIZE);
        m_frame_time_history.reserve(HISTORY_SIZE);
        m_gpu_time_history.reserve(HISTORY_SIZE);
        m_cpu_time_history.reserve(HISTORY_SIZE);
        m_fps_history.reserve(HISTORY_SIZE);
        m_draw_calls_history.reserve(HISTORY_SIZE);
        m_triangles_history.reserve(HISTORY_SIZE);
        m_vertices_history.reserve(HISTORY_SIZE);
        m_vram_history.reserve(HISTORY_SIZE);
        m_ram_history.reserve(HISTORY_SIZE);
    }


    stats_window::~stats_window() { }

    // CLASS PUBLIC ====================================================================================================

    void stats_window::window(const f32 /*delta_time*/) {

        if (!m_show_window)
            return;

        apply_pending_dock();
        ImGui::SetNextWindowSizeConstraints(ImVec2(720.0f, 400.0f),
            ImVec2(std::numeric_limits<f32>::max(), std::numeric_limits<f32>::max()));

        if (ImGui::Begin(m_window_id.c_str(), &m_show_window)) {

            // ---- top toolbar -----------------------------------------------------------------------------------
            if (ImGui::Button(m_paused ? "Resume" : "Pause"))
                m_paused = !m_paused;

            ImGui::SameLine();
            if (ImGui::Button("Reset history"))
                clear_history();

            ImGui::Separator();

            // ---- split layout ----------------------------------------------------------------------------------
            UI::custom_frame(DETAILS_PANEL_WIDTH, true, ImGui::GetColorU32(GLT::imgui_config::get_default_gray1_ref()),
                [this]() {

                    draw_core_section();
                    draw_rendering_section();
                    draw_memory_section();
                    draw_resources_section();
                    draw_system_sections();
                },
                [this]() {

                    if (m_time_axis.empty()) {

                        ImGui::TextDisabled("Collecting samples...");
                        return;
                    }

                    // ---- combined ms plot ---------------------------------------------------------------------------------
                    draw_combined_ms_plot();
                    UI::shift_cursor_pos(ImVec2(0.0f, 8.0f));

                    // ---- individual plots ---------------------------------------------------------------------------------
                    draw_stat_plot("##plot_fps", "FPS", m_fps_history, ImVec4(0.35f, 0.85f, 0.90f, 1.0f), "fps");
                    UI::shift_cursor_pos(ImVec2(0.0f, 6.0f));

                    draw_stat_plot("##plot_drawcalls", "Draw calls", m_draw_calls_history, ImVec4(0.95f, 0.80f, 0.35f, 1.0f), "calls");
                    UI::shift_cursor_pos(ImVec2(0.0f, 6.0f));

                    draw_stat_plot("##plot_triangles", "Triangles", m_triangles_history, ImVec4(0.75f, 0.55f, 0.95f, 1.0f), "tris");
                    UI::shift_cursor_pos(ImVec2(0.0f, 6.0f));

                    draw_stat_plot("##plot_vertices", "Vertices", m_vertices_history, ImVec4(0.95f, 0.55f, 0.80f, 1.0f), "verts");
                    UI::shift_cursor_pos(ImVec2(0.0f, 6.0f));

                    draw_stat_plot("##plot_vram", "VRAM (MB)", m_vram_history, ImVec4(0.40f, 0.70f, 1.00f, 1.0f), "MB");
                    UI::shift_cursor_pos(ImVec2(0.0f, 6.0f));

                    draw_stat_plot("##plot_ram", "RAM (MB)", m_ram_history, ImVec4(0.55f, 0.90f, 0.55f, 1.0f), "MB");
                });
        }

        ImGui::End();
    }


    void stats_window::update(const f32 /*delta_time*/) {

        if (m_paused)
            return;

        m_app_snapshot = GLT::debug::get_application_stats();       // Snapshot the app stats so the pause button freezes a coherent view.
        push_history_sample(m_app_snapshot);
        recompute_window_stats();

        m_system_sections = GLT::debug::collect_systems();          // Collect every registered system's custom values.
    }


    bool stats_window::serialize(const std::filesystem::path& /*project_file*/, const GLT::serializer::option /*option*/) { return false; }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    // sections (left panel) -------------------------------------------------------------------------------------------

    void stats_window::draw_core_section() {

        if (!ImGui::CollapsingHeader("Frame", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        if (UI::begin_table(STATS_TABLE_NAME, false)) {

            UI::table_row_text("frame time", "%.2f ms", m_app_snapshot.frame_time_ms);
            UI::table_row_text("fps", "%.1f",    m_app_snapshot.fps);

            if (m_gpu_available)
                UI::table_row_text("gpu time", "%.2f ms", m_app_snapshot.render.gpu_time_ms);

            if (m_app_snapshot.cpu_time_ms > 0.0f)
                UI::table_row_text("cpu time", "%.2f ms", m_app_snapshot.cpu_time_ms);

            UI::end_table();
        }

        // Summary of the sliding window - mirrors exactly what the plots
        // are showing, so the numbers always agree with the visuals.
        if (!m_frame_time_history.empty()) {

            ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::monospace_regular));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.65f, 1.0f));
            ImGui::Text("frame  min %6.2f  avg %6.2f  max %6.2f ms", m_frame_stats.min, m_frame_stats.avg, m_frame_stats.max);
            ImGui::PopStyleColor();

            if (m_gpu_available) {

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.65f, 1.0f));
                ImGui::Text("gpu    min %6.2f  avg %6.2f  max %6.2f ms", m_gpu_stats.min, m_gpu_stats.avg, m_gpu_stats.max);
                ImGui::PopStyleColor();
            }
            ImGui::PopFont();
        }

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }


    void stats_window::draw_rendering_section() {

        if (!ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        if (UI::begin_table(STATS_TABLE_NAME, false)) {

            UI::table_row_text("draw calls", "%u", m_app_snapshot.render.draw_calls);
            UI::table_row_text("triangles", "%u", m_app_snapshot.render.triangles);
            UI::table_row_text("vertices", "%u", m_app_snapshot.render.vertices);
            UI::table_row_text("passes", "%u", m_app_snapshot.render.render_passes);
            UI::end_table();
        }

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }


    void stats_window::draw_memory_section() {

        if (!ImGui::CollapsingHeader("Memory", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        if (UI::begin_table(STATS_TABLE_NAME, false)) {

            UI::table_row_text("vram", "%.2f MB", m_app_snapshot.render.vram_bytes / MB);
            UI::table_row_text("ram", "%.2f MB", m_app_snapshot.ram_bytes / MB);
            UI::table_row_text("ram peak", "%.2f MB", GLT::util::get_process_peak_ram_bytes() / MB);
            UI::end_table();
        }

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }


    void stats_window::draw_resources_section() {

        if (!ImGui::CollapsingHeader("Resources", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        if (UI::begin_table(STATS_TABLE_NAME, false)) {

            UI::table_row_text("textures", "%u", m_app_snapshot.render.texture_count);
            UI::table_row_text("buffers", "%u", m_app_snapshot.render.buffer_count);
            UI::table_row_text("descriptor sets", "%u", m_app_snapshot.render.descriptor_set_count);
            UI::table_row_text("pipelines", "%u", m_app_snapshot.render.pipeline_count);
            UI::end_table();
        }

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
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

            if (UI::begin_table(STATS_TABLE_NAME, false)) {

                for (const auto& v : stats.custom)
                    UI::table_row_text(v.name.c_str(), v.format, v.value);

                UI::end_table();
            }

            ImGui::Dummy(ImVec2(0.0f, 4.0f));
        }
    }

    // plots (right panel) ---------------------------------------------------------------------------------------------

    void stats_window::draw_combined_ms_plot() {

        if (m_time_axis.empty())
            return;

        const int count = static_cast<int>(m_time_axis.size());
        const auto target_interval_duration = GLT::application::get().get_fps_controller().get_target_interval_duration();
        const f64 target_frame_ms = std::chrono::duration<f64, std::milli>(target_interval_duration).count();

        // ---- Build the cumulative series for the stacked view --------------------
        //
        //   bottom band = CPU  →  fills [0          .. cpu]
        //   top band    = GPU  →  fills [cpu        .. cpu + gpu]
        //
        // The total (cpu + gpu) is also kept as its own array so we can draw a
        // crisp line on top of the stack - the shaded bands alone make the
        // boundary visible, but a line makes the total instantly readable.
        std::vector<f32> zero_line(count, 0.0f);
        std::vector<f32> cpu_plus_gpu(count);
        for (int i = 0; i < count; ++i)
            cpu_plus_gpu[i] = m_cpu_time_history[i] + (m_gpu_available ? m_gpu_time_history[i] : 0.0f);

        // ---- Auto-fit Y ----------------------------------------------------------
        // Must clear: the frame budget (with headroom so the reference line never
        // touches the top edge), the measured wall-clock frame time, and the
        // stacked total.
        f32 y_max = static_cast<f32>(target_frame_ms) * 1.25f;
        for (int i = 0; i < count; ++i) {
            y_max = std::max(y_max, m_frame_time_history[i]);
            y_max = std::max(y_max, cpu_plus_gpu[i]);
        }
        y_max = std::max(y_max, 1.0f);      // never collapse to zero height

        const f32 x_min = m_time_axis.front();
        const f32 x_max = m_time_axis.back();

        if (ImPlot::BeginPlot("##frame_time_plot", ImVec2(-1.0f, GRAPH_HEIGHT + 20.0f), ImPlotFlags_NoMenus | ImPlotFlags_NoMouseText)) {

            ImPlot::SetupAxes("time (s)", "ms", ImPlotAxisFlags_NoHighlight, ImPlotAxisFlags_NoHighlight);
            ImPlot::SetupAxesLimits(x_min, std::max(x_max, x_min + 0.001f), 0.0, static_cast<f64>(y_max), ImPlotCond_Always);
            ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal);

            // ---- Frame budget reference -----------------------------------------
            // Drawn first so the stacked bands and the total line sit on top of it.
            ImPlot::PlotInfLines("Frame budget", &target_frame_ms, 1,
                ImPlotSpec(ImPlotProp_LineColor,  ImVec4(1.0f, 0.45f, 0.45f, 0.65f), 
                    ImPlotProp_LineWeight, 1.0f,
                    ImPlotProp_Flags, ImPlotInfLinesFlags_Horizontal));

            // ---- Stacked CPU + GPU "used" work ----------------------------------
            // PlotShaded(label, xs, ys1, ys2, count, ...) fills between ys1 and ys2.
            ImPlot::PlotShaded("CPU used", m_time_axis.data(), zero_line.data(),    m_cpu_time_history.data(), count,
                ImPlotSpec(ImPlotProp_FillColor, ImVec4(1.00f, 0.65f, 0.40f, 0.55f), ImPlotProp_LineColor, ImVec4(1.00f, 0.65f, 0.40f, 1.00f)));

            if (m_gpu_available) {

                ImPlot::PlotShaded("GPU used", m_time_axis.data(), m_cpu_time_history.data(), cpu_plus_gpu.data(), count,
                    ImPlotSpec(ImPlotProp_FillColor, ImVec4(0.40f, 0.60f, 1.00f, 0.55f), ImPlotProp_LineColor, ImVec4(0.40f, 0.60f, 1.00f, 1.00f)));

                // Crisp top-of-stack line so the total is easy to read even when
                // the two band colours are close in luminance.
                ImPlot::PlotLine("Total work", m_time_axis.data(), cpu_plus_gpu.data(), count, 
                    ImPlotSpec(ImPlotProp_LineColor,  ImVec4(0.85f, 0.85f, 0.95f, 0.9f), ImPlotProp_LineWeight, 1.0f));
            }

            // ---- Measured wall-clock frame time ---------------------------------
            // This is the line the user actually "feels" - when it crosses above
            // the budget line, the frame is over-budget.
            ImPlot::PlotLine("Frame time", m_time_axis.data(), m_frame_time_history.data(), count, 
                ImPlotSpec(ImPlotProp_LineColor, ImVec4(0.40f, 0.85f, 0.40f, 1.0f), ImPlotProp_LineWeight, 1.5f));

            ImPlot::EndPlot();
        }
    }


    void stats_window::draw_stat_plot(const char* id, const char* label, const std::vector<f32>& data,
        const ImVec4& color, const char* y_unit) {

        if (data.empty() || m_time_axis.empty())
            return;

        const int count = static_cast<int>(data.size());

        f32 y_max = 0.0f;
        for (const f32 v : data)
            y_max = std::max(y_max, v);

        y_max = std::max(y_max * 1.15f, 0.001f);
        const f32 x_min = m_time_axis.front();
        const f32 x_max = m_time_axis.back();

        // Section title above the plot so stacked plots read as a list.
        if (UI::begin_collapsing_header_section(label)) {

            if (ImPlot::BeginPlot(id, ImVec2(-1.0f, GRAPH_HEIGHT), ImPlotFlags_NoMenus | ImPlotFlags_NoMouseText)) {

                ImPlot::SetupAxes("time (s)", y_unit, ImPlotAxisFlags_NoHighlight, ImPlotAxisFlags_NoHighlight);
                ImPlot::SetupAxesLimits(x_min, std::max(x_max, x_min + 0.001f), 0.0, static_cast<f64>(y_max), ImPlotCond_Always);
                ImPlot::PlotLine(label, m_time_axis.data(), data.data(), count, ImPlotSpec(ImPlotProp_LineColor,  color, 
                    ImPlotProp_LineWeight, 1.5f));

                ImPlot::EndPlot();
            }
            UI::end_collapsing_header_section();
        }
    }


    // history ---------------------------------------------------------------------------------------------------------

    void stats_window::push_history_sample(const GLT::debug::application_stats& stats) {

        m_elapsed_time_s += ImGui::GetIO().DeltaTime;

        m_time_axis.push_back(m_elapsed_time_s);

        m_frame_time_history.push_back(ImGui::GetIO().DeltaTime * 1000.f);
        m_gpu_time_history.push_back(stats.render.gpu_time_ms);
        m_cpu_time_history.push_back(stats.cpu_time_ms);

        m_fps_history.push_back(stats.fps);
        m_draw_calls_history.push_back(static_cast<f32>(stats.render.draw_calls));
        m_triangles_history.push_back(static_cast<f32>(stats.render.triangles));
        m_vertices_history.push_back(static_cast<f32>(stats.render.vertices));
        m_vram_history.push_back(static_cast<f32>(stats.render.vram_bytes / MB));
        m_ram_history.push_back(static_cast<f32>(stats.ram_bytes / MB));

        if (m_time_axis.size() > HISTORY_SIZE) {

            const auto drop = [](auto& v) { v.erase(v.begin()); };
            drop(m_time_axis);
            drop(m_frame_time_history);
            drop(m_gpu_time_history);
            drop(m_cpu_time_history);
            drop(m_fps_history);
            drop(m_draw_calls_history);
            drop(m_triangles_history);
            drop(m_vertices_history);
            drop(m_vram_history);
            drop(m_ram_history);
        }

        // Latch on once the renderer starts contributing GPU timings; we
        // don't want a transient zero to hide the line forever.
        if (!m_gpu_available && stats.render.gpu_time_ms > 0.0f)
            m_gpu_available = true;
    }


    void stats_window::clear_history() {

        m_time_axis.clear();
        m_frame_time_history.clear();
        m_gpu_time_history.clear();
        m_cpu_time_history.clear();
        m_fps_history.clear();
        m_draw_calls_history.clear();
        m_triangles_history.clear();
        m_vertices_history.clear();
        m_vram_history.clear();
        m_ram_history.clear();

        m_elapsed_time_s = 0.0f;
        m_frame_stats = {};
        m_gpu_stats = {};
        m_cpu_stats = {};
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
        m_gpu_stats = compute(m_gpu_time_history);
        m_cpu_stats = compute(m_cpu_time_history);
    }

}
