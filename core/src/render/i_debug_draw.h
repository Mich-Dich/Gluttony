
#pragma once

#include "util/pch.h"
#include "plugin_system/i_plugin.h"
#include "render/i_renderer.h"
#include "world/entity.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::render {

    // CONSTANTS =======================================================================================================

    // Passing this as [lifetime] keeps the primitive / watched mesh visible until clear() is called (or the same
    // handle is re-set with a different lifetime). It exists so callers don't have to reason about float overflow.
    inline constexpr f32                DEBUG_DRAW_FOREVER = std::numeric_limits<f32>::max();

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // @brief A single line segment in world space
    struct debug_line_primitive {

        glm::vec3                       from{};
        glm::vec3                       to{};
        glm::vec4                       color{ 1.f, 1.f, 1.f, 1.f };
        f32                             lifetime{ 0.f };        // seconds; 0 = render for a single frame
        bool                            depth_test{ true };
    };


    // @brief A single triangle in world space
    struct debug_triangle_primitive {

        glm::vec3                       a{};
        glm::vec3                       b{};
        glm::vec3                       c{};
        glm::vec4                       color{ 1.f, 1.f, 1.f, 1.f };
        f32                             lifetime{ 0.f };
        bool                            depth_test{ true };
    };


    // @brief An axis-aligned box in world space
    struct debug_aabb_primitive {

        glm::vec3                       min{};
        glm::vec3                       max{};
        glm::vec4                       color{ 1.f, 1.f, 1.f, 1.f };
        f32                             lifetime{ 0.f };
        bool                            depth_test{ true };
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Immediate-mode debug drawing capability, implemented by renderer plugins
    //
    // Not part of [i_renderer_plugin] - renderers opt in by inheriting from this interface. Callers reach it via
    // [i_plugin::as<i_debug_draw>()] or the helper [debug_draw::get_ref()]. If the active renderer doesn't implement it,
    // get_ref() returns nullptr
    //
    // Primitives are submitted from the update thread; the renderer snapshots and renders them during the next
    // begin_frame(). [lifetime] > 0 makes a primitive persist across frames; lifetime == 0 renders it once
    class i_debug_draw {
    public:

        virtual ~i_debug_draw() = default;

        // submission --------------------------------------------------------------------------------------------------

        virtual void draw_line(const debug_line_primitive& line) = 0;

        virtual void draw_triangle(const debug_triangle_primitive& tri) = 0;

        virtual void draw_aabb(const debug_aabb_primitive& box) = 0;

        // convenience overloads ---------------------------------------------------------------------------------------

        void line(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color = { 1.f, 1.f, 1.f, 1.f },
            const f32 lifetime = 0.f, const bool depth_test = true);


        void aabb(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color = { 1.f, 1.f, 1.f, 1.f },
            const f32 lifetime = 0.f, const bool depth_test = true);


        // Draws the local-space box [local_min, local_max] transformed by [transform] as an oriented box
        void aabb_transformed(const glm::mat4& transform, const glm::vec3& local_min, const glm::vec3& local_max,
            const glm::vec4& color = { 1.f, 1.f, 1.f, 1.f }, const f32 lifetime = 0.f, const bool depth_test = true);


        // Draws a connected sequence of line segments. If [closed] and there are at least three points, adds the
        // segment from the last back to the first
        void polyline(std::span<const glm::vec3> points, const glm::vec4& color = { 1.f, 1.f, 1.f, 1.f },
            const f32 lifetime = 0.f, const bool depth_test = true, const bool closed = false);


        // Draws an arrow from -> to: the shaft plus four small barbs at the tip
        void arrow(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color = { 1.f, 1.f, 1.f, 1.f },
            const f32 lifetime = 0.f, const bool depth_test = true, const f32 head_size = 0.2f);


        // Draws three axis-aligned lines through [position]; handy for marking a point
        void cross(const glm::vec3& position, const f32 size = 1.0f, const glm::vec4& color = { 1.f, 1.f, 1.f, 1.f },
            const f32 lifetime = 0.f, const bool depth_test = true);


        // Draws a wireframe sphere as three great circles in the XY, XZ and YZ planes
        void sphere(const glm::vec3& center, const f32 radius, const glm::vec4& color = { 1.f, 1.f, 1.f, 1.f },
            const f32 lifetime = 0.f, const bool depth_test = true, const u32 segments = 16);


        // Draws a wireframe capsule: two end circles, N side lines, and quarter-circle arcs on each hemispherical cap
        void capsule(const glm::vec3& a, const glm::vec3& b, const f32 radius, const glm::vec4& color = { 1.f, 1.f, 1.f, 1.f },
            const f32 lifetime = 0.f, const bool depth_test = true, const u32 segments = 12);

        // lifecycle ---------------------------------------------------------------------------------------------------

        // Removes every pending primitive and watched mesh
        virtual void clear() = 0;

        // watched meshes ----------------------------------------------------------------------------------------------

        virtual void draw_mesh_aabb(std::span<const GLT::world::entity_id> ids, const glm::vec4& color = { 1.f, 1.f, 1.f, 1.f },
            const f32 lifetime = 0.f, const bool depth_test = true) = 0;


        // Single entity. Covers the 90% case: "outline this one selected thing"
        void draw_mesh_aabb(const GLT::world::entity_id id, const glm::vec4& color = { 1.f, 1.f, 1.f, 1.f }, const f32 lifetime = 0.f,
            const bool depth_test = true);
        
        // @brief Draws an AABB around every instance of [mesh_handle] currently in the submitted scene
        //
        // The mesh's local bounds are looked up from the renderer's cache (populated when the mesh was uploaded),
        // then transformed per-instance by the instance's model matrix. If the mesh isn't in the submitted scene, nothing
        // is drawn
        //
        // Calling this repeatedly with the same handle updates the existing entry (colour, lifetime, depth-test) rather
        // than adding a duplicate. It is safe to call from any thread
        //
        // @param mesh_handle  Asset handle of the mesh to outline
        // @param color        Line colour
        // @param lifetime     Seconds the watch entry survives after the last call. Pass [DEBUG_DRAW_FOREVER] to keep it
        //                     around until clear() or a subsequent set_draw_mesh_aabb() with a different lifetime
        // @param depth_test   Whether the AABB respects the depth buffer (false = always-on-top)
        virtual void set_draw_mesh_aabb(GLT::asset::handle mesh_handle, const glm::vec4& color = { 1.f, 1.f, 1.f, 1.f },
            const f32 lifetime = 0.f, const bool depth_test = true) = 0;

    };

}
