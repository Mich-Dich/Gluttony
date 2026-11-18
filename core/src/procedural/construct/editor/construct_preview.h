#pragma once

#include <imgui.h>

#include "procedural/construct/engine/service.h"
#include "world/camera_snapshot.h"

// The 3D view of construct editors: published meshes in their own offscreen scene renderer (sky + sun), an orbit
// camera, and projection helpers so editors can draw overlays (guides, handles) and pick in screen space.
//
// Camera: left drag orbits (when the editor does not take the drag), middle / right drag pans, wheel zooms,
// WASD (+ Q / E) moves while hovered, F frames.



namespace GLT::render {
    class i_scene_renderer;
    class i_debug_draw;
}

namespace GLT::construct::editor {

    // CLASS DECLARATION ===============================================================================================

    class construct_preview {
    public:

        struct camera_state {
            glm::vec3                                   target{ 0.0f };
            f32                                         distance = 8.0f;
            f32                                         yaw = 0.7f;
            f32                                         pitch = -0.45f;         // negative: looking down
            f32                                         fov = 50.0f;            // vertical, degrees
        };

        // What happened in the view this frame (for the editor's picking / dragging)
        struct interaction {
            bool                                        hovered = false;
            bool                                        clicked = false;        // left button pressed on the view
            bool                                        picked = false;         // left button released without dragging (a click)
            bool                                        dragging = false;       // left button held on the view
            ImVec2                                      mouse{};                // screen position
            ImVec2                                      delta{};                // mouse delta this frame
        };

        construct_preview() = default;
        ~construct_preview();

        construct_preview(const construct_preview&) = delete;
        construct_preview& operator=(const construct_preview&) = delete;

        // Draws the view into the current window, filling [size]. [orbit_with_left]: false while the editor drags a
        // handle with the left button. [overlay] is called with the view's draw list clipped to the view
        interaction draw(ImVec2 size, std::span<const published_mesh> meshes, bool orbit_with_left,
                         const std::function<void(ImDrawList&)>& overlay = {});

        // Not shown: the renderer stops (no GPU work), meshes stay
        void set_visible(bool visible);

        void frame(const glm::vec3& lo, const glm::vec3& hi);
        [[nodiscard]] camera_state& camera() noexcept { return m_camera; }

        // Screen position of a world point; empty behind the camera
        [[nodiscard]] std::optional<ImVec2> project(const glm::vec3& world) const;
        // World-space offset that moves a point at [world] by [screen_delta] pixels, parallel to the view
        [[nodiscard]] glm::vec3 screen_delta_to_world(const glm::vec3& world, ImVec2 screen_delta) const;
        [[nodiscard]] glm::vec3 eye() const;
        [[nodiscard]] glm::mat4 view_matrix() const;
        [[nodiscard]] glm::mat4 projection_matrix() const;
        [[nodiscard]] ImVec2 view_origin() const noexcept { return m_origin; }
        [[nodiscard]] ImVec2 view_size() const noexcept { return m_size; }
        // World ray through a screen position: origin, normalized direction
        [[nodiscard]] std::pair<glm::vec3, glm::vec3> ray(ImVec2 screen) const;

        // Lines drawn into the 3D view (depth tested), for overlays that should hide behind meshes. Null without a
        // renderer. Submit every frame from the window's update() (one-frame lifetime)
        [[nodiscard]] GLT::render::i_debug_draw* debug_draw() const;

        // Floor grid at y = 0, metres. Spacing follows the zoom (10 cm / 1 m / 10 m / 100 m ...), faded between levels.
        // Call from update() with the rest of the debug lines
        void set_grid(bool enabled) noexcept { m_grid = enabled; }
        [[nodiscard]] bool grid() const noexcept { return m_grid; }
        void submit_grid();

        // Text shown in the top left corner of the view (status of the evaluation)
        void set_status(std::string text, bool error = false) { m_status = std::move(text); m_status_error = error; }

    private:

        void ensure_renderer();
        void destroy_renderer();
        void submit(std::span<const published_mesh> meshes);
        void handle_input(bool hovered, bool active, bool orbit_with_left);
        [[nodiscard]] GLT::world::camera_snapshot snapshot() const;
        [[nodiscard]] glm::vec3 forward() const;
        [[nodiscard]] glm::mat4 view_projection() const;

        GLT::render::i_scene_renderer*                  m_scene = nullptr;
        glm::uvec2                                      m_extent{ 0, 0 };
        camera_state                                    m_camera{};
        ImVec2                                          m_origin{};             // top left of the view on screen
        ImVec2                                          m_size{ 1.0f, 1.0f };
        u64                                             m_submitted_hash = 0;
        bool                                            m_lighting_sent = false;
        std::string                                     m_status{};
        bool                                            m_status_error = false;
        bool                                            m_grid = true;
        std::string                                     m_grid_label{};
        ImVec2                                          m_press_position{};
        bool                                            m_pressed = false;
        bool                                            m_moved = false;
    };

}
