
#include "util/pch.h"
#include "terrain_preview_3d.h"

#include <numeric>

#include <application.h>
#include <render/i_scene_renderer.h>
#include <world/light_snapshot.h>
#include <world/environment_snapshot.h>

#include "util/ui/panel_collection.h"

#include "world/chunk_mesh.h"



namespace GLT::terrain {

    namespace {

        constexpr u32                                   MAX_JOBS = 6;           // tile evaluations in flight
        constexpr f32                                   UV_SCALE = 1.0f / 32.0f;
        constexpr f32                                   MIN_DISTANCE = 5.0f;
        constexpr f32                                   MAX_DISTANCE = 2.0e5f;
        constexpr f32                                   COVERAGE_PER_DISTANCE = 3.0f;   // window width / camera distance
        constexpr i32                                   MIN_LEVEL = -3;         // finest spacing: 1/8 world unit

        // Sun from the upper left (light_snapshot convention: the direction the light travels)
        const glm::vec3                                 SUN_DIRECTION = glm::normalize(glm::vec3(-0.45f, -0.65f, -0.35f));

        [[nodiscard]] u64 cell_key(i32 level, i64 ix, i64 iy) {
            return hash_combine(hash_combine(hash_mix(static_cast<u64>(static_cast<u32>(level)) + 0x9E37u), static_cast<u64>(ix)), static_cast<u64>(iy));
        }

        struct rect {
            f64 x0, y0, x1, y1;
            [[nodiscard]] bool overlaps(const rect& o) const noexcept { return x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1; }
        };

    }

    // CLASS IMPLEMENTATION ============================================================================================

    region terrain_preview_3d::tile::vertices() const noexcept {

        region r{};
        r.x0 = ix * static_cast<i64>(QUADS);
        r.y0 = iy * static_cast<i64>(QUADS);
        r.width = QUADS + 1;                            // shared edge vertices with the neighbours
        r.height = QUADS + 1;
        r.spacing = spacing();
        return r;
    }


    terrain_preview_3d::terrain_preview_3d(evaluator& eval) : m_eval(eval) {}


    terrain_preview_3d::~terrain_preview_3d() {

        for (auto& [key, t] : m_tiles)
            release(t);
        m_tiles.clear();
        destroy_renderer();
    }


    void terrain_preview_3d::set_visible(bool visible) {

        m_visible = visible;
        if (m_scene)
            m_scene->set_active(visible);
    }

    // CLASS PRIVATE ===================================================================================================

    void terrain_preview_3d::ensure_renderer() {

        if (m_scene)
            return;

        GLT::render::offscreen_target_desc offscreen{};
        offscreen.extent = glm::uvec2(512, 512);
        GLT::render::scene_renderer_desc desc{
            .target = offscreen,
            .clear_color = glm::vec4(0.09f, 0.10f, 0.12f, 1.0f),
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
        m_instances_dirty = true;
    }


    void terrain_preview_3d::destroy_renderer() {

        if (!m_scene)
            return;
        GLT::application::get().destroy_scene_renderer(m_scene);
        m_scene = nullptr;
        m_extent = { 0, 0 };
    }


    void terrain_preview_3d::release(tile& t) {

        // Jobs reference only the evaluator (outlives every editor window): cancel, don't wait
        if (t.cancel)
            t.cancel->store(true);
        if (t.mesh != GLT::render::INVALID_PROCEDURAL_MESH) {
            GLT::render::procedural_meshes().destroy(t.mesh);
            t.mesh = GLT::render::INVALID_PROCEDURAL_MESH;
            m_instances_dirty = true;
        }
    }


    void terrain_preview_3d::collect(tile& t) {

        if (!t.job.valid() || t.job.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;

        eval_outcome outcome = t.job.get();
        t.cancel.reset();
        if (!outcome) {
            if (outcome.error() != "cancelled") {
                t.failed_key = t.pending_key;           // the same state would fail again
                m_status = outcome.error();
            }
            t.pending_key = 0;
            return;
        }
        if (outcome->data)
            build_mesh(t, *outcome->data);
        t.shown_key = t.pending_key;
        t.pending_key = 0;
    }


    void terrain_preview_3d::build_mesh(tile& t, const field& data) {

        // The vertices; [data] has one more sample around them for the normals
        chunk_mesh_result built = build_chunk_mesh(data, t.vertices(), { .skirt_depth = 0.0f, .uv_scale = UV_SCALE });
        GLT::render::procedural_mesh_data& mesh = built.data;
        const f32 lo = built.min_height;
        const f32 hi = built.max_height;

        auto& store = GLT::render::procedural_meshes();
        if (t.mesh == GLT::render::INVALID_PROCEDURAL_MESH) {
            t.mesh = store.create({ .update_class = GLT::render::mesh_update_class::occasional,
                .detail = GLT::render::procedural_detail::none, .debug_name = "terrain preview chunk" });
            t.frames_with_mesh = 0;
            m_instances_dirty = true;
        }
        if (auto updated = store.update(t.mesh, std::move(mesh)); !updated)
            LOG(warn, "terrain preview: chunk mesh update failed: {}", updated.error().message)
        t.min_height = lo;
        t.max_height = hi;
    }


    void terrain_preview_3d::update_tiles(const target& what) {

        m_domain_min = what.domain_min;
        m_domain_max = what.domain_max;
        const bool has_target = what.graph && what.node != INVALID_NODE_ID;
        if (has_target != m_has_target) {
            m_has_target = has_target;
            m_instances_dirty = true;
        }

        u64 state = 0;
        if (has_target) {
            state = hash_combine(what.revision, what.node);
            state = hash_combine(state, hash_string(what.pin));
            state = hash_combine(state, what.seed);
            state = hash_combine(state, what.parameters_hash);
        }

        // ---- the window: TILES x TILES tiles under the orbit point, spacing from the camera distance ---------------------
        m_coverage = std::max(static_cast<f64>(m_camera.distance) * COVERAGE_PER_DISTANCE, 64.0);
        const i32 level = std::max(MIN_LEVEL, static_cast<i32>(std::ceil(std::log2(m_coverage / static_cast<f64>(TILES * QUADS)))));
        const f64 tile_size = std::ldexp(1.0, level) * QUADS;
        const i64 center_x = static_cast<i64>(std::floor(static_cast<f64>(m_camera.target.x) / tile_size));
        const i64 center_y = static_cast<i64>(std::floor(static_cast<f64>(m_camera.target.z) / tile_size));
        const i64 first_x = center_x - static_cast<i64>(TILES / 2);
        const i64 first_y = center_y - static_cast<i64>(TILES / 2);

        for (auto& [key, t] : m_tiles)
            t.desired = false;

        std::vector<tile*> window;
        window.reserve(TILES * TILES);
        for (i64 y = first_y; y < first_y + static_cast<i64>(TILES); ++y)
            for (i64 x = first_x; x < first_x + static_cast<i64>(TILES); ++x) {
                auto [it, inserted] = m_tiles.try_emplace(cell_key(level, x, y));
                tile& t = it->second;
                if (inserted) {
                    t.level = level;
                    t.ix = x;
                    t.iy = y;
                }
                t.desired = true;
                window.push_back(&t);
            }

        // Nearest to the orbit point first
        const glm::dvec2 focus(m_camera.target.x, m_camera.target.z);
        auto distance_to_focus = [&](const tile* t) {
            const glm::dvec2 center((static_cast<f64>(t->ix) + 0.5) * t->size(), (static_cast<f64>(t->iy) + 0.5) * t->size());
            return glm::length(center - focus);
        };
        std::sort(window.begin(), window.end(), [&](const tile* a, const tile* b) { return distance_to_focus(a) < distance_to_focus(b); });

        // ---- finished jobs, jobs no longer needed ----------------------------------------------------------------------
        m_running = 0;
        for (auto& [key, t] : m_tiles) {
            if (t.mesh != GLT::render::INVALID_PROCEDURAL_MESH)
                ++t.frames_with_mesh;
            collect(t);
            if (t.job.valid() && !t.desired && t.cancel)
                t.cancel->store(true);                  // left the window: stop at its next node
            m_running += t.job.valid() ? 1u : 0u;
        }

        // ---- start jobs ----------------------------------------------------------------------------------------------------
        m_ready = 0;
        for (tile* t : window) {
            if (!has_target)
                continue;
            const u64 key = std::max<u64>(hash_combine(state, cell_key(t->level, t->ix, t->iy)), 1);
            if (key == t->shown_key) {
                ++m_ready;
                continue;
            }
            if (t->job.valid() || key == t->failed_key || m_running >= MAX_JOBS)
                continue;

            eval_request request{};
            request.graph = what.graph;
            request.node = what.node;
            request.pin = what.pin;
            request.seed = what.seed;
            request.parameters = what.parameters;
            request.area = t->vertices();
            request.area.x0 -= 1;                       // one more sample around it: normals at the edges
            request.area.y0 -= 1;
            request.area.width += 2;
            request.area.height += 2;

            t->cancel = std::make_shared<std::atomic<bool>>(false);
            t->job = m_eval.evaluate_async(std::move(request), t->cancel);
            t->pending_key = key;
            ++m_running;
        }

        // ---- drop tiles outside the window once the window covers their area -------------------------------------------
        auto area_of = [](const tile& t) {
            const f64 size = t.size();
            return rect{ static_cast<f64>(t.ix) * size, static_cast<f64>(t.iy) * size, static_cast<f64>(t.ix + 1) * size, static_cast<f64>(t.iy + 1) * size };
        };
        for (auto it = m_tiles.begin(); it != m_tiles.end(); ) {
            tile& old = it->second;
            if (old.desired) {
                ++it;
                continue;
            }
            // Keep it while a window tile over its area is still without a mesh (no holes while zooming / panning)
            const rect area = area_of(old);
            bool still_needed = false;
            if (has_target && old.mesh != GLT::render::INVALID_PROCEDURAL_MESH)
                for (const tile* t : window)
                    if ((t->mesh == GLT::render::INVALID_PROCEDURAL_MESH || t->frames_with_mesh < 4) && area.overlaps(area_of(*t))) {
                        still_needed = true;
                        break;
                    }
            if (still_needed) {
                ++it;
                continue;
            }
            release(old);
            it = m_tiles.erase(it);
        }
    }


    void terrain_preview_3d::submit_instances() {

        if (!m_scene || !m_instances_dirty)
            return;
        m_instances_dirty = false;

        std::vector<GLT::asset::mesh::instance> instances;
        if (m_has_target)
            for (const auto& [key, t] : m_tiles)
                if (t.mesh != GLT::render::INVALID_PROCEDURAL_MESH) {
                    GLT::asset::mesh::instance instance{};
                    instance.procedural_mesh = t.mesh;
                    instance.transform = glm::mat4(1.0f);
                    instances.push_back(std::move(instance));
                }
        m_scene->submit_scene(instances);
    }


    void terrain_preview_3d::submit_lighting() {

        if (!m_scene)
            return;

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


    void terrain_preview_3d::frame_domain() {

        const glm::dvec2 center = (m_domain_min + m_domain_max) * 0.5;
        const f64 extent = std::max(std::max(m_domain_max.x - m_domain_min.x, m_domain_max.y - m_domain_min.y), 64.0);

        f32 lo = 0.0f, hi = 0.0f;
        bool any = false;
        for (const auto& [key, t] : m_tiles)
            if (t.mesh != GLT::render::INVALID_PROCEDURAL_MESH) {
                lo = any ? std::min(lo, t.min_height) : t.min_height;
                hi = any ? std::max(hi, t.max_height) : t.max_height;
                any = true;
            }

        m_camera.target = glm::vec3(static_cast<f32>(center.x), (lo + hi) * 0.5f, static_cast<f32>(center.y));
        m_camera.distance = std::clamp(static_cast<f32>(extent) * 0.95f, MIN_DISTANCE, MAX_DISTANCE);
        m_camera.yaw = 0.7f;
        m_camera.pitch = -0.55f;
    }


    glm::vec3 terrain_preview_3d::forward() const {

        return glm::vec3(std::cos(m_camera.pitch) * std::sin(m_camera.yaw), std::sin(m_camera.pitch), std::cos(m_camera.pitch) * std::cos(m_camera.yaw));
    }


    GLT::world::camera_snapshot terrain_preview_3d::camera() const {

        const glm::vec3 position = m_camera.target - forward() * m_camera.distance;
        GLT::world::camera_snapshot cam{};
        cam.position = position;
        cam.view = glm::lookAt(position, m_camera.target, glm::vec3(0.0f, 1.0f, 0.0f));
        cam.fov = m_camera.fov;
        cam.near_plane = std::max(0.1f, m_camera.distance * 0.002f);
        cam.far_plane = m_camera.distance * 2.0f + static_cast<f32>(m_coverage) * 1.5f;
        return cam;
    }


    void terrain_preview_3d::handle_input(bool hovered, bool active) {

        ImGuiIO& io = ImGui::GetIO();
        const glm::vec3 fwd = forward();
        const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 ground_forward = glm::normalize(glm::vec3(fwd.x, 0.0f, fwd.z));

        if (hovered && io.MouseWheel != 0.0f) {
            m_camera.distance = std::clamp(m_camera.distance * std::pow(0.88f, io.MouseWheel), MIN_DISTANCE, MAX_DISTANCE);
            io.MouseWheel = 0.0f;
        }

        // Left: orbit
        if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            m_camera.yaw -= io.MouseDelta.x * 0.006f;
            m_camera.pitch = std::clamp(m_camera.pitch - io.MouseDelta.y * 0.006f, -1.5f, 0.2f);
        }

        // Middle / right: pan over the ground (screen-sized steps at any distance)
        if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f))) {
            const f32 scale = m_camera.distance * 0.0016f;
            m_camera.target += (-right * io.MouseDelta.x + ground_forward * io.MouseDelta.y) * scale;
        }

        // WASD + Q / E while hovered: move the orbit point
        if (hovered && !io.WantTextInput) {
            const f32 speed = m_camera.distance * 0.8f * io.DeltaTime * (io.KeyShift ? 3.0f : 1.0f);
            glm::vec3 move(0.0f);
            if (ImGui::IsKeyDown(ImGuiKey_W)) move += ground_forward;
            if (ImGui::IsKeyDown(ImGuiKey_S)) move -= ground_forward;
            if (ImGui::IsKeyDown(ImGuiKey_D)) move += right;
            if (ImGui::IsKeyDown(ImGuiKey_A)) move -= right;
            if (ImGui::IsKeyDown(ImGuiKey_E)) move.y += 1.0f;
            if (ImGui::IsKeyDown(ImGuiKey_Q)) move.y -= 1.0f;
            m_camera.target += move * speed;
            if (ImGui::IsKeyPressed(ImGuiKey_F, false))
                m_frame_pending = true;
        }
    }

    // CLASS PUBLIC ====================================================================================================

    void terrain_preview_3d::draw(const target& what, ImVec2 size) {

        ensure_renderer();
        set_visible(true);

        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 extent(std::max(size.x, 32.0f), std::max(size.y, 32.0f));
        const ImVec2 p1(p0.x + extent.x, p0.y + extent.y);

        if (!m_scene) {
            ImGui::Dummy(extent);
            ImGui::GetWindowDrawList()->AddText(ImVec2(p0.x + 8.0f, p0.y + 8.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                "3D preview unavailable: the renderer could not create a preview target");
            return;
        }

        const glm::uvec2 wanted(static_cast<u32>(extent.x), static_cast<u32>(extent.y));
        if (wanted != m_extent) {
            m_scene->resize(wanted);
            m_extent = wanted;
        }

        // Framing first (the window follows the camera), heights refine it once chunks exist
        if (m_frame_pending) {
            m_domain_min = what.domain_min;
            m_domain_max = what.domain_max;
            frame_domain();
            m_frame_pending = false;
        }

        ImGui::InvisibleButton("##terrain_3d", extent, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        handle_input(hovered, active);

        update_tiles(what);
        m_scene->set_active_camera(camera());
        submit_lighting();
        submit_instances();

        // ---- image + overlays ------------------------------------------------------------------------------------------
        constexpr f32 ROUNDING = 6.0f;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (void* image = m_scene->get_output_image())
            draw->AddImageRounded(reinterpret_cast<ImTextureID>(image), p0, p1, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE, ROUNDING);
        else
            draw->AddRectFilled(p0, p1, IM_COL32(16, 17, 20, 255), ROUNDING);

        const f32 pad = 8.0f;
        auto pill = [&](ImVec2 pos, const char* text, ImU32 color) {
            const ImVec2 text_size = ImGui::CalcTextSize(text);
            draw->AddRectFilled(pos, ImVec2(pos.x + text_size.x + 12.0f, pos.y + text_size.y + 6.0f), IM_COL32(10, 10, 12, 190), 4.0f);
            draw->AddText(ImVec2(pos.x + 6.0f, pos.y + 3.0f), color, text);
        };

        draw->PushClipRect(p0, p1, true);
        if (!m_has_target) {
            const char* hint = "Add a Surface output (or select a heightfield node)";
            const ImVec2 hint_size = ImGui::CalcTextSize(hint);
            draw->AddText(ImVec2(p0.x + (extent.x - hint_size.x) * 0.5f, p0.y + (extent.y - hint_size.y) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
        } else if (m_ready < TILES * TILES) {
            const std::string text = std::format("evaluating {} / {} chunks", m_ready, TILES * TILES);
            pill(ImVec2(p0.x + pad, p0.y + pad), text.c_str(), IM_COL32(255, 255, 255, 220));
        } else if (hovered) {
            const f64 spacing = m_coverage / static_cast<f64>(TILES * QUADS);
            const std::string text = std::format("{:.0f} m across, ~{:.2g} m per sample", m_coverage, spacing);
            pill(ImVec2(p0.x + pad, p0.y + pad), text.c_str(), IM_COL32(200, 200, 205, 220));
        }
        if (!m_status.empty() && m_ready < TILES * TILES && m_running == 0)
            pill(ImVec2(p0.x + pad, p1.y - pad - ImGui::GetTextLineHeight() - 6.0f), m_status.c_str(), IM_COL32(255, 120, 100, 255));
        if (hovered && !active)
            pill(ImVec2(p0.x + pad, p1.y - pad - ImGui::GetTextLineHeight() - 6.0f),
                "LMB orbit   MMB / RMB pan   wheel zoom (wider area)   WASD QE move   F frame", IM_COL32(200, 200, 205, 220));
        draw->PopClipRect();
        draw->AddRect(p0, p1, ImGui::GetColorU32(ImGuiCol_Border), ROUNDING);
    }

}
