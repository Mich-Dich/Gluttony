#include "util/pch.h"

#include "procedural/construct/editor/construct_preview.h"

#include "application.h"
#include "render/i_scene_renderer.h"
#include "render/i_debug_draw.h"
#include "world/light_snapshot.h"
#include "world/environment_snapshot.h"



namespace GLT::construct::editor {

    // CONSTANTS =======================================================================================================

    namespace {

        constexpr f32                                   MIN_DISTANCE = 0.2f;
        constexpr f32                                   MAX_DISTANCE = 5.0e4f;
        const glm::vec3                                 SUN_DIRECTION = glm::normalize(glm::vec3(-0.45f, -0.75f, -0.35f));

    }

    // CLASS IMPLEMENTATION ============================================================================================

    construct_preview::~construct_preview() { destroy_renderer(); }


    void construct_preview::set_visible(bool visible) {

        if (m_scene)
            m_scene->set_active(visible);
    }


    void construct_preview::ensure_renderer() {

        if (m_scene)
            return;
        GLT::render::offscreen_target_desc offscreen{};
        offscreen.extent = glm::uvec2(512, 512);
        GLT::render::scene_renderer_desc desc{
            .target = offscreen,
            .clear_color = glm::vec4(0.11f, 0.12f, 0.14f, 1.0f),
        };
        m_scene = GLT::application::get().create_scene_renderer(desc);
        if (!m_scene)
            return;
        m_extent = offscreen.extent;
        for (const auto& mode : m_scene->supported_modes())
            if (mode.is_default) {
                m_scene->set_render_mode(mode.id);
                break;
            }
        m_submitted_hash = 0;
        m_lighting_sent = false;
    }


    void construct_preview::destroy_renderer() {

        if (!m_scene)
            return;
        GLT::application::get().destroy_scene_renderer(m_scene);
        m_scene = nullptr;
        m_extent = { 0, 0 };
    }


    void construct_preview::submit(std::span<const published_mesh> meshes) {

        u64 h = meshes.size();
        for (const published_mesh& m : meshes)
            h = hash_combine(h, m.mesh);
        if (h != m_submitted_hash) {
            m_submitted_hash = h;
            std::vector<GLT::asset::mesh::instance> instances;
            for (const published_mesh& m : meshes) {
                GLT::asset::mesh::instance instance{};
                instance.procedural_mesh = m.mesh;
                instance.transform = glm::mat4(1.0f);
                instances.push_back(std::move(instance));
            }
            m_scene->submit_scene(instances);
        }

        if (!m_lighting_sent) {
            m_lighting_sent = true;
            GLT::world::environment_snapshot environment{};
            environment.has_sky = true;
            environment.has_sun = true;
            environment.sun_direction = SUN_DIRECTION;
            environment.sun_color = glm::vec3(1.0f, 0.96f, 0.9f);
            environment.sun_intensity = 1.0f;
            m_scene->submit_environment(environment);

            GLT::world::light_snapshot sun{};
            sun.type = GLT::world::light_type::directional;
            sun.direction = SUN_DIRECTION;
            sun.color = environment.sun_color;
            sun.intensity = environment.sun_intensity;
            m_scene->submit_lights(std::span<const GLT::world::light_snapshot>(&sun, 1));
        }
    }


    glm::vec3 construct_preview::forward() const {

        return glm::vec3(std::cos(m_camera.pitch) * std::sin(m_camera.yaw), std::sin(m_camera.pitch),
                         std::cos(m_camera.pitch) * std::cos(m_camera.yaw));
    }


    glm::vec3 construct_preview::eye() const { return m_camera.target - forward() * m_camera.distance; }


    GLT::world::camera_snapshot construct_preview::snapshot() const {

        GLT::world::camera_snapshot cam{};
        cam.position = eye();
        cam.view = glm::lookAt(cam.position, m_camera.target, glm::vec3(0.0f, 1.0f, 0.0f));
        cam.fov = m_camera.fov;
        cam.near_plane = std::max(0.01f, m_camera.distance * 0.002f);
        cam.far_plane = m_camera.distance * 20.0f + 100.0f;
        return cam;
    }


    glm::mat4 construct_preview::view_matrix() const { return snapshot().view; }


    glm::mat4 construct_preview::projection_matrix() const {

        const GLT::world::camera_snapshot cam = snapshot();
        const f32 aspect = m_size.y > 0.0f ? m_size.x / m_size.y : 1.0f;
        return glm::perspective(glm::radians(cam.fov), aspect, cam.near_plane, cam.far_plane);
    }


    glm::mat4 construct_preview::view_projection() const { return projection_matrix() * view_matrix(); }


    std::pair<glm::vec3, glm::vec3> construct_preview::ray(ImVec2 screen) const {

        const glm::mat4 inv = glm::inverse(view_projection());
        const f32 x = (screen.x - m_origin.x) / std::max(m_size.x, 1.0f) * 2.0f - 1.0f;
        const f32 y = 1.0f - (screen.y - m_origin.y) / std::max(m_size.y, 1.0f) * 2.0f;
        glm::vec4 a = inv * glm::vec4(x, y, -1.0f, 1.0f), b = inv * glm::vec4(x, y, 1.0f, 1.0f);
        a /= a.w;
        b /= b.w;
        return { glm::vec3(a), glm::normalize(glm::vec3(b - a)) };
    }


    GLT::render::i_debug_draw* construct_preview::debug_draw() const { return m_scene ? m_scene->get_debug_draw() : nullptr; }


    void construct_preview::submit_grid() {

        m_grid_label.clear();
        GLT::render::i_debug_draw* debug = debug_draw();
        if (!m_grid || !debug)
            return;

        // Spacing from the distance to the ground: the minor lines are 1/10 of the major ones and fade out as they
        // get dense. Kept faint: a hint of scale, not a feature of the scene
        const glm::vec3 from = eye();
        const f32 height = std::abs(from.y);
        const f32 reach = std::max(std::min(m_camera.distance, height * 3.0f + m_camera.distance * 0.3f), 0.05f);
        const f32 level = std::log10(reach * 0.1f);
        const f32 minor = std::pow(10.0f, std::floor(level));
        const f32 major = minor * 10.0f;
        const f32 fade = 1.0f - (level - std::floor(level));               // 1: minor lines just appeared big, 0: about to vanish
        const i32 lines = 10;                                               // major lines each side
        const f32 extent = major * static_cast<f32>(lines);
        const glm::vec2 centre(std::round(m_camera.target.x / major) * major, std::round(m_camera.target.z / major) * major);

        const glm::vec4 major_color(0.62f, 0.64f, 0.70f, 0.30f);
        const glm::vec4 minor_color(0.55f, 0.57f, 0.62f, 0.22f * fade);
        for (i32 i = -lines * 10; i <= lines * 10; ++i) {
            const bool is_major = i % 10 == 0;
            if (!is_major && fade < 0.05f)
                continue;
            const glm::vec4& color = is_major ? major_color : minor_color;
            const f32 x = centre.x + static_cast<f32>(i) * minor, z = centre.y + static_cast<f32>(i) * minor;
            const bool axis_x = std::abs(z) < minor * 0.01f, axis_z = std::abs(x) < minor * 0.01f;
            debug->line({ x, 0.0f, centre.y - extent }, { x, 0.0f, centre.y + extent },
                        axis_z ? glm::vec4(0.35f, 0.45f, 0.95f, 0.55f) : color, 0.0f, true);
            debug->line({ centre.x - extent, 0.0f, z }, { centre.x + extent, 0.0f, z },
                        axis_x ? glm::vec4(0.95f, 0.35f, 0.35f, 0.55f) : color, 0.0f, true);
        }

        auto metric = [](f32 v) {
            if (v >= 1000.0f) return std::format("{:g} km", v / 1000.0f);
            if (v >= 1.0f)    return std::format("{:g} m", v);
            if (v >= 0.01f)   return std::format("{:g} cm", v * 100.0f);
            return std::format("{:g} mm", v * 1000.0f);
        };
        m_grid_label = fade >= 0.05f ? std::format("grid  {}  /  {}", metric(major), metric(minor)) : std::format("grid  {}", metric(major));
    }


    std::optional<ImVec2> construct_preview::project(const glm::vec3& world) const {

        const glm::vec4 clip = view_projection() * glm::vec4(world, 1.0f);
        if (clip.w <= 1e-5f)
            return std::nullopt;
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        return ImVec2(m_origin.x + (ndc.x * 0.5f + 0.5f) * m_size.x, m_origin.y + (0.5f - ndc.y * 0.5f) * m_size.y);
    }


    glm::vec3 construct_preview::screen_delta_to_world(const glm::vec3& world, ImVec2 screen_delta) const {

        const glm::vec3 fwd = forward();
        const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 up = glm::cross(right, fwd);
        const f32 depth = std::max(glm::dot(world - eye(), fwd), 0.01f);
        const f32 world_per_pixel = 2.0f * depth * std::tan(glm::radians(m_camera.fov) * 0.5f) / std::max(m_size.y, 1.0f);
        return (right * screen_delta.x - up * screen_delta.y) * world_per_pixel;
    }


    void construct_preview::frame(const glm::vec3& lo, const glm::vec3& hi) {

        const glm::vec3 centre = (lo + hi) * 0.5f;
        const f32 radius = std::max(glm::length(hi - lo) * 0.5f, 0.5f);
        m_camera.target = centre;
        m_camera.distance = std::clamp(radius / std::sin(glm::radians(m_camera.fov) * 0.5f) * 1.05f, MIN_DISTANCE, MAX_DISTANCE);
    }


    void construct_preview::handle_input(bool hovered, bool active, bool orbit_with_left) {

        ImGuiIO& io = ImGui::GetIO();
        const glm::vec3 fwd = forward();
        const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 up = glm::cross(right, fwd);

        if (hovered && io.MouseWheel != 0.0f) {
            m_camera.distance = std::clamp(m_camera.distance * std::pow(0.88f, io.MouseWheel), MIN_DISTANCE, MAX_DISTANCE);
            io.MouseWheel = 0.0f;
        }
        if (active && orbit_with_left && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            m_camera.yaw -= io.MouseDelta.x * 0.006f;
            m_camera.pitch = std::clamp(m_camera.pitch - io.MouseDelta.y * 0.006f, -1.55f, 1.55f);
        }
        if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)))
            m_camera.target += screen_delta_to_world(m_camera.target, ImVec2(-io.MouseDelta.x, -io.MouseDelta.y));
        if (hovered && !io.WantTextInput) {
            const f32 speed = m_camera.distance * 0.8f * io.DeltaTime * (io.KeyShift ? 3.0f : 1.0f);
            glm::vec3 move(0.0f);
            if (ImGui::IsKeyDown(ImGuiKey_W)) move += fwd;
            if (ImGui::IsKeyDown(ImGuiKey_S)) move -= fwd;
            if (ImGui::IsKeyDown(ImGuiKey_D)) move += right;
            if (ImGui::IsKeyDown(ImGuiKey_A)) move -= right;
            if (ImGui::IsKeyDown(ImGuiKey_E)) move += up;
            if (ImGui::IsKeyDown(ImGuiKey_Q)) move -= up;
            m_camera.target += move * speed;
        }
    }


    construct_preview::interaction construct_preview::draw(ImVec2 size, std::span<const published_mesh> meshes, bool orbit_with_left,
                                                           const std::function<void(ImDrawList&)>& overlay) {

        interaction result{};
        ensure_renderer();
        set_visible(true);

        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 extent(std::max(size.x, 32.0f), std::max(size.y, 32.0f));
        const ImVec2 p1(p0.x + extent.x, p0.y + extent.y);
        m_origin = p0;
        m_size = extent;

        ImGui::InvisibleButton("##construct_view", extent, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
        result.hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        result.clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        result.dragging = active && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        result.mouse = ImGui::GetIO().MousePos;
        result.delta = ImGui::GetIO().MouseDelta;
        if (result.clicked) {
            m_pressed = true;
            m_moved = false;
            m_press_position = result.mouse;
        }
        if (m_pressed && std::hypot(result.mouse.x - m_press_position.x, result.mouse.y - m_press_position.y) > 4.0f)
            m_moved = true;
        if (m_pressed && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            result.picked = !m_moved && result.hovered;
            m_pressed = false;
        }
        handle_input(result.hovered, active, orbit_with_left);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        constexpr f32 ROUNDING = 6.0f;
        if (!m_scene) {
            draw->AddRectFilled(p0, p1, IM_COL32(16, 17, 20, 255), ROUNDING);
            draw->AddText(ImVec2(p0.x + 8.0f, p0.y + 8.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                          "3D preview unavailable: the renderer could not create a preview target");
            return result;
        }

        const glm::uvec2 wanted(static_cast<u32>(extent.x), static_cast<u32>(extent.y));
        if (wanted != m_extent) {
            m_scene->resize(wanted);
            m_extent = wanted;
        }
        m_scene->set_active_camera(snapshot());
        submit(meshes);

        if (void* image = m_scene->get_output_image())
            draw->AddImageRounded(reinterpret_cast<ImTextureID>(image), p0, p1, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE, ROUNDING);
        else
            draw->AddRectFilled(p0, p1, IM_COL32(16, 17, 20, 255), ROUNDING);

        draw->PushClipRect(p0, p1, true);
        if (overlay)
            overlay(*draw);
        auto pill = [&](ImVec2 pos, const char* text, ImU32 color) {
            const ImVec2 text_size = ImGui::CalcTextSize(text);
            draw->AddRectFilled(pos, ImVec2(pos.x + text_size.x + 12.0f, pos.y + text_size.y + 6.0f), IM_COL32(10, 10, 12, 190), 4.0f);
            draw->AddText(ImVec2(pos.x + 6.0f, pos.y + 3.0f), color, text);
        };
        if (!m_status.empty())
            pill(ImVec2(p0.x + 8.0f, p0.y + 8.0f), m_status.c_str(), m_status_error ? IM_COL32(255, 120, 100, 255) : IM_COL32(220, 220, 225, 230));
        if (!m_grid_label.empty()) {
            const ImVec2 text_size = ImGui::CalcTextSize(m_grid_label.c_str());
            pill(ImVec2(p1.x - text_size.x - 20.0f, p1.y - text_size.y - 14.0f), m_grid_label.c_str(), IM_COL32(170, 172, 180, 200));
        }
        if (result.hovered && !active)
            pill(ImVec2(p0.x + 8.0f, p1.y - 8.0f - ImGui::GetTextLineHeight() - 6.0f),
                 "LMB click select / drag orbit   MMB / RMB pan   wheel zoom   WASD QE move   F frame", IM_COL32(200, 200, 205, 220));
        draw->PopClipRect();
        draw->AddRect(p0, p1, ImGui::GetColorU32(ImGuiCol_Border), ROUNDING);
        return result;
    }

}
