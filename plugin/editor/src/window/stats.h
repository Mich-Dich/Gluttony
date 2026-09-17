
#pragma once

#include <debug/profiler.h>

#include "window/base_window.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Always-on, collapsible statistics overlay with an ImPlot-rendered
    // frame-time graph.
    //
    // Reads GLT::debug::get_application_stats() for the fixed application-level
    // metrics (frame time, FPS, VRAM, resource counts, ...) and iterates
    // registered system providers for their custom values. Every section is
    // independent: a system that disappears simply removes its section.
    class stats_window : public base_window {
    public:

        stats_window();
        ~stats_window();

        void window(const f32 delta_time) override;

        void update(const f32 delta_time) override;

        bool serialize(const std::filesystem::path& project_file, const GLT::serializer::option option) override;

    private:

        // drawing -----------------------------------------------------------------------------------------------------
        void draw_core_section();

        void draw_frame_graph();

        void draw_rendering_section();

        void draw_memory_section();

        void draw_resources_section();

        void draw_system_sections();

        // history -----------------------------------------------------------------------------------------------------
        void push_history_sample(const GLT::debug::application_stats& s);

        void clear_history();

        void recompute_window_stats();

        // ---- sliding window ----------------------------------------------------------------------------------------
        // Parallel vectors instead of a ring buffer: ImPlot's PlotLine takes
        // (xs, ys, count) with contiguous data, so having separate arrays
        // avoids any stride gymnastics. At 480 samples the erase-front cost
        // is negligible (~2 KB memmove).
        static constexpr int                                    HISTORY_SIZE = 480;     // ~8 s at 60 fps

        std::vector<f32>                                        m_time_axis{};
        std::vector<f32>                                        m_frame_time_history{};
        std::vector<f32>                                        m_gpu_time_history{};
        f32                                                     m_elapsed_time_s = 0.0f;

        // ---- window summary stats (recomputed each update) ---------------------------------------------------------
        struct window_stats {
            f32 min = 0.0f;
            f32 max = 0.0f;
            f32 avg = 0.0f;
        };
        window_stats                                            m_frame_stats{};
        window_stats                                            m_gpu_stats{};

        // ---- snapshot (updated each frame, frozen when paused) -----------------------------------------------------
        GLT::debug::application_stats                           m_app_snapshot{};
        std::vector<std::pair<std::string, GLT::debug::system_stats>>   m_system_sections{};

        // ---- view state --------------------------------------------------------------------------------------------
        bool                                                    m_paused = false;
        bool                                                    m_gpu_available = false;    // true once any gpu_time_ms > 0 was seen
        bool                                                    m_show_gpu = true;
        bool                                                    m_show_target_line = true;
        f32                                                     m_target_frame_ms = 16.667f;    // 60 fps
    };

}
