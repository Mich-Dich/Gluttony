#include "util/pch.h"
#include "log_viewer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

#include <util/system.h>
#include <util/io/logger.h>

#include "util/ui/pannel_collection.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // Cap a single poll's read so a burst of logging can't stall the frame.
    constexpr size_t            MAX_READ_PER_POLL         = 256 * 1024;

    // Cap the initial read of a pre-existing log file so opening the window is always cheap. Any earlier content is simply skipped.
    constexpr std::uintmax_t    MAX_INITIAL_READ_BYTES    = 4 * 1024 * 1024;

    // If a "line" grows beyond this without ever hitting a newline, treat it as garbage and discard — protects against binary files.
    constexpr size_t            MAX_PARTIAL_LINE_BYTES    = 64 * 1024;

    // Marker colours aligned with the logger plugin's console_color_table.
    constexpr ImU32 severity_colors[] = {
        IM_COL32(130, 130, 130, 255),   // trace: gray
        IM_COL32( 80, 140, 255, 255),   // debug: blue
        IM_COL32( 80, 200,  80, 255),   // info:  green
        IM_COL32(230, 200,  60, 255),   // warn:  yellow
        IM_COL32(230,  80,  80, 255),   // error: red
        IM_COL32(255,  50,  50, 255),   // fatal: bright red
    };

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

    log_viewer_window::log_viewer_window() {

        make_window_name("Log Viewer");

        // Seed the file list from the logger plugin if it's already up; if
        // not, this falls back to <exe>/logs. A manual Refresh re-enumerates.
        refresh_log_file_list();
    }


    log_viewer_window::~log_viewer_window() { }

    // CLASS PUBLIC ====================================================================================================

    void log_viewer_window::window(const f32 /*delta_time*/) {

        if (!m_show_window)
            return;

        apply_pending_dock();
        ImGui::SetNextWindowSizeConstraints(ImVec2(480.0f, 240.0f),
            ImVec2(std::numeric_limits<f32>::max(), std::numeric_limits<f32>::max()));

        if (ImGui::Begin(m_window_id.c_str(), &m_show_window)) {

            // Ctrl+F jumps focus to the filter box; Ctrl+L clears the view.
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {

                if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
                    m_request_focus_search = true;

                if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_L, false))
                    clear_view();
            }

            draw_toolbar();
            ImGui::Separator();
            draw_filter_bar();
            ImGui::Separator();
            draw_content();
            ImGui::Separator();
            draw_status_bar();
        }

        ImGui::End();
    }


    void log_viewer_window::update(const f32 delta_time) {

        // Poll at a fixed cadence rather than every frame — cheap and
        // completely imperceptible at 4 Hz.
        m_poll_accumulator += delta_time;
        if (m_poll_accumulator >= POLL_INTERVAL) {

            m_poll_accumulator = 0.0f;
            poll_log_file();
        }
    }


    bool log_viewer_window::serialize(const std::filesystem::path& /*project_file*/, const GLT::serializer::option /*option*/) { return false; }

    // CLASS PRIVATE ===================================================================================================

    // log file plumbing -----------------------------------------------------------------------------------------------

    void log_viewer_window::refresh_log_file_list() {

        m_available_log_files.clear();

        // Primary candidate: whatever the logger plugin reports it's writing
        // to right now.
        const std::filesystem::path current = GLT::logger::get_log_file_location();
        std::filesystem::path search_dir;

        if (!current.empty()) {

            search_dir = current.parent_path();
            m_available_log_files.push_back(current);

        } else {

            search_dir = GLT::util::get_executable_path() / GLT::config::LOG_DIR;
        }

        // Secondary candidates: any other .log files in the same directory.
        std::error_code error;
        if (GLT::vfs::is_directory(search_dir, error)) {

            auto iterator = GLT::vfs::directory_iterator(search_dir, error);
            if (!error) {
                for (const auto& entry : iterator) {

                    if (!entry.is_regular_file(error) || error)
                        continue;

                    if (entry.path().extension() != ".log")
                        continue;

                    if (std::contains(m_available_log_files, entry.path()))
                        continue;

                    m_available_log_files.push_back(entry.path());
                }
            }
        }

        // Fallback so the combo always has at least one entry.
        if (m_available_log_files.empty())
            m_available_log_files.push_back(search_dir / "general.log");

        if (m_selected_log_file < 0 || m_selected_log_file >= static_cast<int>(m_available_log_files.size()))
            m_selected_log_file = 0;

        select_log_file(m_selected_log_file);
    }


    void log_viewer_window::select_log_file(const int index) {

        if (index < 0 || index >= static_cast<int>(m_available_log_files.size()))
            return;

        m_selected_log_file = index;

        const auto& new_path = m_available_log_files[index];
        if (new_path == m_log_file_path)
            return;

        m_log_file_path = new_path;

        clear_view();
        reset_tail_state();
        read_new_bytes();       // prime the view immediately
    }


    void log_viewer_window::reset_tail_state() {

        m_last_read_pos = 0;
        m_partial_line.clear();
        m_skip_next_partial = false;

        if (m_log_file_path.empty())
            return;

        std::error_code error{};
        const auto size = GLT::vfs::file_size(m_log_file_path, error);
        if (error)
            return;

        // Skip to near the end of very large files so opening the window is
        // not gated on reading megabytes of old content.
        if (size > MAX_INITIAL_READ_BYTES) {

            m_last_read_pos = static_cast<std::streamoff>(size - MAX_INITIAL_READ_BYTES);
            m_skip_next_partial = true;     // first found "line" is a fragment
        }
    }


    void log_viewer_window::poll_log_file() {

        if (m_log_file_path.empty())
            return;

        std::error_code error{};
        const auto size = GLT::vfs::file_size(m_log_file_path, error);
        if (error)
            return;     // may not exist yet

        const auto size_off = static_cast<std::streamoff>(size);

        // Detect truncation / rotation (e.g. a new run with overwrite mode
        // started). Reset the cursor without wiping what the user is
        // currently looking at.
        if (size_off < m_last_read_pos) {

            m_last_read_pos = 0;
            m_partial_line.clear();
            m_skip_next_partial = false;
        }

        if (size_off == m_last_read_pos)
            return;

        read_new_bytes();
    }


    void log_viewer_window::read_new_bytes() {

        if (m_log_file_path.empty())
            return;

        std::error_code error{};
        const auto size = GLT::vfs::file_size(m_log_file_path, error);
        if (error)
            return;

        const auto size_off = static_cast<std::streamoff>(size);
        if (size_off <= m_last_read_pos)
            return;

        // Cap a single poll so a logging burst doesn't hitch the frame;
        // whatever is left over is picked up on the next poll.
        const std::streamoff remaining = size_off - m_last_read_pos;
        const std::streamoff to_read   = std::min(remaining, static_cast<std::streamoff>(MAX_READ_PER_POLL));

        std::ifstream file(m_log_file_path, std::ios::binary);
        if (!file.is_open())
            return;

        file.seekg(m_last_read_pos, std::ios::beg);
        if (!file)
            return;

        std::string chunk;
        chunk.resize(static_cast<size_t>(to_read));
        file.read(chunk.data(), to_read);
        const size_t bytes_read = static_cast<size_t>(file.gcount());
        chunk.resize(bytes_read);

        m_last_read_pos += static_cast<std::streamoff>(bytes_read);

        m_partial_line += chunk;

        size_t line_start = 0;
        while (true) {

            const size_t nl = m_partial_line.find('\n', line_start);
            if (nl == std::string::npos)
                break;

            std::string line = m_partial_line.substr(line_start, nl - line_start);

            // Strip trailing '\r' from Windows line endings.
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            if (m_skip_next_partial)
                m_skip_next_partial = false;
            else
                append_line(std::move(line));

            line_start = nl + 1;
        }

        if (line_start > 0)
            m_partial_line.erase(0, line_start);

        // Guard against a file that never emits a newline.
        if (m_partial_line.size() > MAX_PARTIAL_LINE_BYTES)
            m_partial_line.clear();
    }


    void log_viewer_window::append_line(std::string line) {

        log_line entry;
        entry.severity = detect_severity(line);
        entry.text     = std::move(line);

        m_lines.push_back(std::move(entry));

        if (m_lines.size() > m_max_lines) {

            const size_t to_drop = m_lines.size() - m_max_lines;
            m_lines.erase(m_lines.begin(), m_lines.begin() + static_cast<std::ptrdiff_t>(to_drop));
        }

        m_filter_dirty = true;
    }

    // drawing ---------------------------------------------------------------------------------------------------------

    void log_viewer_window::draw_toolbar() {

        // ---- file selector -------------------------------------------------------------------------------------
        ImGui::TextUnformatted("File");
        ImGui::SameLine();

        std::vector<std::string> names;
        names.reserve(m_available_log_files.size());
        for (const auto& p : m_available_log_files)
            names.push_back(p.filename().string());

        std::vector<const char*> name_ptrs;
        name_ptrs.reserve(names.size());
        for (const auto& n : names)
            name_ptrs.push_back(n.c_str());

        ImGui::SetNextItemWidth(240.0f);
        int selected = m_selected_log_file;
        if (!name_ptrs.empty() && ImGui::Combo("##log_file", &selected, name_ptrs.data(),
                static_cast<int>(name_ptrs.size()))) {

            select_log_file(selected);
        }

        ImGui::SameLine();
        if (UI::gray_button("Refresh"))
            refresh_log_file_list();

        ImGui::SameLine();
        if (UI::gray_button("Clear view"))
            clear_view();

        ImGui::SameLine();
        if (UI::gray_button("Copy path"))
            ImGui::SetClipboardText(m_log_file_path.generic_string().c_str());

        // ---- right-aligned toggles ------------------------------------------------------------------------------
        ImGui::SameLine();

        // Leave some space for the status text at the right edge.
        const f32 controls_w = 220.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12.0f,
            ImGui::GetWindowContentRegionMax().x - controls_w));

        ImGui::Checkbox("Auto-scroll", &m_auto_scroll);

        ImGui::SameLine();
        ImGui::Checkbox("Line #", &m_show_line_numbers);
    }


    void log_viewer_window::draw_filter_bar() {

        // ---- minimum severity ----------------------------------------------------------------------------------
        ImGui::TextUnformatted("Min level");
        ImGui::SameLine();

        static const char* min_level_labels[] = {
            "All", "Trace", "Debug", "Info", "Warn", "Error", "Fatal"
        };

        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::Combo("##log_min_sev", &m_min_severity, min_level_labels,
                IM_ARRAYSIZE(min_level_labels)))
            m_filter_dirty = true;

        // ---- substring filter ----------------------------------------------------------------------------------
        ImGui::SameLine();
        ImGui::TextUnformatted("Filter");
        ImGui::SameLine();

        if (m_request_focus_search) {

            ImGui::SetKeyboardFocusHere();
            m_request_focus_search = false;
        }

        char buffer[256];
        const size_t n = std::min(m_filter_text.size(), sizeof(buffer) - 1);
        std::memcpy(buffer, m_filter_text.data(), n);
        buffer[n] = '\0';

        ImGui::SetNextItemWidth(240.0f);
        if (ImGui::InputTextWithHint("##log_filter", "case-insensitive substring",
                buffer, sizeof(buffer))) {

            m_filter_text = buffer;
            m_filter_dirty = true;
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(m_filter_text.empty());
        if (ImGui::SmallButton("X##log_filter_clear")) {

            m_filter_text.clear();
            m_filter_dirty = true;
        }
        ImGui::EndDisabled();
    }


    void log_viewer_window::draw_content() {

        if (m_filter_dirty)
            rebuild_filter_cache();

        // The child claims the remaining vertical space minus the status bar.
        const f32 status_height = ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("##log_content", ImVec2(0, -status_height), false,
            ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysVerticalScrollbar);

        // Check BEFORE we render — auto-scroll should only kick in when the
        // user hasn't scrolled up manually.
        const bool was_at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;

        if (m_filtered_indices.empty()) {

            ImGui::TextDisabled("No log lines match the current filter.");

        } else {

            // One row per line. Both the marker column and ansi_text() consume
            // exactly one row, so the list clipper can skip offscreen entries.
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(m_filtered_indices.size()));

            while (clipper.Step()) {

                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {

                    const size_t line_index = m_filtered_indices[static_cast<size_t>(row)];
                    const log_line& line = m_lines[line_index];

                    // ---- severity marker ----------------------------------------------------------------
                    // Always reserve the column so plain lines and coloured
                    // lines stay left-aligned with each other.
                    {
                        const ImVec2 cursor    = ImGui::GetCursorScreenPos();
                        const f32    marker_w  = 3.0f;
                        const f32    marker_gap= 4.0f;
                        const f32    marker_h  = ImGui::GetTextLineHeight();

                        if (line.severity < 6) {

                            ImGui::GetWindowDrawList()->AddRectFilled(
                                cursor, ImVec2(cursor.x + marker_w, cursor.y + marker_h),
                                severity_colors[line.severity]);
                        }

                        ImGui::Dummy(ImVec2(marker_w + marker_gap, marker_h));
                        ImGui::SameLine(0, 0);
                    }

                    // ---- optional line number -----------------------------------------------------------
                    if (m_show_line_numbers) {

                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                        ImGui::Text("%6zu", line_index + 1);
                        ImGui::PopStyleColor();
                        ImGui::SameLine(0, 8);
                    }

                    // ---- the line itself ----------------------------------------------------------------
                    // ansi_text() handles any embedded ANSI colour codes that
                    // the logger's $B/$E format specifiers produced, and ends
                    // with a NewLine() — one row per call.
                    UI::ansi_text(line.text);
                }
            }

            clipper.End();
        }

        if (m_auto_scroll && was_at_bottom)
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndChild();
    }


    void log_viewer_window::draw_status_bar() {

        const size_t total    = m_lines.size();
        const size_t filtered = m_filtered_indices.size();

        if (filtered == total)
            ImGui::Text("%zu lines", total);
        else
            ImGui::Text("%zu / %zu lines", filtered, total);

        ImGui::SameLine();
        ImGui::TextDisabled("|");

        ImGui::SameLine();
        if (!m_log_file_path.empty()) {

            ImGui::TextDisabled("%s", m_log_file_path.filename().string().c_str());

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", m_log_file_path.generic_string().c_str());
        }

        // Buffer cursor as a tiny "live tail" indicator.
        ImGui::SameLine();
        ImGui::TextDisabled("|");

        ImGui::SameLine();
        ImGui::TextDisabled("cursor %lld",
            static_cast<long long>(m_last_read_pos));
    }

    // helpers ---------------------------------------------------------------------------------------------------------

    void log_viewer_window::rebuild_filter_cache() {

        m_filtered_indices.clear();
        m_filtered_indices.reserve(m_lines.size());

        std::string needle;
        if (!m_filter_text.empty())
            needle = GLT::util::to_lower(m_filter_text);

        // m_min_severity: 0 = All, 1 = Trace .. 6 = Fatal.
        // Underlying severity values are 0..5; -1 means "no lower bound".
        const int min_sev_value = m_min_severity - 1;

        for (size_t i = 0; i < m_lines.size(); ++i) {

            const log_line& line = m_lines[i];

            // Severity gate. Unknown lines (severity == 6) always pass.
            if (min_sev_value >= 0 && line.severity < 6) {

                if (static_cast<int>(line.severity) < min_sev_value)
                    continue;
            }

            if (!needle.empty()) {

                const std::string lowered = GLT::util::to_lower(line.text);
                if (lowered.find(needle) == std::string::npos)
                    continue;
            }

            m_filtered_indices.push_back(i);
        }

        m_filter_dirty = false;
    }


    void log_viewer_window::clear_view() {

        m_lines.clear();
        m_filtered_indices.clear();
        m_filter_dirty = true;
    }


    u8 log_viewer_window::detect_severity(const std::string& line) {

        // Look for the canonical severity tags anywhere in the line. The
        // logger's $L format spec emits these verbatim so this works for the
        // default format. If a custom format omits $L, the line falls back to
        // "unknown" and is always shown.
        static constexpr const char* tags[] = {
            "[TRACE]", "[DEBUG]", "[INFO]", "[WARN]", "[ERROR]", "[FATAL]"
        };

        for (u8 i = 0; i < 6; ++i) {

            if (line.find(tags[i]) != std::string::npos)
                return i;
        }

        return 6;   // unknown
    }

}
