#pragma once

#include "util/pch.h"
#include "asset/mesh.h"
#include "platform/i_window.h"
#include "world/camera_snapshot.h"
#include "world/light_snapshot.h"
#include "world/environment_snapshot.h"
#include "render/i_debug_draw.h"
#include "render/render_mode.h"
#include "render/type.h"
#include "render/procedural_mesh.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::platform {
    class i_window_plugin;
}

namespace GLT::render {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct surface_target_desc {

        GLT::platform::window_handle                    window = GLT::platform::INVALID_WINDOW_HANDLE;
    };


    struct offscreen_target_desc {

        glm::uvec2                                      extent{ 512, 512 };
    };


    using scene_target = std::variant<surface_target_desc, offscreen_target_desc>;


    struct scene_renderer_desc {

        scene_target                                    target;
        std::optional<glm::vec4>                        clear_color;
        u32                                             frames_in_flight = 3;

        // When false, this surface renderer will NOT touch ImGui at all — no backend init, no begin/end frame, no imgui pass
        // Required for secondary windows: the ImGui GLFW backend can only be bound to one native window per context, so a
        // second surface renderer that called imgui_init() would steal the binding from the primary and break editor input
        bool enable_imgui = true;
    };


    struct scene_renderer_error {

        enum class code : u32 {
            none = 0,
            target_invalid,
            feature_unsupported,
            backend,
        }                                               kind = code::none;
        std::string                                     message{};
    };


    struct render_error {

        enum class code : u32 {
            none = 0,
            target_out_of_date,
            target_lost,
            submission_rejected,
            backend,
        }                                               kind = code::none;
        std::string                                     message{};
    };


    // @brief What one finished frame of a scene renderer cost on the GPU (i_scene_renderer::last_frame_stats())
    //
    // GPU results arrive a few frames after the frame was submitted. [camera_position], [render_mode] and [extent] are
    // those of the measured frame, so a caller can tell which view the numbers belong to without counting frames
    struct frame_stats {

        u64                                             serial = 0;             // number of frames measured so far; 0 = none yet / not supported
        f32                                             gpu_time_ms = 0.0f;     // every pass of this renderer
        f32                                             geometry_time_ms = 0.0f;// of that: culling and drawing scene geometry (depth, shading, shadows)
        u32                                             draw_calls = 0;         // draws left after culling, main view
        u32                                             triangles = 0;          // triangles of those draws
        u32                                             cluster_draws = 0;      // of the draws: virtual geometry clusters
        u32                                             cluster_triangles = 0;  // of the triangles: in those clusters
        glm::vec3                                       camera_position{ 0.0f };
        u32                                             render_mode = 0;
        glm::uvec2                                      extent{ 0, 0 };
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class i_scene_renderer {
    public:

        virtual ~i_scene_renderer() = default;


        virtual void submit_scene(std::span<const GLT::asset::mesh::instance> instances) = 0;


        virtual void submit_lights(std::span<const GLT::world::light_snapshot> lights) = 0;


        // World-level environment (sky / sun / ambient). Renderers that don't support it can ignore it
        virtual void submit_environment(const GLT::world::environment_snapshot& /*environment*/) {}


        virtual void set_active_camera(const GLT::world::camera_snapshot& camera) = 0;


        virtual void update(f32 delta_time) = 0;


        [[nodiscard]] virtual std::expected<void, render_error> render(u64 frame_number) = 0;


        virtual void resize(glm::uvec2 extent) = 0;


        [[nodiscard]] virtual void* get_output_image() const = 0;


        [[nodiscard]] virtual glm::uvec2 get_extent() const = 0;


        [[nodiscard]] virtual bool is_offscreen() const = 0;


        [[nodiscard]] virtual GLT::render::i_debug_draw* get_debug_draw() = 0;


        [[nodiscard]] virtual std::span<const GLT::render::render_mode_info> supported_modes() const = 0;


        virtual void set_render_mode(u32 mode_id) = 0;


        [[nodiscard]] virtual u32 get_render_mode() const = 0;


        // @brief Enables or disables rendering without destroying resources.
        //
        // While inactive, `update()` and `render()` return immediately and no GPU work is submitted.
        // The last submitted frame remains on the target. Reactivating causes the next frame to
        // render normally. Safe to toggle any time.
        virtual void set_active(bool active) = 0;


        [[nodiscard]] virtual bool is_active() const = 0;

        // Procedural meshes are not created here: generators use GLT::render::procedural_meshes() (render/procedural_mesh.h)
        // and reference them from mesh::instance::procedural_mesh. Every renderer plugin mirrors that store to the GPU

        // statistics ------------------------------------------------------------------

        // Measured cost of the most recent frame whose GPU results are in (see frame_stats). Renderers without GPU
        // timing return the default (serial 0)
        [[nodiscard]] virtual frame_stats last_frame_stats() const { return {}; }

    };

}
