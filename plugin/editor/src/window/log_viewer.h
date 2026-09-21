
#pragma once

#include "window/base_window.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // A single parsed log line read from disk.
    //
    // severity is 0..5 matching GLT::logger::severity, or 6 if no severity
    // tag could be located in the line (e.g. the banner lines written by the
    // logger plugin during init/shutdown). Unknown lines are always shown
    // regardless of the active minimum-severity filter, so format changes
    // that omit `$L` don't silently blank the view.
    struct log_line {
        std::string     text{};
        u8              severity = 6;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Live-tailing log viewer.
    //
    // The editor and the logger live in different plugins. Rather than
    // hooking the logger plugin's internal queue (which would require
    // touching that plugin), this window simply reads the log file the
    // logger plugin writes to. Every frame the file's size is polled; if
    // anything changed, the new bytes are read and split into lines. This
    // keeps the view live while remaining completely decoupled from the
    // logger implementation - any future logger plugin that writes to the
    // same path works unchanged.
    //
    // Parsing of severity tags is best-effort: it looks for the canonical
    // "[TRACE]"/"[DEBUG]"/... substrings anywhere in the line. Lines with no
    // detectable tag are shown in every filter mode.
    //
    // The log file's path is taken from GLT::logger::get_log_file_location();
    // if the logger plugin has not yet initialised, the window falls back to
    // <exe>/logs/general.log and any other .log files found next to it.
    class log_viewer_window : public base_window {
    public:

        log_viewer_window();
        ~log_viewer_window();

        void window(f32 delta_time) override;


        void update(f32 delta_time) override;


        bool serialize(const std::filesystem::path& project_file, const GLT::serializer::option option) override;

    private:

        // ---- log file plumbing ---------------------------------------------------------------------------------
        // Enumerate *.log files in the log directory and (re)select the
        // current one. Called on construction and via the Refresh button.
        void refresh_log_file_list();

        // Switch to the file at `index` in m_available_log_files. Resets the
        // tail cursor and clears the displayed lines.
        void select_log_file(int index);

        // Position the tail cursor. If the file is already huge, skip to
        // near the end so opening the window doesn't read the whole history.
        void reset_tail_state();

        // Check the file for new bytes and read them if any. Cheap when
        // nothing changed (file_size only).
        void poll_log_file();

        // Read from m_last_read_pos to EOF, split into lines, feed each to
        // append_line(). Handles CRLF and unterminated trailing lines.
        void read_new_bytes();

        // Push a completed line into the ring buffer and mark the filter
        // cache dirty.
        void append_line(std::string line);

        // ---- drawing -------------------------------------------------------------------------------------------
        void draw_toolbar();

        void draw_filter_bar();

        void draw_content();

        void draw_status_bar();

        // ---- helpers -------------------------------------------------------------------------------------------
        void rebuild_filter_cache();

        void clear_view();

        static u8 detect_severity(const std::string& line);

        // ---- file tailing state --------------------------------------------------------------------------------
        std::vector<std::filesystem::path>      m_available_log_files{};
        int                                     m_selected_log_file = -1;
        std::filesystem::path                   m_log_file_path{};
        std::streamoff                          m_last_read_pos = 0;
        std::string                             m_partial_line{};           // bytes without a newline yet
        bool                                    m_skip_next_partial = false;

        // ---- parsed lines --------------------------------------------------------------------------------------
        std::vector<log_line>                   m_lines{};
        std::vector<size_t>                     m_filtered_indices{};
        bool                                    m_filter_dirty = true;

        // ---- filter / display options --------------------------------------------------------------------------
        int                                     m_min_severity = 0;         // 0 = All, 1 = Trace .. 6 = Fatal
        std::string                             m_filter_text{};
        bool                                    m_auto_scroll = true;
        bool                                    m_show_line_numbers = false;
        size_t                                  m_max_lines = 10000;

        // ---- frame timing --------------------------------------------------------------------------------------
        f32                                     m_poll_accumulator = 0.0f;
        static constexpr f32                    POLL_INTERVAL = 0.25f;

        // ---- UI state ------------------------------------------------------------------------------------------
        bool                                    m_request_focus_search = false;
    };

}
