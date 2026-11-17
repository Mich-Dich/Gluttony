
#pragma once

#include <imgui.h>
#include <render/procedural_mesh.h>
#include <world/camera_snapshot.h>

#include "graph/evaluator.h"



namespace GLT::render {
    class i_scene_renderer;
}

namespace GLT::terrain {

    // @brief The 3D view of the terrain editor: the graph's surface around the camera, in its own offscreen renderer
    //
    // Like the 2D map, the view follows the camera: a window of TILES x TILES preview chunks (QUADS x QUADS quads each)
    // centred under the orbit point. The sample spacing is a power of two chosen from the camera distance, so zooming
    // out covers more ground with coarser chunks, zooming in shows detail. Chunks sit on the shared sample lattice of
    // their spacing (neighbours meet exactly) and are evaluated on the thread pool, nearest first, then published as
    // procedural meshes the offscreen scene renderer draws under a sky with a sun. Nothing touches the world.
    //
    // Changing view: chunks that left the window stay visible until the new chunks covering their area are ready, so
    // zooming / panning never opens holes. Changing graph, seed or parameters: a chunk keeps its old mesh until the new
    // one is there.
    //
    // Camera: left drag orbits, middle / right drag pans over the ground, wheel zooms, WASD (+ Q / E) moves while hovered.
    class terrain_preview_3d {
    public:

        static constexpr u32                            TILES = 10;             // per side of the window
        static constexpr u32                            QUADS = 64;             // per tile side

        struct target {
            std::shared_ptr<const terrain_graph>        graph{};
            u64                                         revision = 0;
            u32                                         node = INVALID_NODE_ID; // a heightfield output (or a sink's input)
            std::string                                 pin{};
            u64                                         seed = 0;
            std::unordered_map<std::string, f32>        parameters{};
            u64                                         parameters_hash = 0;
            glm::dvec2                                  domain_min{ 0.0 };      // what "frame" shows
            glm::dvec2                                  domain_max{ 0.0 };
        };

        explicit terrain_preview_3d(evaluator& eval);
        ~terrain_preview_3d();

        terrain_preview_3d(const terrain_preview_3d&) = delete;
        terrain_preview_3d& operator=(const terrain_preview_3d&) = delete;

        // Draws into the current window, filling [size]. Creates the renderer on first use
        void draw(const target& what, ImVec2 size);

        // Not shown this frame: the renderer stops (no GPU work), the meshes stay
        void set_visible(bool visible);

        // The next draw puts the preview domain in view
        void frame() noexcept { m_frame_pending = true; }

    private:

        struct tile {
            i32                                         level = 0;              // spacing = 2^level
            i64                                         ix = 0;                 // tile coordinates at that spacing
            i64                                         iy = 0;
            bool                                        desired = false;        // inside the current window

            GLT::render::procedural_mesh_id             mesh = GLT::render::INVALID_PROCEDURAL_MESH;
            u64                                         shown_key = 0;
            u64                                         pending_key = 0;
            u64                                         failed_key = 0;
            std::future<eval_outcome>                   job{};
            std::shared_ptr<std::atomic<bool>>          cancel{};
            f32                                         min_height = 0.0f;
            f32                                         max_height = 0.0f;

            [[nodiscard]] f64 spacing() const noexcept { return std::ldexp(1.0, level); }
            [[nodiscard]] f64 size() const noexcept { return spacing() * QUADS; }
            [[nodiscard]] region vertices() const noexcept;     // QUADS + 1 samples per side
        };

        struct camera_state {
            glm::vec3                                   target{ 0.0f };
            f32                                         distance = 3000.0f;
            f32                                         yaw = 0.7f;
            f32                                         pitch = -0.55f;         // negative: looking down
            f32                                         fov = 50.0f;
        };

        void ensure_renderer();
        void destroy_renderer();

        void update_tiles(const target& what);
        void collect(tile& t);
        void build_mesh(tile& t, const field& data);
        void release(tile& t);
        void submit_instances();
        void submit_lighting();
        void frame_domain();

        void handle_input(bool hovered, bool active);
        [[nodiscard]] GLT::world::camera_snapshot camera() const;
        [[nodiscard]] glm::vec3 forward() const;

        evaluator&                                      m_eval;
        GLT::render::i_scene_renderer*                  m_scene = nullptr;
        glm::uvec2                                      m_extent{ 0, 0 };
        bool                                            m_visible = false;

        std::unordered_map<u64, tile>                   m_tiles{};              // by (level, ix, iy)
        bool                                            m_instances_dirty = true;
        bool                                            m_has_target = false;

        camera_state                                    m_camera{};
        bool                                            m_frame_pending = true;
        glm::dvec2                                      m_domain_min{ 0.0 };
        glm::dvec2                                      m_domain_max{ 0.0 };
        f64                                             m_coverage = 0.0;       // world width of the window

        u32                                             m_running = 0;
        u32                                             m_ready = 0;            // window tiles showing the current state
        std::string                                     m_status{};
    };

}
