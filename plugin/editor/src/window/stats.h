
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

    // Always-on, collapsible statistics overlay.
    //
    // Layout mirrors image_viewer_window / content_browser_window: a resizable
    // split with a fixed-width details panel on the left and a scrollable
    // plot area on the right.
    //
    //   +----------------------------+----------------------------------+
    //   |  tables (values)           |  plots                           |
    //   |  Frame / Rendering /       |   - combined ms plot (frame /    |
    //   |  Memory / Resources /      |     gpu / cpu on one axis)       |
    //   |  system-provided sections  |   - individual plot per stat     |
    //   +----------------------------+----------------------------------+
    //
    // Reads GLT::debug::get_application_stats() for the fixed application-level
    // metrics and iterates registered system providers for their custom values.
    class stats_window : public base_window {
    public:

        stats_window();
        ~stats_window();

        void window(const f32 delta_time) override;

        void update(const f32 delta_time) override;

        bool serialize(const std::filesystem::path& project_file, const GLT::serializer::option option) override;

    private:

        // sections (left panel) ---------------------------------------------------------------------------------------

        void draw_core_section();

        void draw_rendering_section();

        void draw_memory_section();

        void draw_resources_section();

        void draw_system_sections();

        // plots (right panel) -----------------------------------------------------------------------------------------

        void draw_combined_ms_plot();

        void draw_stat_plot(const char* id, const char* label, const std::vector<f32>& data, const ImVec4& color, const char* y_unit);

        // history -----------------------------------------------------------------------------------------------------

        void push_history_sample(const GLT::debug::application_stats& s);

        void clear_history();

        void recompute_window_stats();

        // ---- layout -------------------------------------------------------------------------------------------------

        static constexpr f32                                            DETAILS_PANEL_WIDTH = 500.0f;
        static constexpr f32                                            GRAPH_HEIGHT = 240.0f;
        static constexpr int                                            HISTORY_SIZE = 480;      // ~8 s at 60 fps

        // ---- sliding window -----------------------------------------------------------------------------------------

        // Parallel vectors instead of a ring buffer: ImPlot's PlotLine takes
        // (xs, ys, count) with contiguous data, so having separate arrays
        // avoids any stride gymnastics.
        std::vector<f32>                                                m_time_axis{};
        std::vector<f32>                                                m_frame_time_history{};
        std::vector<f32>                                                m_gpu_time_history{};
        std::vector<f32>                                                m_cpu_time_history{};
        std::vector<f32>                                                m_fps_history{};
        std::vector<f32>                                                m_draw_calls_history{};
        std::vector<f32>                                                m_triangles_history{};
        std::vector<f32>                                                m_vertices_history{};
        std::vector<f32>                                                m_vram_history{};
        std::vector<f32>                                                m_ram_history{};
        f32                                                             m_elapsed_time_s = 0.0f;

        // ---- window summary stats (recomputed each update) ----------------------------------------------------------

        struct window_stats {
            f32                                                         min = 0.0f;
            f32                                                         max = 0.0f;
            f32                                                         avg = 0.0f;
        };
        window_stats                                                    m_frame_stats{};
        window_stats                                                    m_gpu_stats{};
        window_stats                                                    m_cpu_stats{};

        // ---- snapshot (updated each frame, frozen when paused) ------------------------------------------------------

        GLT::debug::application_stats                                   m_app_snapshot{};
        std::vector<std::pair<std::string, GLT::debug::system_stats>>   m_system_sections{};

        // ---- view state ---------------------------------------------------------------------------------------------

        bool                                                            m_paused = false;
        bool                                                            m_gpu_available = false;    // true once any gpu_time_ms > 0 was seen

    };

}
