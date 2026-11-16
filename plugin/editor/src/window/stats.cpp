#include "util/pch.h"
#include "stats.h"

#include <imgui.h>
#include <implot.h>

#include <debug/profiler.h>
#include <util/ui/panel_collection.h>

#include <config/imgui_config.h>

#include "window/base/window_chrome.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    constexpr f64                               MB = 1024.0 * 1024.0;

    constexpr f32                               SPLITTER_WIDTH = 4.0f;

    constexpr f32                               MIN_DETAILS_WIDTH = 240.0f;

    constexpr f32                               MIN_PLOTS_WIDTH = 260.0f;

    constexpr f32                               TILE_HEIGHT = 64.0f;

    constexpr ImU32                             STATE_LIVE = IM_COL32(110, 200, 120, 255);

    constexpr ImU32                             STATE_PAUSED = IM_COL32(230, 180, 60, 255);

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // Vertical splitter between two panels (thin line, accent while hovered / dragged); edits [width]
    static void draw_splitter(const char* id, f32& width, f32 min_w, f32 max_w, f32 height);

    // printf into a std::string (system stats carry their own format string)
    static std::string format_value(const char* format, f32 value);

    // One summary tile: dim caption, big value, dim detail line. [accent] colors the bar at the tile's left edge
    static void stat_tile(const char* caption, const char* value, const char* detail, const ImVec4& accent, f32 width);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static void draw_splitter(const char* id, f32& width, const f32 min_w, const f32 max_w, const f32 height) {

        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, ImVec2(SPLITTER_WIDTH, std::max(height, 1.f)));

        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        if (active)
            width = std::clamp(width + ImGui::GetIO().MouseDelta.x, min_w, std::max(min_w, max_w));
        if (hovered || active)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        const f32 cx = cursor.x + SPLITTER_WIDTH * 0.5f;
        if (hovered || active)
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(cx - 1.0f, cursor.y), ImVec2(cx + 1.0f, cursor.y + height),
                ImGui::GetColorU32(active ? GLT::imgui_config::get_action_color00_active_ref() : GLT::imgui_config::get_action_color00_hover_ref()));
        else
            ImGui::GetWindowDrawList()->AddLine(ImVec2(cx, cursor.y), ImVec2(cx, cursor.y + height), ImGui::GetColorU32(ImGuiCol_Separator));
    }


#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wformat-nonliteral"
    #pragma GCC diagnostic ignored "-Wformat-security"
#endif
    static std::string format_value(const char* format, const f32 value) {

        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), format ? format : "%.2f", static_cast<f64>(value));
        return buffer;
    }
#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif


    static void stat_tile(const char* caption, const char* value, const char* detail, const ImVec4& accent, const f32 width) {

        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + width, min.y + TILE_HEIGHT);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        draw_list->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), 6.f);
        draw_list->AddRectFilled(ImVec2(min.x, min.y + 8.f), ImVec2(min.x + 3.f, max.y - 8.f), ImGui::GetColorU32(accent), 1.5f);

        const f32 x = min.x + 12.f;
        ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::small));
        draw_list->AddText(ImVec2(x, min.y + 7.f), ImGui::GetColorU32(ImGuiCol_TextDisabled), caption);
        const f32 detail_h = ImGui::GetTextLineHeight();
        if (detail && *detail)
            draw_list->AddText(ImVec2(x, max.y - detail_h - 7.f), ImGui::GetColorU32(ImGuiCol_TextDisabled), detail);
        ImGui::PopFont();

        ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::bold_big));
        draw_list->AddText(ImVec2(x, min.y + (TILE_HEIGHT - ImGui::GetTextLineHeight()) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), value);
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(width, TILE_HEIGHT));
    }

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
        ImGui::SetNextWindowSizeConstraints(ImVec2(620.0f, 360.0f),
            ImVec2(std::numeric_limits<f32>::max(), std::numeric_limits<f32>::max()));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        const bool open = ImGui::Begin(m_window_id.c_str(), &m_show_window);
        ImGui::PopStyleVar();

        if (open) {

            draw_header();

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 8.f));
            ImGui::BeginChild("##stats_body", ImVec2(0.f, 0.f), ImGuiChildFlags_AlwaysUseWindowPadding,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleVar();

            draw_summary_tiles();
            ImGui::Dummy(ImVec2(0.f, 4.f));

            // ---- details (left) | splitter | plots (right) ---------------------------------------------------
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            m_details_width = std::clamp(m_details_width, MIN_DETAILS_WIDTH, std::max(MIN_DETAILS_WIDTH, avail.x - MIN_PLOTS_WIDTH - SPLITTER_WIDTH));

            ImGui::BeginChild("##stats_details", ImVec2(m_details_width, avail.y), ImGuiChildFlags_None);
            draw_core_section();
            draw_rendering_section();
            draw_memory_section();
            draw_resources_section();
            draw_system_sections();
            ImGui::EndChild();

            ImGui::SameLine(0.f, 0.f);
            draw_splitter("##stats_splitter", m_details_width, MIN_DETAILS_WIDTH, avail.x - MIN_PLOTS_WIDTH - SPLITTER_WIDTH, avail.y);
            ImGui::SameLine(0.f, 0.f);

            ImGui::BeginChild("##stats_plots", ImVec2(0.f, avail.y), ImGuiChildFlags_None);
            if (m_time_axis.empty()) {
                UI::empty_state("Collecting samples...", m_paused ? "Resume to record new samples" : nullptr);
            } else {

                draw_combined_ms_plot();
                draw_stat_plot("##plot_fps", "FPS", m_fps_history, ImVec4(0.35f, 0.85f, 0.90f, 1.0f), "fps");
                draw_stat_plot("##plot_drawcalls", "Draw Calls", m_draw_calls_history, ImVec4(0.95f, 0.80f, 0.35f, 1.0f), "calls");
                draw_stat_plot("##plot_triangles", "Triangles", m_triangles_history, ImVec4(0.75f, 0.55f, 0.95f, 1.0f), "tris");
                draw_stat_plot("##plot_vertices", "Vertices", m_vertices_history, ImVec4(0.95f, 0.55f, 0.80f, 1.0f), "verts");
                draw_stat_plot("##plot_vram", "VRAM", m_vram_history, ImVec4(0.40f, 0.70f, 1.00f, 1.0f), "MB");
                draw_stat_plot("##plot_ram", "RAM", m_ram_history, ImVec4(0.55f, 0.90f, 0.55f, 1.0f), "MB");
            }
            ImGui::EndChild();

            ImGui::EndChild();
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

    void stats_window::draw_header() {

        const char* pause_label = m_paused ? "Resume" : "Pause";
        const char* reset_label = "Reset";
        const f32 pause_w = chrome::button_width("Resume");
        const f32 reset_w = chrome::button_width(reset_label);

        const std::string subtitle = std::to_string(m_time_axis.size()) + " samples, "
            + std::format("{:.1f} s", m_time_axis.empty() ? 0.f : m_time_axis.back() - m_time_axis.front());

        chrome::header_options options{};
        options.subtitle = subtitle.c_str();
        options.pill = m_paused ? "Paused" : "Live";
        options.pill_color = m_paused ? STATE_PAUSED : STATE_LIVE;
        options.buttons_width = pause_w + reset_w + 6.f;
        const chrome::header_bar bar = chrome::begin_header_bar(icon_manager::icon::settings, "Debug Statistics", options);

        if (UI::subtle_button(pause_label, ImVec2(pause_w, 0.f)))
            m_paused = !m_paused;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", m_paused ? "Record new samples again" : "Freeze the numbers and plots");

        ImGui::SameLine(0.f, 6.f);
        if (UI::subtle_button(reset_label, ImVec2(reset_w, 0.f)))
            clear_history();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Throw away the recorded history");

        chrome::end_header_bar(bar);
    }


    void stats_window::draw_summary_tiles() {

        const f32 spacing = 8.f;
        const int tiles = m_gpu_available ? 4 : 3;
        const f32 width = std::max(80.f, (ImGui::GetContentRegionAvail().x - spacing * (tiles - 1)) / static_cast<f32>(tiles));
        const bool has_window = !m_frame_time_history.empty();

        char value[48];
        char detail[64];

        // frame time
        std::snprintf(value, sizeof(value), "%.2f ms", m_app_snapshot.frame_time_ms);
        if (has_window)
            std::snprintf(detail, sizeof(detail), "avg %.2f  max %.2f", m_frame_stats.avg, m_frame_stats.max);
        else
            detail[0] = '\0';
        stat_tile("FRAME", value, detail, ImVec4(0.40f, 0.85f, 0.40f, 1.0f), width);

        // fps
        ImGui::SameLine(0.f, spacing);
        std::snprintf(value, sizeof(value), "%.0f", m_app_snapshot.fps);
        const auto target = GLT::application::get().get_fps_controller().get_target_interval_duration();
        const f64 target_ms = std::chrono::duration<f64, std::milli>(target).count();
        if (target_ms > 0.0)
            std::snprintf(detail, sizeof(detail), "target %.0f", 1000.0 / target_ms);
        else
            detail[0] = '\0';
        stat_tile("FPS", value, detail, ImVec4(0.35f, 0.85f, 0.90f, 1.0f), width);

        // gpu
        if (m_gpu_available) {
            ImGui::SameLine(0.f, spacing);
            std::snprintf(value, sizeof(value), "%.2f ms", m_app_snapshot.render.gpu_time_ms);
            if (has_window)
                std::snprintf(detail, sizeof(detail), "avg %.2f  max %.2f", m_gpu_stats.avg, m_gpu_stats.max);
            else
                detail[0] = '\0';
            stat_tile("GPU", value, detail, ImVec4(0.40f, 0.60f, 1.00f, 1.0f), width);
        }

        // cpu work
        ImGui::SameLine(0.f, spacing);
        std::snprintf(value, sizeof(value), "%.2f ms", m_app_snapshot.cpu_time_ms);
        std::snprintf(detail, sizeof(detail), "wait %.2f  limiter %.2f", m_app_snapshot.cpu_wait_ms, m_app_snapshot.limiter_ms);
        stat_tile("CPU WORK", value, detail, ImVec4(1.00f, 0.65f, 0.40f, 1.0f), width);
    }


    void stats_window::draw_core_section() {

        UI::card_options options{};
        options.collapsible = true;
        if (UI::begin_card("Frame", options)) {

            UI::property_value("Frame time", std::format("{:.2f} ms", m_app_snapshot.frame_time_ms));
            UI::property_value("FPS", std::format("{:.1f}", m_app_snapshot.fps));
            if (m_gpu_available)
                UI::property_value("GPU time", std::format("{:.2f} ms", m_app_snapshot.render.gpu_time_ms), "Measured with GPU timestamp queries");

            // CPU time used to include every blocking call (fence waits, acquire, present/vsync), so it always tracked the frame
            // time. Now: work = main loop minus waits; wait = blocked on GPU / swapchain; limiter = frame-rate cap sleep
            UI::property_value("CPU work", std::format("{:.2f} ms", m_app_snapshot.cpu_time_ms), "Main loop time minus the waits below");
            UI::property_value("CPU wait", std::format("{:.2f} ms", m_app_snapshot.cpu_wait_ms), "Blocked on GPU fences, image acquire, present / vsync");
            UI::property_value("Frame limiter", std::format("{:.2f} ms", m_app_snapshot.limiter_ms), "Sleep of the frame-rate cap");

            // Summary of the sliding window - mirrors exactly what the plots are showing
            if (!m_frame_time_history.empty()) {
                chrome::section_label("WINDOW", "min / avg / max");
                UI::property_value("Frame", std::format("{:.2f} / {:.2f} / {:.2f} ms", m_frame_stats.min, m_frame_stats.avg, m_frame_stats.max));
                if (m_gpu_available)
                    UI::property_value("GPU", std::format("{:.2f} / {:.2f} / {:.2f} ms", m_gpu_stats.min, m_gpu_stats.avg, m_gpu_stats.max));
                UI::property_value("CPU work", std::format("{:.2f} / {:.2f} / {:.2f} ms", m_cpu_stats.min, m_cpu_stats.avg, m_cpu_stats.max));
            }
        }
        UI::end_card();
    }


    void stats_window::draw_rendering_section() {

        UI::card_options options{};
        options.collapsible = true;
        if (UI::begin_card("Rendering", options)) {
            UI::property_value("Draw calls", std::to_string(m_app_snapshot.render.draw_calls), "Includes the UI");
            UI::property_value("Triangles", std::to_string(m_app_snapshot.render.triangles));
            UI::property_value("Vertices", std::to_string(m_app_snapshot.render.vertices));
            UI::property_value("Render passes", std::to_string(m_app_snapshot.render.render_passes));
        }
        UI::end_card();
    }


    void stats_window::draw_memory_section() {

        UI::card_options options{};
        options.collapsible = true;
        if (UI::begin_card("Memory", options)) {
            UI::property_value("VRAM", std::format("{:.2f} MB", m_app_snapshot.render.vram_bytes / MB));
            UI::property_value("RAM", std::format("{:.2f} MB", m_app_snapshot.ram_bytes / MB));
            UI::property_value("RAM peak", std::format("{:.2f} MB", GLT::util::get_process_peak_ram_bytes() / MB), "Highest RAM use of this process so far");
        }
        UI::end_card();
    }


    void stats_window::draw_resources_section() {

        UI::card_options options{};
        options.collapsible = true;
        if (UI::begin_card("GPU Resources", options)) {
            UI::property_value("Textures", std::to_string(m_app_snapshot.render.texture_count));
            UI::property_value("Buffers", std::to_string(m_app_snapshot.render.buffer_count));
            UI::property_value("Descriptor sets", std::to_string(m_app_snapshot.render.descriptor_set_count));
            UI::property_value("Pipelines", std::to_string(m_app_snapshot.render.pipeline_count));
        }
        UI::end_card();
    }


    void stats_window::draw_system_sections() {

        bool first = true;
        for (const auto& [name, stats] : m_system_sections) {

            if (stats.custom.empty())
                continue;

            // Plugin-provided sections: under their own label so they read apart from the fixed built-in set
            if (first) {
                chrome::section_label("SYSTEMS");
                first = false;
            }

            UI::card_options options{};
            options.collapsible = true;
            const std::string summary = std::to_string(stats.custom.size()) + (stats.custom.size() == 1 ? " value" : " values");
            options.summary = summary.c_str();
            if (UI::begin_card(name.c_str(), options)) {
                for (const auto& v : stats.custom)
                    UI::property_value(v.name.c_str(), format_value(v.format, v.value));
            }
            UI::end_card();
        }
    }

    // plots (right panel) ---------------------------------------------------------------------------------------------

    void stats_window::draw_combined_ms_plot() {

        if (m_time_axis.empty())
            return;

        const int count = static_cast<int>(m_time_axis.size());
        const auto target_interval_duration = GLT::application::get().get_fps_controller().get_target_interval_duration();
        const f64 target_frame_ms = std::chrono::duration<f64, std::milli>(target_interval_duration).count();

        // ---- Series --------------------------------------------------------------
        //
        //   bottom band = CPU work  ->  [0        .. work]
        //   top band    = CPU wait  ->  [work     .. work + wait]   (blocked on GPU fences / swapchain / vsync)
        //   GPU time    = separate line: CPU and GPU run in parallel, stacking them would double count
        std::vector<f32> zero_line(count, 0.0f);
        std::vector<f32> cpu_plus_wait(count);
        for (int i = 0; i < count; ++i)
            cpu_plus_wait[i] = m_cpu_time_history[i] + m_cpu_wait_history[i];

        // ---- Auto-fit Y ----------------------------------------------------------
        // Must clear: the frame budget (with headroom so the reference line never
        // touches the top edge), the measured wall-clock frame time, and the
        // stacked total.
        f32 y_max = static_cast<f32>(target_frame_ms) * 1.25f;
        for (int i = 0; i < count; ++i) {
            y_max = std::max(y_max, m_frame_time_history[i]);
            y_max = std::max(y_max, cpu_plus_wait[i]);
            if (m_gpu_available)
                y_max = std::max(y_max, m_gpu_time_history[i]);
        }
        y_max = std::max(y_max, 1.0f);      // never collapse to zero height

        const f32 x_min = m_time_axis.front();
        const f32 x_max = m_time_axis.back();

        UI::card_options card{};
        card.collapsible = true;
        const std::string summary = std::format("{:.2f} ms, budget {:.2f} ms", m_frame_time_history.back(), target_frame_ms);
        card.summary = summary.c_str();
        const bool open = UI::begin_card("Frame Time", card);

        if (open && ImPlot::BeginPlot("##frame_time_plot", ImVec2(-1.0f, GRAPH_HEIGHT + 40.0f), ImPlotFlags_NoMenus | ImPlotFlags_NoMouseText)) {

            ImPlot::SetupAxes("time (s)", "ms", ImPlotAxisFlags_NoHighlight, ImPlotAxisFlags_NoHighlight);
            ImPlot::SetupAxesLimits(x_min, std::max(x_max, x_min + 0.001f), 0.0, static_cast<f64>(y_max), ImPlotCond_Always);
            ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal);

            // ---- Frame budget reference -----------------------------------------
            // Drawn first so the stacked bands and the total line sit on top of it.
            ImPlot::PlotInfLines("Frame budget", &target_frame_ms, 1,
                ImPlotSpec(ImPlotProp_LineColor,  ImVec4(1.0f, 0.45f, 0.45f, 0.65f), 
                    ImPlotProp_LineWeight, 1.0f,
                    ImPlotProp_Flags, ImPlotInfLinesFlags_Horizontal));

            // ---- CPU work + CPU wait (stacked), GPU (line) -----------------------
            ImPlot::PlotShaded("CPU work", m_time_axis.data(), zero_line.data(), m_cpu_time_history.data(), count,
                ImPlotSpec(ImPlotProp_FillColor, ImVec4(1.00f, 0.65f, 0.40f, 0.55f), ImPlotProp_LineColor, ImVec4(1.00f, 0.65f, 0.40f, 1.00f)));

            ImPlot::PlotShaded("CPU wait", m_time_axis.data(), m_cpu_time_history.data(), cpu_plus_wait.data(), count,
                ImPlotSpec(ImPlotProp_FillColor, ImVec4(0.85f, 0.30f, 0.30f, 0.35f), ImPlotProp_LineColor, ImVec4(0.85f, 0.30f, 0.30f, 0.80f)));

            if (m_gpu_available)
                ImPlot::PlotLine("GPU", m_time_axis.data(), m_gpu_time_history.data(), count,
                    ImPlotSpec(ImPlotProp_LineColor, ImVec4(0.40f, 0.60f, 1.00f, 1.00f), ImPlotProp_LineWeight, 1.5f));

            // ---- Measured wall-clock frame time ---------------------------------
            // This is the line the user actually "feels" - when it crosses above
            // the budget line, the frame is over-budget.
            ImPlot::PlotLine("Frame time", m_time_axis.data(), m_frame_time_history.data(), count, 
                ImPlotSpec(ImPlotProp_LineColor, ImVec4(0.40f, 0.85f, 0.40f, 1.0f), ImPlotProp_LineWeight, 1.5f));

            ImPlot::EndPlot();
        }
        UI::end_card();
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

        // One collapsible card per plot; the latest value stays visible in the header while collapsed
        UI::card_options card{};
        card.collapsible = true;
        const f32 latest = data.back();
        const std::string summary = (latest >= 10000.f ? std::format("{:.0f}", latest) : std::format("{:.2f}", latest)) + " " + y_unit;
        card.summary = summary.c_str();

        ImGui::PushID(id);
        if (UI::begin_card(label, card)) {

            if (ImPlot::BeginPlot(id, ImVec2(-1.0f, GRAPH_HEIGHT), ImPlotFlags_NoMenus | ImPlotFlags_NoMouseText | ImPlotFlags_NoLegend)) {

                ImPlot::SetupAxes(nullptr, y_unit, ImPlotAxisFlags_NoHighlight, ImPlotAxisFlags_NoHighlight);
                ImPlot::SetupAxesLimits(x_min, std::max(x_max, x_min + 0.001f), 0.0, static_cast<f64>(y_max), ImPlotCond_Always);
                ImVec4 fill = color;
                fill.w = 0.18f;
                ImPlot::PlotShaded(label, m_time_axis.data(), data.data(), count, 0.0,
                    ImPlotSpec(ImPlotProp_FillColor, fill, ImPlotProp_LineColor, color));
                ImPlot::PlotLine(label, m_time_axis.data(), data.data(), count, ImPlotSpec(ImPlotProp_LineColor,  color, 
                    ImPlotProp_LineWeight, 1.5f));

                ImPlot::EndPlot();
            }
        }
        UI::end_card();
        ImGui::PopID();
    }


    // history ---------------------------------------------------------------------------------------------------------

    void stats_window::push_history_sample(const GLT::debug::application_stats& stats) {

        m_elapsed_time_s += ImGui::GetIO().DeltaTime;

        m_time_axis.push_back(m_elapsed_time_s);

        m_frame_time_history.push_back(stats.frame_time_ms);
        m_gpu_time_history.push_back(stats.render.gpu_time_ms);
        m_cpu_time_history.push_back(stats.cpu_time_ms);
        m_cpu_wait_history.push_back(stats.cpu_wait_ms);

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
            drop(m_cpu_wait_history);
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
        m_cpu_wait_history.clear();
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
