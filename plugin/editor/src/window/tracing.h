#pragma once

#include <debug/profiler.h>
#include <debug/trace.h>

#include "window/base/base_window.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Profiling overview built on the engine's instrumentation (GLT_PROFILE_* zones, see core/debug/trace.h). The same
    // zones go to Tracy, so this window is the quick in-editor view and Tracy (launched from here) is the deep one.
    //
    //   +-------------------------------------------------------------------------------------------------------+
    //   | Tracy: version / connected?   [Launch Tracy] [path]     [x] Record  [Pause] [Clear]                    |
    //   | frame history: one bar per frame = CPU work | CPU wait (GPU / vsync) | limiter, click to select         |
    //   +-------------------------------------------------------------------------------------------------------+
    //   | tabs:  Timeline (selected frame, per thread)  |  Zones (sortable stats, self / inclusive)  |  GPU passes |
    //   +-------------------------------------------------------------------------------------------------------+
    class tracing_window : public base_window {
    public:

        tracing_window();
        ~tracing_window();

        void window(const f32 delta_time) override;

        void update(const f32 delta_time) override;

        bool serialize(const std::filesystem::path& project_file, const GLT::serializer::option option) override;

    private:

        struct frame_summary {
            f32                                                         total_ms = 0.f;
            f32                                                         work_ms = 0.f;
            f32                                                         wait_ms = 0.f;
            f32                                                         idle_ms = 0.f;
        };

        struct zone_stats {
            std::string_view                                            name;
            u32                                                         calls = 0;
            u32                                                         frames = 0;         // frames the zone appeared in
            f64                                                         inclusive_ms = 0.0;
            f64                                                         self_ms = 0.0;
            f64                                                         max_ms = 0.0;
            bool                                                        wait = false;
        };

        // ---- sections -----------------------------------------------------------------------------------------------

        void draw_tracy_bar();

        void draw_frame_history();

        void draw_timeline();

        void draw_zone_table();

        void draw_gpu_passes();

        // ---- helpers ------------------------------------------------------------------------------------------------

        void rebuild_summaries();

        void rebuild_zone_stats();

        void launch_tracy();

        [[nodiscard]] const GLT::debug::trace::frame_record* selected_frame() const;

        // ---- layout -------------------------------------------------------------------------------------------------

        static constexpr f32                                            HISTORY_HEIGHT = 110.0f;
        static constexpr f32                                            LANE_ROW_HEIGHT = 20.0f;

        // ---- state --------------------------------------------------------------------------------------------------

        std::vector<GLT::debug::trace::frame_record>                    m_frames{};          // snapshot of the recorder
        std::vector<frame_summary>                                      m_summaries{};
        std::vector<zone_stats>                                         m_zone_stats{};
        std::vector<std::string>                                        m_thread_names{};
        std::vector<std::pair<std::string, GLT::debug::system_stats>>   m_system_sections{};
        u32                                                             m_main_thread = 0;

        i32                                                             m_selected = -1;     // index into m_frames, -1 = latest
        bool                                                            m_follow_latest = true;
        bool                                                            m_record = true;
        bool                                                            m_zone_stats_dirty = true;
        u32                                                             m_zone_frames = 120; // frames aggregated by the zone table

        f32                                                             m_timeline_zoom = 1.0f;
        f32                                                             m_timeline_pan = 0.0f;  // 0..1 of the frame

        char                                                            m_tracy_path[512] = {};
        int                                                             m_capture_frames = 300;
        std::string                                                     m_export_status{};
        std::string                                                     m_launch_status{};
    };

}
