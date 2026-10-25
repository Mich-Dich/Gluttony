#pragma once

#include "util/pch.h"
#include "asset/mesh.h"
#include "platform/i_window.h"
#include "world/camera_snapshot.h"
#include "world/light_snapshot.h"
#include "render/i_debug_draw.h"
#include "render/render_mode.h"
#include "render/type.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::platform {
    class i_window_plugin;
}

namespace GLT::render {

    // CONSTANTS =======================================================================================================

    using procedural_mesh_id = u32;
    inline constexpr procedural_mesh_id                 INVALID_PROCEDURAL_MESH = 0;

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
        u64                                             procedural_mesh_capacity_bytes = 8ull * 1024 * 1024;

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


    // Update contract per class:
    //   immutable  — update_procedural_mesh may be called exactly once. A second call returns already_finalized.
    //   occasional — any number of updates. Buffers may be reallocated between calls.
    //   streaming  — not implemented in v1; update_procedural_mesh returns not_supported.
    enum class mesh_update_class : u8 {

        immutable = 0,
        occasional,
        streaming,
    };


    struct procedural_mesh_error {

        enum class code : u32 {
            none = 0,
            not_found,
            not_supported,
            already_finalized,
            invalid_input,
            out_of_memory,
            backend,
        }                                               kind = code::none;
        std::string                                     message{};
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

        // procedural meshes -----------------------------------------------------------

        // Allocates a scene-local procedural mesh slot. Returns 0 on failure (slot table full).
        [[nodiscard]] virtual procedural_mesh_id create_procedural_mesh(mesh_update_class cls) = 0;


        // Uploads vertices + indices + binds a material. Preconditions:
        //   - `vertices.size() > 0 && indices.size() > 0`
        //   - `vertices` layout == GLT::asset::mesh::vertex
        //   - every index is in [0, vertices.size())
        // A second call on an immutable slot returns already_finalized. Passing INVALID_HANDLE for material falls back to the
        // default material
        virtual std::expected<void, procedural_mesh_error> update_procedural_mesh(procedural_mesh_id id,
            std::span<const GLT::asset::mesh::vertex> vertices, std::span<const u32> indices, 
            GLT::asset::handle material = INVALID_HANDLE) = 0;


        // Releases GPU buffers and material. Implicitly called on scene renderer destruction.
        virtual void destroy_procedural_mesh(procedural_mesh_id id) = 0;

    };

}
