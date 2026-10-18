
#include "util/pch.h"
#include "i_debug_draw.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::render {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    void i_debug_draw::line(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color, const f32 lifetime, const bool depth_test) {

        draw_line({ from, to, color, lifetime, depth_test });
    }


    void i_debug_draw::aabb(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color, const f32 lifetime, const bool depth_test) {

        draw_aabb({ min, max, color, lifetime, depth_test });
    }


    // Draws the local-space box [local_min, local_max] transformed by [transform] as an oriented box
    void i_debug_draw::aabb_transformed(const glm::mat4& transform, const glm::vec3& local_min, const glm::vec3& local_max,
        const glm::vec4& color, const f32 lifetime, const bool depth_test) {

        glm::vec3 c[8] = {
            { local_min.x, local_min.y, local_min.z },
            { local_max.x, local_min.y, local_min.z },
            { local_max.x, local_max.y, local_min.z },
            { local_min.x, local_max.y, local_min.z },
            { local_min.x, local_min.y, local_max.z },
            { local_max.x, local_min.y, local_max.z },
            { local_max.x, local_max.y, local_max.z },
            { local_min.x, local_max.y, local_max.z },
        };
        for (auto& v : c)
            v = glm::vec3(transform * glm::vec4(v, 1.0f));

        static constexpr u32 e[12][2] = { {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };
        for (auto& [a, b] : e)
            draw_line({ c[a], c[b], color, lifetime, depth_test });
    }


    // Draws a connected sequence of line segments. If [closed] and there are at least three points, adds the
    // segment from the last back to the first
    void i_debug_draw::polyline(std::span<const glm::vec3> points, const glm::vec4& color, const f32 lifetime, const bool depth_test,
        const bool closed) {

        if (points.size() < 2)
            return;

        for (size_t index = 0; index + 1 < points.size(); index++)
            draw_line({ points[index], points[index + 1], color, lifetime, depth_test });

        if (closed && points.size() > 2)
            draw_line({ points.back(), points.front(), color, lifetime, depth_test });
    }


    // Draws an arrow from -> to: the shaft plus four small barbs at the tip
    void i_debug_draw::arrow(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color, const f32 lifetime, const bool depth_test,
        const f32 head_size) {

        const glm::vec3 axis = to - from;
        const f32 length = glm::length(axis);
        if (length < 1e-6f)
            return;

        const glm::vec3 dir = axis / length;

        // Pick an "up" that isn't parallel to dir
        const glm::vec3 up = (std::abs(glm::dot(dir, glm::vec3(0.f, 1.f, 0.f))) > 0.99f)
            ? glm::vec3(0.f, 0.f, 1.f)
            : glm::vec3(0.f, 1.f, 0.f);

        const glm::vec3 side1 = glm::normalize(glm::cross(dir, up));
        const glm::vec3 side2 = glm::cross(dir, side1);

        const glm::vec3 back = to - dir * head_size;
        const glm::vec3 barb = side1 * (head_size * 0.4f);

        draw_line({ from, to, color, lifetime, depth_test });
        draw_line({ to, back + barb, color, lifetime, depth_test });
        draw_line({ to, back - barb, color, lifetime, depth_test });
        draw_line({ to, back + side2 * (head_size * 0.4f), color, lifetime, depth_test });
        draw_line({ to, back - side2 * (head_size * 0.4f), color, lifetime, depth_test });
    }


    // Draws three axis-aligned lines through [position]; handy for marking a point
    void i_debug_draw::cross(const glm::vec3& position, const f32 size, const glm::vec4& color, const f32 lifetime, const bool depth_test) {

        const glm::vec3 s = glm::vec3(size, 0.f, 0.f);
        const glm::vec3 t = glm::vec3(0.f, size, 0.f);
        const glm::vec3 u = glm::vec3(0.f, 0.f, size);

        draw_line({ position - s, position + s, color, lifetime, depth_test });
        draw_line({ position - t, position + t, color, lifetime, depth_test });
        draw_line({ position - u, position + u, color, lifetime, depth_test });
    }


    // Draws a wireframe sphere as three great circles in the XY, XZ and YZ planes
    void i_debug_draw::sphere(const glm::vec3& center, const f32 radius, const glm::vec4& color, const f32 lifetime, const bool depth_test,
        const u32 segments) {

        if (radius <= 0.f || segments < 3)
            return;

        const f32 two_pi = 6.2831853071795864f;

        for (u32 index = 0; index < segments; index++) {

            const f32 a = static_cast<f32>(index) / static_cast<f32>(segments) * two_pi;
            const f32 b = static_cast<f32>(index + 1) / static_cast<f32>(segments) * two_pi;

            const glm::vec3 xy_a = center + glm::vec3(std::cos(a), std::sin(a), 0.f) * radius;
            const glm::vec3 xy_b = center + glm::vec3(std::cos(b), std::sin(b), 0.f) * radius;
            const glm::vec3 xz_a = center + glm::vec3(std::cos(a), 0.f, std::sin(a)) * radius;
            const glm::vec3 xz_b = center + glm::vec3(std::cos(b), 0.f, std::sin(b)) * radius;
            const glm::vec3 yz_a = center + glm::vec3(0.f, std::cos(a), std::sin(a)) * radius;
            const glm::vec3 yz_b = center + glm::vec3(0.f, std::cos(b), std::sin(b)) * radius;

            draw_line({ xy_a, xy_b, color, lifetime, depth_test });
            draw_line({ xz_a, xz_b, color, lifetime, depth_test });
            draw_line({ yz_a, yz_b, color, lifetime, depth_test });
        }
    }


    // Draws a wireframe capsule: two end circles, N side lines, and quarter-circle arcs on each hemispherical cap
    void i_debug_draw::capsule(const glm::vec3& a, const glm::vec3& b, const f32 radius, const glm::vec4& color, const f32 lifetime,
        const bool depth_test, const u32 segments) {

        const glm::vec3 axis = b - a;
        const f32 length = glm::length(axis);

        if (length < 1e-6f || radius <= 0.f || segments < 3) {
            sphere(a, radius, color, lifetime, depth_test, segments);
            return;
        }

        const glm::vec3 dir = axis / length;
        const glm::vec3 up = (std::abs(glm::dot(dir, glm::vec3(0.f, 1.f, 0.f))) > 0.99f)
            ? glm::vec3(0.f, 0.f, 1.f)
            : glm::vec3(0.f, 1.f, 0.f);

        const glm::vec3 perp1 = glm::normalize(glm::cross(dir, up));
        const glm::vec3 perp2 = glm::cross(dir, perp1);

        const f32 two_pi = 6.2831853071795864f;
        const f32 half_pi = 1.5707963267948966f;

        // Cylinder: end circles + side lines
        for (u32 index = 0; index < segments; index++) {

            const f32 t1 = static_cast<f32>(index)     / static_cast<f32>(segments) * two_pi;
            const f32 t2 = static_cast<f32>(index + 1) / static_cast<f32>(segments) * two_pi;

            const glm::vec3 r1 = perp1 * std::cos(t1) + perp2 * std::sin(t1);
            const glm::vec3 r2 = perp1 * std::cos(t2) + perp2 * std::sin(t2);

            draw_line({ a + r1 * radius, a + r2 * radius, color, lifetime, depth_test });
            draw_line({ b + r1 * radius, b + r2 * radius, color, lifetime, depth_test });
            draw_line({ a + r1 * radius, b + r1 * radius, color, lifetime, depth_test });
        }

        // Hemispherical caps: two meridian half-arcs per end
        const u32 arc_segments = 4;
        for (u32 index = 0; index < arc_segments; index++) {

            const f32 t1 = static_cast<f32>(index)     / static_cast<f32>(arc_segments) * half_pi;
            const f32 t2 = static_cast<f32>(index + 1) / static_cast<f32>(arc_segments) * half_pi;

            draw_line({
                a + perp1 * (std::cos(t1) * radius) - dir * (std::sin(t1) * radius),
                a + perp1 * (std::cos(t2) * radius) - dir * (std::sin(t2) * radius),
                color, lifetime, depth_test });
            draw_line({
                a + perp2 * (std::cos(t1) * radius) - dir * (std::sin(t1) * radius),
                a + perp2 * (std::cos(t2) * radius) - dir * (std::sin(t2) * radius),
                color, lifetime, depth_test });
            draw_line({
                b + perp1 * (std::cos(t1) * radius) + dir * (std::sin(t1) * radius),
                b + perp1 * (std::cos(t2) * radius) + dir * (std::sin(t2) * radius),
                color, lifetime, depth_test });
            draw_line({
                b + perp2 * (std::cos(t1) * radius) + dir * (std::sin(t1) * radius),
                b + perp2 * (std::cos(t2) * radius) + dir * (std::sin(t2) * radius),
                color, lifetime, depth_test });
        }
    }


    void i_debug_draw::cone(const glm::vec3& origin, const glm::vec3& direction, const f32 angle, const f32 distance,
        const glm::vec4& color, const f32 lifetime, const bool depth_test, const u32 segments) {

        const f32 len = glm::length(direction);
        if (len < 1e-6f || distance <= 0.f || angle <= 0.f || angle >= 1.5707963f || segments < 3)
            return;

        const glm::vec3 dir = direction / len;
        const glm::vec3 up = (std::abs(glm::dot(dir, glm::vec3(0.f, 1.f, 0.f))) > 0.99f) 
            ? glm::vec3(0.f, 0.f, 1.f) : glm::vec3(0.f, 1.f, 0.f);
        const glm::vec3 p1 = glm::normalize(glm::cross(dir, up));
        const glm::vec3 p2 = glm::cross(dir, p1);

        const f32 base_radius = distance * std::tan(angle);
        const glm::vec3 base_center = origin + dir * distance;
        const f32 two_pi = 6.2831853071795864f;

        for (u32 index = 0; index < segments; index++) {
            const f32 t1 = static_cast<f32>(index)     / static_cast<f32>(segments) * two_pi;
            const f32 t2 = static_cast<f32>(index + 1) / static_cast<f32>(segments) * two_pi;
            const glm::vec3 r1 = p1 * std::cos(t1) + p2 * std::sin(t1);
            const glm::vec3 r2 = p1 * std::cos(t2) + p2 * std::sin(t2);
            draw_line({ base_center + r1 * base_radius, base_center + r2 * base_radius, color, lifetime, depth_test });
        }

        const u32 spoke_stride = std::max(1u, segments / 4);
        for (u32 index = 0; index < segments; index += spoke_stride) {
            const f32 t = static_cast<f32>(index) / static_cast<f32>(segments) * two_pi;
            const glm::vec3 r = p1 * std::cos(t) + p2 * std::sin(t);
            draw_line({ origin, base_center + r * base_radius, color, lifetime, depth_test });
        }
    }


    void i_debug_draw::highlight_entity(const GLT::world::entity_id id, const glm::vec4& color, const f32 lifetime, const bool depth_test) {

        highlight_entity(std::span<const GLT::world::entity_id>(&id, 1), color, lifetime, depth_test);
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
