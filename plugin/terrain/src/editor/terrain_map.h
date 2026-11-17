
#pragma once

#include <imgui.h>

#include "graph/evaluator.h"
#include "user_texture.h"



namespace GLT::terrain {

    // @brief The 2D map of the terrain editor: any pin of the graph over any part of the world, at screen resolution
    //
    // Pan with the left / middle mouse button, zoom with the wheel. The visible area (any aspect ratio, it fills the
    // rectangle it is given) is evaluated on a worker thread with RESOLUTION samples along its longer side whenever the
    // view, the graph or the inspected pin changes. Heightfields are drawn hill-shaded with a height tint, masks as a
    // heat map. View size, value range, timing and errors are drawn as overlays on the map
    class terrain_map {
    public:

        static constexpr u32                            RESOLUTION = 256;

        struct target {
            std::shared_ptr<const terrain_graph>        graph{};
            u64                                         revision = 0;
            u32                                         node = INVALID_NODE_ID;
            std::string                                 pin{};
            bool                                        mask = false;
            u64                                         seed = 0;
            std::unordered_map<std::string, f32>        parameters{};
            u64                                         parameters_hash = 0;
        };

        explicit terrain_map(evaluator& eval);
        ~terrain_map();

        terrain_map(const terrain_map&) = delete;
        terrain_map& operator=(const terrain_map&) = delete;

        // Draws into the current window, filling [size]
        void draw(const target& what, ImVec2 size);

        // Shows this world rectangle (fitted into the map's aspect ratio on the next draw)
        void frame(glm::dvec2 min, glm::dvec2 max);

        [[nodiscard]] const std::string& status() const noexcept { return m_status; }

    private:

        void collect();
        void start(const target& what, u64 key);
        void colorize(const field& data, bool mask);

        evaluator&                                      m_eval;
        user_texture                                    m_texture{};

        glm::dvec2                                      m_center{ 0.0 };
        f64                                             m_view = 8192.0;        // visible world width
        f64                                             m_aspect = 1.0;         // height / width of the map rectangle
        std::optional<std::pair<glm::dvec2, glm::dvec2>> m_frame_request{};
        std::string                                     m_stats{};              // timing of the shown result
        bool                                            m_failed = false;

        std::future<eval_outcome>                       m_pending{};
        std::shared_ptr<std::atomic<bool>>              m_cancel{};
        u64                                             m_pending_key = 0;
        u64                                             m_failed_key = 0;       // not retried until something changes
        std::chrono::steady_clock::time_point           m_pending_start{};
        bool                                            m_pending_mask = false;
        glm::dvec2                                      m_pending_center{ 0.0 };
        f64                                             m_pending_view = 0.0;

        // What the texture shows
        std::shared_ptr<const field>                    m_field{};
        u64                                             m_shown_key = 0;
        glm::dvec2                                      m_shown_center{ 0.0 };
        f64                                             m_shown_view = 0.0;
        f32                                             m_min = 0.0f;
        f32                                             m_max = 0.0f;

        std::string                                     m_status{};
        std::vector<u32>                                m_pixels{};
    };

}
