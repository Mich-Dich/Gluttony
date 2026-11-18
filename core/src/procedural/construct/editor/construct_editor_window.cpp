#include "util/pch.h"

#include "procedural/construct/editor/construct_editor_window.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include "application.h"
#include "asset/i_asset_registry.h"
#include "config/project.h"
#include "render/i_debug_draw.h"
#include "util/ui/panel_collection.h"
#include "world/i_world.h"

#include "procedural/construct/editor/integration.h"
#include "procedural/construct/engine/static_mesh_export.h"
#include "procedural/construct/self_test.h"



namespace GLT::construct::editor {

    // CONSTANTS =======================================================================================================

    namespace {

        constexpr f32                                   HANDLE_RADIUS = 5.0f;
        constexpr f32                                   PICK_RADIUS = 9.0f;
        constexpr f32                                   EDGE_PICK_DISTANCE = 5.0f;
        constexpr size_t                                MAX_UNDO = 200;

        const ImU32                                     COLOR_GUIDE = IM_COL32(120, 200, 255, 150);
        const ImU32                                     COLOR_SELECTED = IM_COL32(255, 190, 70, 255);
        const ImU32                                     COLOR_HANDLE = IM_COL32(255, 255, 255, 230);
        const ImVec4                                    TEXT_WARN{ 1.0f, 0.72f, 0.28f, 1.0f };
        const ImVec4                                    TEXT_ERROR{ 1.0f, 0.42f, 0.36f, 1.0f };
        const ImVec4                                    TEXT_OK{ 0.45f, 0.8f, 0.45f, 1.0f };

        void tooltip(const char* text) {
            if (text && *text && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", text);
        }

        f32 segment_distance(ImVec2 p, ImVec2 a, ImVec2 b) {
            const glm::vec2 pa(p.x - a.x, p.y - a.y), ba(b.x - a.x, b.y - a.y);
            const f32 len2 = glm::dot(ba, ba);
            const f32 t = len2 > 0.0f ? std::clamp(glm::dot(pa, ba) / len2, 0.0f, 1.0f) : 0.0f;
            return glm::length(pa - ba * t);
        }

        const char* shape_label(const guide& g) {
            switch (g.shape.index()) {
                case 0:  return "volume";
                case 1:  return "spline";
                case 2:  return "plane";
                default: return "points";
            }
        }

        geometry::curve make_curve(const curve_guide& c) {
            geometry::curve out(c.type, c.points, c.closed);
            for (const auto& [id, values] : c.point_attributes)
                for (u32 i = 0; i < values.size(); ++i)
                    out.set_point_attribute(id, i, values[i]);
            return out;
        }

        // Value of a per point attribute column (created on demand)
        std::vector<attribute_value>& point_column(curve_guide& c, attribute_id id, f32 fallback) {
            for (auto& [cid, values] : c.point_attributes)
                if (cid == id) {
                    values.resize(c.points.size(), fallback);
                    return values;
                }
            c.point_attributes.emplace_back(id, std::vector<attribute_value>(c.points.size(), fallback));
            return c.point_attributes.back().second;
        }

        ImU32 to_color(const glm::vec3& c, f32 alpha = 1.0f) {
            return IM_COL32(static_cast<int>(c.r * 255.0f), static_cast<int>(c.g * 255.0f), static_cast<int>(c.b * 255.0f), static_cast<int>(alpha * 255.0f));
        }

        // Ray / box (slabs): entry distance or none
        std::optional<f32> ray_box(const glm::vec3& o, const glm::vec3& d, const glm::vec3& lo, const glm::vec3& hi) {
            f32 t0 = 0.0f, t1 = std::numeric_limits<f32>::max();
            for (int a = 0; a < 3; ++a) {
                if (std::abs(d[a]) < 1e-12f) {
                    if (o[a] < lo[a] || o[a] > hi[a])
                        return std::nullopt;
                    continue;
                }
                f32 n = (lo[a] - o[a]) / d[a], f = (hi[a] - o[a]) / d[a];
                if (n > f)
                    std::swap(n, f);
                t0 = std::max(t0, n);
                t1 = std::min(t1, f);
                if (t0 > t1)
                    return std::nullopt;
            }
            return t0;
        }

        // Ray / triangle (Moller-Trumbore), both sides
        std::optional<f32> ray_triangle(const glm::vec3& o, const glm::vec3& d, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
            const glm::vec3 e1 = b - a, e2 = c - a;
            const glm::vec3 p = glm::cross(d, e2);
            const f32 det = glm::dot(e1, p);
            if (std::abs(det) < 1e-12f)
                return std::nullopt;
            const f32 inv = 1.0f / det;
            const glm::vec3 s = o - a;
            const f32 u = glm::dot(s, p) * inv;
            if (u < 0.0f || u > 1.0f)
                return std::nullopt;
            const glm::vec3 q = glm::cross(s, e1);
            const f32 v = glm::dot(d, q) * inv;
            if (v < 0.0f || u + v > 1.0f)
                return std::nullopt;
            const f32 t = glm::dot(e2, q) * inv;
            return t > 0.0f ? std::optional<f32>(t) : std::nullopt;
        }

        // Small curve editor for profile settings: drag points, double click adds one, right click removes one
        bool profile_editor(const char* id, profile_curve& curve, f32 width, f32 height, f32 y_min, f32 y_max) {

            bool changed = false;
            ImGui::PushID(id);
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            const ImVec2 size(std::max(width, 60.0f), height);
            const ImVec2 p1(p0.x + size.x, p0.y + size.y);
            ImGui::InvisibleButton("##profile", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const f32 span = std::max(y_max - y_min, 1e-4f);
            constexpr f32 PAD = 6.0f;
            auto to_screen = [&](const glm::vec2& v) {
                return ImVec2(p0.x + PAD + v.x * (size.x - 2 * PAD), p1.y - PAD - (v.y - y_min) / span * (size.y - 2 * PAD));
            };
            auto from_screen = [&](ImVec2 s) {
                return glm::vec2(std::clamp((s.x - p0.x - PAD) / (size.x - 2 * PAD), 0.0f, 1.0f),
                                 std::clamp(y_min + (p1.y - PAD - s.y) / (size.y - 2 * PAD) * span, y_min, y_max));
            };

            draw->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f);
            for (int i = 1; i < 4; ++i) {
                const f32 x = p0.x + PAD + (size.x - 2 * PAD) * static_cast<f32>(i) / 4.0f;
                draw->AddLine(ImVec2(x, p0.y + 2), ImVec2(x, p1.y - 2), ImGui::GetColorU32(ImGuiCol_Border, 0.35f));
            }
            if (y_min < 1.0f && y_max > 1.0f) {
                const ImVec2 one = to_screen({ 0.0f, 1.0f });
                draw->AddLine(ImVec2(p0.x + 2, one.y), ImVec2(p1.x - 2, one.y), ImGui::GetColorU32(ImGuiCol_Border, 0.6f));
            }
            ImVec2 prev = to_screen({ 0.0f, curve.evaluate(0.0f) });
            for (int i = 1; i <= 48; ++i) {
                const f32 x = static_cast<f32>(i) / 48.0f;
                const ImVec2 cur = to_screen({ x, curve.evaluate(x) });
                draw->AddLine(prev, cur, ImGui::GetColorU32(ImGuiCol_PlotLines), 2.0f);
                prev = cur;
            }

            ImGuiStorage* storage = ImGui::GetStateStorage();
            const ImGuiID drag_id = ImGui::GetID("##drag");
            int dragging = storage->GetInt(drag_id, -1);
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            int near = -1;
            f32 near_d = 8.0f;
            for (size_t i = 0; i < curve.points.size(); ++i) {
                const ImVec2 s = to_screen(curve.points[i]);
                const f32 d = std::hypot(s.x - mouse.x, s.y - mouse.y);
                if (d < near_d) {
                    near_d = d;
                    near = static_cast<int>(i);
                }
            }
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                dragging = near;
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && near < 0) {
                curve.points.push_back(from_screen(mouse));
                curve.sanitize();
                changed = true;
            }
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && near > 0 && near + 1 < static_cast<int>(curve.points.size())) {
                curve.points.erase(curve.points.begin() + near);
                changed = true;
                near = -1;
            }
            if (active && dragging >= 0 && dragging < static_cast<int>(curve.points.size())) {
                glm::vec2 v = from_screen(mouse);
                const size_t i = static_cast<size_t>(dragging);
                if (i == 0)
                    v.x = 0.0f;
                else if (i + 1 == curve.points.size())
                    v.x = 1.0f;
                else
                    v.x = std::clamp(v.x, curve.points[i - 1].x + 0.01f, curve.points[i + 1].x - 0.01f);
                if (v != curve.points[i]) {
                    curve.points[i] = v;
                    changed = true;
                }
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                dragging = -1;
            storage->SetInt(drag_id, dragging);

            for (size_t i = 0; i < curve.points.size(); ++i) {
                const bool hot = static_cast<int>(i) == near || static_cast<int>(i) == dragging;
                draw->AddCircleFilled(to_screen(curve.points[i]), hot ? 5.0f : 3.5f,
                                      hot ? IM_COL32(255, 200, 90, 255) : ImGui::GetColorU32(ImGuiCol_Text));
            }
            if (hovered) {
                const glm::vec2 v = from_screen(mouse);
                if (dragging >= 0 && dragging < static_cast<int>(curve.points.size()))
                    ImGui::SetTooltip("%.2f -> %.3f", curve.points[static_cast<size_t>(dragging)].x, curve.points[static_cast<size_t>(dragging)].y);
                else if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("at %.2f: %.3f\ndrag points, double click adds, right click removes", v.x, curve.evaluate(v.x));
            }
            ImGui::PopID();
            return changed;
        }

        std::string format_count(u64 n) {
            if (n >= 1000000) return std::format("{:.1f}M", static_cast<f64>(n) / 1e6);
            if (n >= 10000)   return std::format("{:.0f}k", static_cast<f64>(n) / 1e3);
            if (n >= 1000)    return std::format("{:.1f}k", static_cast<f64>(n) / 1e3);
            return std::to_string(n);
        }

        // "%.2f m" style formats for settings with units
        std::string number_format(const setting_desc& d, bool integer) {
            const std::string unit = d.unit.empty() ? std::string() : " " + d.unit;
            if (integer)
                return "%d" + unit;
            const f32 range = d.max > d.min ? d.max - d.min : 10.0f;
            return (range >= 50.0f ? "%.1f" : (range >= 2.0f ? "%.2f" : "%.3f")) + unit;
        }

    }

    // CLASS IMPLEMENTATION ============================================================================================

    construct_editor_window::construct_editor_window(std::filesystem::path asset_path, GLT::asset::asset_ref asset)
        : m_path(std::move(asset_path)), m_asset(std::move(asset)) {

        // The data is taken on the first frame (take() runs the packs' upgrade functions and starts an evaluation)
        auto* registry = GLT::asset::registry::get_ptr();
        const auto* a = registry && m_asset.valid() ? dynamic_cast<const construct_asset*>(registry->data(m_asset.get())) : nullptr;
        if (a) {
            m_working = *a->data();
            m_settings = a->preview;
            m_saved_hash = content_hash(m_working);
            m_saved_seed = m_settings.seed;
            if (m_settings.camera_distance > 0.0f) {
                m_preview.camera().target = m_settings.camera_target;
                m_preview.camera().distance = m_settings.camera_distance;
                m_preview.camera().yaw = m_settings.camera_yaw;
                m_preview.camera().pitch = m_settings.camera_pitch;
                m_framed = true;
            }
        } else {
            m_path.clear();
            m_asset = {};
            m_saved_hash = ~0ull;                           // never saved
        }
    }


    construct_editor_window::~construct_editor_window() {

        remove_from_world();
        if (m_job.valid())
            m_job.wait();                                   // runs pack code: let it finish before the window goes
        unpublish(m_meshes);
    }


    std::string construct_editor_window::title() const {

        return m_path.empty() ? std::string("Construct editor") : "Construct: " + m_path.stem().generic_string();
    }


    int construct_editor_window::window_flags() const {

        return ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    }


    publish_settings construct_editor_window::preview_publish_settings() const {

        publish_settings s{};
        s.update_class = GLT::render::mesh_update_class::occasional;
        s.detail = GLT::render::procedural_detail::none;
        s.debug_name = "construct preview";
        return s;
    }


    // what the packs supply -------------------------------------------------------------------------------------------

    std::shared_ptr<const registry_snapshot> construct_editor_window::registry_view() const { return packs().snapshot(); }


    bool construct_editor_window::pack_active(std::string_view pack) const {

        return pack == "core" || m_working.packs.empty() || std::ranges::find(m_working.packs, pack) != m_working.packs.end();
    }


    bool construct_editor_window::upgrade(construct_data& data) const {

        bool changed = false;
        const auto snap = registry_view();
        for (const auto& p : snap->packs)
            if (p.desc->upgrade && (data.packs.empty() || std::ranges::find(data.packs, p.desc->name) != data.packs.end()))
                changed |= p.desc->upgrade(data);
        return changed;
    }


    generator_instance construct_editor_window::make_stage(const generator_desc& type, u32 id) const {

        generator_instance g{};
        g.id = id;
        g.type = type.name;
        g.label = type.display;
        if (type.takes_input)
            for (auto it = m_working.generators.rbegin(); it != m_working.generators.rend(); ++it)
                if (type.accepts.empty() || std::ranges::find(type.accepts, it->type) != type.accepts.end()) {
                    g.inputs = { it->id };
                    break;
                }
        if (type.on_add)
            type.on_add(g, m_working);
        const std::string base = g.label;
        u32 n = 1;
        while (std::ranges::any_of(m_working.generators, [&](const generator_instance& o) { return o.label == g.label; }))
            g.label = std::format("{} {}", base, ++n);
        return g;
    }


    // One row for a whole-element attribute of [set], typed by the attribute's schema (number, integer, toggle)
    bool construct_editor_window::draw_attribute_row(attribute_set& set, attribute_id id) {

        const auto snap = registry_view();
        const attribute_desc* d = snap->find_attribute(id);
        const std::string label = d && !d->display.empty() ? d->display : std::string(id.str());
        const char* tip = d && !d->tooltip.empty() ? d->tooltip.c_str() : nullptr;
        const attribute_value def = d ? d->default_value : attribute_value{ 0.0f };
        const attribute_value* current = set.find(id);
        const bool bounded = d && d->max > d->min;
        bool changed = false;

        if (std::holds_alternative<bool>(def)) {
            bool v = current ? as_number(*current) != 0.0 : std::get<bool>(def);
            const bool fallback = std::get<bool>(def);
            if ((changed = GLT::UI::property_toggle(label.c_str(), v, tip, &fallback)))
                v == fallback ? set.erase(id) : set.set(id, v);
        } else if (std::holds_alternative<i32>(def)) {
            i32 v = current ? static_cast<i32>(as_number(*current)) : std::get<i32>(def);
            const i32 fallback = std::get<i32>(def);
            if (bounded)
                changed = GLT::UI::property_slider(label.c_str(), v, static_cast<i32>(d->min), static_cast<i32>(d->max), "%d", tip, &fallback);
            else
                changed = GLT::UI::property_custom(label.c_str(), [&](f32 width) { ImGui::SetNextItemWidth(width); return ImGui::DragInt("##v", &v, 0.2f); },
                                                   tip, v == fallback, [&] { v = fallback; });
            if (changed)
                v == fallback ? set.erase(id) : set.set(id, v);
        } else {
            const f32 fallback = static_cast<f32>(as_number(def));
            f32 v = current ? static_cast<f32>(as_number(*current)) : fallback;
            const f32 speed = bounded ? (d->max - d->min) / 400.0f : 0.01f;
            if ((changed = GLT::UI::property_drag(label.c_str(), v, speed, bounded ? d->min : 0.0f, bounded ? d->max : 0.0f, "%.3f", tip, &fallback))) {
                if (bounded)
                    v = std::clamp(v, d->min, d->max);
                v == fallback ? set.erase(id) : set.set(id, v);
            }
        }
        return changed;
    }

    // data / history --------------------------------------------------------------------------------------------------

    namespace {

        u64 evaluation_key(u64 data_hash, u64 seed, u32 step) {
            return hash_combine(hash_combine(hash_combine(data_hash, seed), packs().generation()), step);
        }

        // Pipeline stage names of the steps (pipeline_step order); the last runs everything
        constexpr std::array<std::string_view, 6>      STEP_STAGES{ "core.interpret", "core.generate", "core.expand", "core.realize",
                                                                    "core.finish", "" };
        constexpr std::array<const char*, 6>            STEP_LABELS{ "Guides", "Generate", "Expand", "Realize", "Finish", "Output" };

    }


    void construct_editor_window::take(construct_data data, bool keep_history) {

        upgrade(data);
        if (keep_history && m_committed) {
            m_undo.push_back(m_committed);
            if (m_undo.size() > MAX_UNDO)
                m_undo.erase(m_undo.begin());
            m_redo.clear();
        }
        m_working = std::move(data);
        m_committed = std::make_shared<const construct_data>(m_working);
        m_committed_hash = content_hash(*m_committed);
        m_snapshot = m_committed;
        m_snapshot_hash = m_committed_hash;
        m_selection = {};
        m_point = -1;
        m_drag = {};
        m_current_stage = m_working.generators.empty() ? 0 : m_working.generators.front().id;
        m_hidden_stages.clear();
        request_evaluation();
    }


    bool construct_editor_window::interacting() const {

        return ImGui::IsAnyItemActive() || m_drag.guide >= 0 || m_gizmo_using;
    }


    bool construct_editor_window::dirty() const {

        return m_snapshot_hash != m_saved_hash || m_settings.seed != m_saved_seed;
    }


    void construct_editor_window::sync_working() {

        const u64 h = content_hash(m_working);
        if (h != m_snapshot_hash) {
            m_snapshot = std::make_shared<const construct_data>(m_working);
            m_snapshot_hash = h;
            request_evaluation();
        }
        // A finished edit (no widget / handle / gizmo held any more) becomes an undo step
        if (m_snapshot_hash != m_committed_hash && !interacting()) {
            m_undo.push_back(m_committed);
            if (m_undo.size() > MAX_UNDO)
                m_undo.erase(m_undo.begin());
            m_redo.clear();
            m_committed = m_snapshot;
            m_committed_hash = m_snapshot_hash;
        }
    }


    const guide* construct_editor_window::selected_guide() const noexcept {
        if (m_selection.kind != selection_kind::guide || m_selection.guide < 0 || m_selection.guide >= static_cast<i32>(m_working.guides.size()))
            return nullptr;
        return &m_working.guides[static_cast<size_t>(m_selection.guide)];
    }

    void construct_editor_window::undo() {

        sync_working();
        if (m_undo.empty())
            return;
        m_redo.push_back(m_committed);
        m_committed = m_undo.back();
        m_undo.pop_back();
        m_committed_hash = content_hash(*m_committed);
        m_working = *m_committed;
        m_snapshot = m_committed;
        m_snapshot_hash = m_committed_hash;
        m_drag = {};
        if (m_selection.kind == selection_kind::guide && m_selection.guide >= static_cast<i32>(m_working.guides.size()))
            m_selection = {};
        request_evaluation();
    }


    void construct_editor_window::redo() {

        sync_working();
        if (m_redo.empty())
            return;
        m_undo.push_back(m_committed);
        m_committed = m_redo.back();
        m_redo.pop_back();
        m_committed_hash = content_hash(*m_committed);
        m_working = *m_committed;
        m_snapshot = m_committed;
        m_snapshot_hash = m_committed_hash;
        m_drag = {};
        if (m_selection.kind == selection_kind::guide && m_selection.guide >= static_cast<i32>(m_working.guides.size()))
            m_selection = {};
        request_evaluation();
    }

    // evaluation / preview -------------------------------------------------------------------------------------------

    void construct_editor_window::request_evaluation() {

        const u64 key = evaluation_key(m_snapshot_hash, m_settings.seed, static_cast<u32>(m_step));
        if (m_job.valid() || key == m_result_key)           // running: poll_evaluation() starts the newest when it is done
            return;
        m_job_key = key;
        evaluation_request request{};
        request.data = m_snapshot;
        request.seed = m_settings.seed;
        request.editor_data = true;
        request.stop_after = std::string(STEP_STAGES[static_cast<size_t>(m_step)]);
        m_job = evaluate_async(std::move(request));
    }


    void construct_editor_window::poll_evaluation() {

        if (m_job.valid() && m_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            m_result = m_job.get();
            m_result_key = m_job_key;
            m_has_result = true;

            m_element_index.clear();
            for (u32 i = 0; i < m_result.elements.size(); ++i)
                m_element_index.emplace(m_result.elements[i].path, i);
            m_products.assign(m_result.products.size(), {});
            for (size_t i = 0; i < m_result.products.size(); ++i) {
                const product& p = m_result.products[i];
                product_info& info = m_products[i];
                info.stage = p.stage;
                info.lo = glm::vec3(std::numeric_limits<f32>::max());
                info.hi = glm::vec3(-std::numeric_limits<f32>::max());
                if (const auto* mp = std::get_if<mesh_product>(&p.data)) {
                    const glm::mat4 m = p.placement.matrix_relative_to(m_working.origin);
                    for (const glm::vec3& v : mp->mesh.positions()) {
                        const glm::vec3 w(m * glm::vec4(v, 1.0f));
                        info.lo = glm::min(info.lo, w);
                        info.hi = glm::max(info.hi, w);
                    }
                }
            }
            if (m_selection.kind == selection_kind::element && !find_element(m_selection.element))
                m_selection = {};
            if (m_result.stopped_after.empty())
                m_meshes = publish(m_result, preview_publish_settings(), m_meshes);        // Output: the final meshes
            else if (m_step >= pipeline_step::realize)
                publish_preview();                                                      // Realize / Finish: products per stage
            else {
                unpublish(m_meshes);                                                    // earlier: no meshes yet
                m_meshes.clear();
            }
            if (m_world_linked)
                update_world_link();
        }
        if (!m_job.valid() && evaluation_key(m_snapshot_hash, m_settings.seed, static_cast<u32>(m_step)) != m_result_key)
            request_evaluation();

        if (m_working.generators.empty() && m_working.guides.empty())
            m_preview.set_status("Empty construct: start from a preset (right panel or File > New)");
        else if (m_job.valid() && !m_has_result)
            m_preview.set_status("generating ...");
        else if (m_has_result) {
            u32 triangles = 0, errors = 0, warnings = 0;
            for (const output_result& o : m_result.outputs)
                triangles += o.triangle_count();
            for (const diagnostic& d : m_result.diagnostics) {
                errors += d.level == severity::error ? 1u : 0u;
                warnings += d.level == severity::warning ? 1u : 0u;
            }
            f64 ms = 0.0;
            for (const stage_timing& t : m_result.timings)
                ms += t.milliseconds;
            std::string text = std::format("{} elements   {} triangles   {:.0f} ms", format_count(m_result.elements.size()), format_count(triangles), ms);
            if (errors || warnings)
                text += std::format("   {} error(s), {} warning(s)", errors, warnings);
            if (!m_result.stopped_after.empty())
                text += std::format("   stopped after {}", m_result.stopped_after);
            if (m_job.valid())
                text += "   (updating)";
            m_preview.set_status(std::move(text), errors > 0);
        }
    }


    void construct_editor_window::publish_preview() {

        // One procedural mesh per stage (what the render outputs take), so stages can be shown / hidden without a rebuild
        std::map<u32, std::vector<const product*>> by_stage;
        for (const product& p : m_result.products) {
            if (!std::holds_alternative<mesh_product>(p.data))
                continue;
            const bool rendered = std::ranges::any_of(m_result.outputs, [&](const output_result& o) {
                return o.desc.purpose == output_purpose::render && o.desc.include.matches(p);
            });
            if (rendered)
                by_stage[p.stage].push_back(&p);
        }
        evaluation_result grouped{};
        for (const auto& [stage, list] : by_stage) {
            output_result o{};
            o.desc.name = std::format("stage:{}", stage);
            o.desc.purpose = output_purpose::render;
            o.parts.push_back(merge_products(list, m_working.origin));
            grouped.outputs.push_back(std::move(o));
        }
        m_meshes = publish(grouped, preview_publish_settings(), m_meshes);
    }


    i32 construct_editor_window::stage_index(u32 stage) const {

        for (size_t i = 0; i < m_working.generators.size(); ++i)
            if (m_working.generators[i].id == stage)
                return static_cast<i32>(i);
        return -1;
    }


    bool construct_editor_window::stage_visible(u32 stage) const {

        if (m_step == pipeline_step::output)                // the final meshes are merged: stage filters do not apply
            return true;
        if (m_hidden_stages.contains(stage))
            return false;
        if (m_stage_view == stage_view::all || m_current_stage == 0)
            return true;
        if (m_stage_view == stage_view::only_current)
            return stage == m_current_stage;
        const i32 si = stage_index(stage);
        return stage == 0 || (si >= 0 && si <= stage_index(m_current_stage));
    }


    bool construct_editor_window::stage_pickable(u32 stage) const {

        if (!stage_visible(stage) || m_pick == pick_filter::guides)
            return false;
        return m_pick == pick_filter::everything || stage == m_current_stage;
    }


    const generator_desc* construct_editor_window::stage_type(const generator_instance& g) const {

        const auto snap = packs().snapshot();
        const auto* r = snap->find_generator(g.type);
        return r ? r->desc.get() : nullptr;                 // the descriptor is shared: it outlives the snapshot
    }


    glm::vec3 construct_editor_window::stage_color(u32 stage) const {

        for (const generator_instance& g : m_working.generators)
            if (g.id == stage)
                if (const generator_desc* d = stage_type(g))
                    return d->color;
        return glm::vec3(0.6f, 0.75f, 1.0f);
    }


    const element_summary* construct_editor_window::find_element(const element_path& path) const {

        const auto it = m_element_index.find(path);
        return it != m_element_index.end() && it->second < m_result.elements.size() ? &m_result.elements[it->second] : nullptr;
    }


    const stage_stats* construct_editor_window::find_stats(u32 stage) const {

        if (!m_has_result)
            return nullptr;
        for (const stage_stats& s : m_result.stages)
            if (s.id == stage)
                return &s;
        return nullptr;
    }

    // saving ----------------------------------------------------------------------------------------------------------

    void construct_editor_window::message(std::string text, bool error) {

        if (error)
            LOG(error, "construct editor: {}", text)
        else
            LOG(info, "construct editor: {}", text)
        m_message = std::move(text);
        m_message_error = error;
        m_message_time = ImGui::GetTime();
    }


    void construct_editor_window::save() {

        if (m_path.empty() || !m_asset.valid())
            open_dialog(dialog::save_construct);
        else
            save_construct_to(m_path);
    }


    bool construct_editor_window::save_construct_to(const std::filesystem::path& content_path) {

        auto* registry = GLT::asset::registry::get_ptr();
        if (!registry) {
            m_dialog_error = "No asset registry";
            return false;
        }

        GLT::asset::asset_ref target = content_path == m_path && m_asset.valid() ? m_asset : GLT::asset::asset_ref{};
        if (!target.valid()) {
            if (const GLT::asset::handle h = registry->find(content_path); h != INVALID_HANDLE && registry->is_loaded(h))
                target = GLT::asset::asset_ref(h);
            else if (std::filesystem::exists(PROJECT_CONTENT_DIR / content_path))
                if (auto loaded = registry->load(content_path))
                    target = std::move(*loaded);
        }
        auto* a = target.valid() ? dynamic_cast<construct_asset*>(registry->data(target.get())) : nullptr;
        if (target.valid() && !a) {
            m_dialog_error = std::format("[{}] exists and is not a construct", content_path.generic_string());
            return false;
        }
        if (!a) {
            if (construct_asset_type() == GLT::asset::core_types::invalid) {
                m_dialog_error = "The construct asset type is not registered (no asset registry?)";
                return false;
            }
            auto fresh = GLT::create_unique_ref<construct_asset>(construct_asset_type());
            auto registered = registry->register_runtime(std::move(fresh), content_path, content_path.stem().string());
            if (!registered) {
                m_dialog_error = std::format("Could not register the asset (error {})", static_cast<int>(registered.error()));
                return false;
            }
            target = std::move(*registered);
            a = dynamic_cast<construct_asset*>(registry->data(target.get()));
            if (!a) {
                m_dialog_error = "The registered asset is not a construct";
                return false;
            }
        }

        m_settings.camera_target = m_preview.camera().target;
        m_settings.camera_distance = m_preview.camera().distance;
        m_settings.camera_yaw = m_preview.camera().yaw;
        m_settings.camera_pitch = m_preview.camera().pitch;
        a->set_data(m_working);
        a->preview = m_settings;
        if (auto saved = registry->save(target.get()); !saved) {
            m_dialog_error = std::format("Could not write the file (error {})", static_cast<int>(saved.error()));
            message(std::format("saving [{}] failed", content_path.generic_string()), true);
            return false;
        }
        m_path = content_path;
        m_asset = std::move(target);
        m_saved_hash = content_hash(m_working);
        m_saved_seed = m_settings.seed;
        message(std::format("Saved {}", content_path.generic_string()));
        return true;
    }


    bool construct_editor_window::export_static_mesh_to(const std::filesystem::path& content_path) {

        sync_working();
        evaluation_result fresh{};
        const evaluation_result* source = &m_result;
        if (!m_has_result || m_result_key != evaluation_key(m_snapshot_hash, m_settings.seed, static_cast<u32>(pipeline_step::output))) {
            fresh = evaluate({ .data = m_snapshot, .seed = m_settings.seed });
            source = &fresh;
        }
        std::vector<std::string> warnings;
        auto mesh = to_static_mesh(*source, { .outputs = m_export_outputs }, warnings);
        for (const std::string& w : warnings)
            LOG(warn, "construct editor: export: {}", w)
        auto saved = save_static_mesh(std::move(mesh), content_path);
        if (!saved) {
            m_dialog_error = saved.error();
            message(std::format("export failed: {}", saved.error()), true);
            return false;
        }
        m_settings.export_path = content_path;
        message(std::format("Exported {}", content_path.generic_string()));
        return true;
    }


    void construct_editor_window::open_dialog(dialog d) {

        m_dialog = d;
        m_dialog_open_request = true;
        m_dialog_error.clear();
        const std::filesystem::path content = PROJECT_CONTENT_DIR;
        const std::string stem = m_path.empty() ? std::string("new_construct") : m_path.stem().generic_string();
        if (d == dialog::save_construct) {
            m_dialog_dir = m_path.empty() ? content : (content / m_path.parent_path()).lexically_normal();
            m_dialog_name = stem;
        } else {
            const std::filesystem::path last = m_settings.export_path;
            m_dialog_dir = !last.empty() ? (content / last.parent_path()).lexically_normal()
                                         : (m_path.empty() ? content : (content / m_path.parent_path()).lexically_normal());
            m_dialog_name = !last.empty() ? last.stem().generic_string() : stem + "_mesh";
            m_export_outputs.clear();
            for (const output_result& o : m_result.outputs)
                if (o.desc.purpose == output_purpose::render)
                    m_export_outputs.push_back(o.desc.name);
        }
        std::ranges::replace(m_dialog_name, ' ', '_');
    }


    void construct_editor_window::draw_dialogs() {

        if (m_dialog == dialog::none)
            return;
        const bool construct = m_dialog == dialog::save_construct;
        const char* popup = construct ? "Save as procedural asset##construct_dialog" : "Export static mesh##construct_dialog";
        if (m_dialog_open_request) {
            ImGui::OpenPopup(popup);
            m_dialog_open_request = false;
        }
        ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
        bool open = true;
        if (!ImGui::BeginPopupModal(popup, &open)) {
            m_dialog = dialog::none;
            return;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", construct
            ? "A procedural asset stores the stages, guides and settings. Wherever it is used it is generated (with any seed): "
              "edit it again any time."
            : "A static mesh is the current result baked into triangles (this seed). LODs, colliders and materials are then "
              "set up in the mesh editor like for any imported mesh. Exporting to an existing mesh replaces its geometry.");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        auto* registry = GLT::asset::registry::get_ptr();
        std::string extension = construct ? (registry && construct_asset_type() != GLT::asset::core_types::invalid ? std::string("glt_") + std::string(registry->name_from_type(construct_asset_type())) : std::string("glt_construct"))
                                          : GLT::asset::type_to_extension(GLT::asset::core_types::static_mesh);
        GLT::UI::begin_properties();
        GLT::UI::property_custom("Folder", [&](f32 width) {
            ImGui::SetNextItemWidth(width);
            return GLT::UI::draw_directory_picker("##folder", m_dialog_dir, PROJECT_CONTENT_DIR);
        });
        GLT::UI::property_text("Name", m_dialog_name, "File name, without extension");
        if (!construct) {
            for (const output_result& o : m_result.outputs) {
                if (o.desc.purpose != output_purpose::render)
                    continue;
                bool on = std::ranges::find(m_export_outputs, o.desc.name) != m_export_outputs.end();
                const std::string label = std::format("Include {}", o.desc.name);
                const std::string desc = std::format("{} triangles", format_count(o.triangle_count()));
                if (GLT::UI::property_toggle(label.c_str(), on, desc.c_str())) {
                    if (on)
                        m_export_outputs.push_back(o.desc.name);
                    else
                        std::erase(m_export_outputs, o.desc.name);
                }
            }
        }
        GLT::UI::end_properties();

        std::string name = m_dialog_name;
        std::erase_if(name, [](char c) { return c == '/' || c == '\\'; });
        const std::filesystem::path absolute = m_dialog_dir / (name + "." + extension);
        const std::filesystem::path relative = GLT::project::extract_path_from_project_content_dir(absolute);
        ImGui::Spacing();
        if (relative.empty())
            ImGui::TextColored(TEXT_ERROR, "The folder must be inside the project's content folder");
        else {
            ImGui::TextDisabled("content/%s", relative.generic_string().c_str());
            if (std::filesystem::exists(absolute))
                ImGui::TextColored(TEXT_WARN, construct ? "Exists: it will be replaced" : "Exists: its geometry will be replaced (its id, colliders and materials stay)");
        }
        if (!m_dialog_error.empty())
            ImGui::TextColored(TEXT_ERROR, "%s", m_dialog_error.c_str());

        ImGui::Spacing();
        const bool can = !relative.empty() && !name.empty() && (construct || !m_export_outputs.empty());
        ImGui::BeginDisabled(!can);
        if (GLT::UI::accent_button(construct ? "Save" : "Export", ImVec2(120.0f, 0.0f))) {
            const bool ok = construct ? save_construct_to(relative) : export_static_mesh_to(relative);
            if (ok) {
                m_dialog = dialog::none;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (GLT::UI::subtle_button("Cancel", ImVec2(100.0f, 0.0f)) || !open) {
            m_dialog = dialog::none;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // world link / tests ----------------------------------------------------------------------------------------------

    void construct_editor_window::update_world_link() {

        GLT::editor::i_editor* editor = GLT::editor::get();
        GLT::world::i_world* world = editor ? editor->edited_world() : nullptr;
        const bool same_world = world && world == m_world && world->instance_id() == m_world_instance;
        if (same_world)
            for (const GLT::world::entity_id e : m_world_entities)
                if (world->alive(e))
                    world->despawn(e);
        m_world_entities.clear();
        m_world = world;
        m_world_instance = world ? world->instance_id() : 0;
        if (!world)
            return;
        const glm::vec3 origin(m_working.origin);
        const std::string name = m_path.empty() ? std::string("unsaved") : m_path.stem().generic_string();
        for (const published_mesh& pm : m_meshes) {
            const GLT::world::entity_id e = world->spawn_transient();
            world->set_procedural_mesh(e, pm.mesh);
            world->set_entity_name(e, std::format("Construct: {} ({})", name, pm.output));
            if (origin != glm::vec3(0.0f))
                world->translate_local(e, origin);
            m_world_entities.push_back(e);
        }
    }


    void construct_editor_window::remove_from_world() {

        GLT::editor::i_editor* editor = GLT::editor::get();
        GLT::world::i_world* world = editor ? editor->edited_world() : nullptr;
        if (world && world == m_world && world->instance_id() == m_world_instance)
            for (const GLT::world::entity_id e : m_world_entities)
                if (world->alive(e))
                    world->despawn(e);
        m_world_entities.clear();
        m_world = nullptr;
        m_world_instance = 0;
        m_world_linked = false;
    }


    void construct_editor_window::run_self_test() {

        const self_test_report core = construct::run_self_test();
        std::vector<std::string> failures = core.failures;
        u32 passed = core.passed;
        const auto snap = registry_view();
        for (const auto& p : snap->packs)
            if (p.desc->self_test) {
                const size_t before = failures.size();
                p.desc->self_test(failures, passed);
                for (size_t i = before; i < failures.size(); ++i)
                    failures[i] = std::format("{}: {}", p.desc->name, failures[i]);
            }
        m_test_failed = !failures.empty();
        m_test_summary = failures.empty() ? std::format("Self test: all {} checks passed", passed)
                                          : std::format("Self test: {} failed, {} passed (see log)", failures.size(), passed);
        if (failures.empty())
            LOG(info, "construct self test: all {} check(s) passed", passed)
        else
            for (const std::string& f : failures)
                LOG(error, "construct self test: {}", f)
        message(m_test_summary, m_test_failed);
    }


    glm::vec3 construct_editor_window::to_view(const guide& g, const glm::vec3& local) const {

        return glm::vec3(g.placement.to_world(local) - m_working.origin);
    }


    void construct_editor_window::frame_all() {

        glm::vec3 lo(std::numeric_limits<f32>::max()), hi(-std::numeric_limits<f32>::max());
        bool any = false;
        auto add = [&](const glm::vec3& p) { lo = glm::min(lo, p); hi = glm::max(hi, p); any = true; };
        for (size_t i = 0; i < m_products.size(); ++i)
            if (stage_visible(m_products[i].stage) && m_products[i].lo.x <= m_products[i].hi.x) {
                add(m_products[i].lo);
                add(m_products[i].hi);
            }
        if (m_products.empty())
            for (const element_summary& e : m_result.elements)
                if (stage_visible(e.stage))
                    for (const glm::vec3& p : e.skeleton)
                        add(p);
        for (const guide& g : m_working.guides) {
            if (auto cage = guide_mesh(g))
                for (const glm::vec3& p : cage->positions())
                    add(to_view(g, p));
            if (const auto* c = std::get_if<curve_guide>(&g.shape))
                for (const glm::vec3& p : c->points)
                    add(to_view(g, p));
            if (const auto* pts = std::get_if<point_guide>(&g.shape))
                for (const glm::vec3& p : pts->points)
                    add(to_view(g, p));
        }
        if (!any) {
            lo = glm::vec3(-1.0f, 0.0f, -1.0f);
            hi = glm::vec3(1.0f, 2.0f, 1.0f);
        }
        m_preview.frame(lo, hi);
    }


    void construct_editor_window::make_unique(guide& g) const {

        auto taken = [&](const guide& x) {
            return std::ranges::any_of(m_working.guides, [&](const guide& o) { return o.name == x.name && o.index == x.index; });
        };
        if (g.name.empty())
            g.name = "guide";
        while (taken(g))
            g.index = g.index < 0 ? 1 : g.index + 1;
    }

    // frame -----------------------------------------------------------------------------------------------------------

    void construct_editor_window::update(f32 /*delta_time*/) {

        if (!m_visible)
            m_preview.set_visible(false);
        else
            submit_debug_lines();
        m_visible = false;
    }


    void construct_editor_window::draw_content(f32 /*delta_time*/) {

        if (!m_started) {
            m_started = true;
            take(std::move(m_working), false);
        }
        m_visible = true;
        if (!m_framed && m_has_result) {
            frame_all();
            m_framed = true;
        }

        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput && io.KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_S, false))
                save();
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
                io.KeyShift ? redo() : undo();
            if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
                redo();
        }

        draw_menu_bar();
        draw_toolbar();

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        constexpr f32 SPLITTER = 6.0f;
        m_outline_width = std::clamp(m_outline_width, 180.0f, std::max(180.0f, avail.x * 0.4f));
        m_properties_width = std::clamp(m_properties_width, 260.0f, std::max(260.0f, avail.x * 0.5f));
        const f32 view_width = std::max(avail.x - m_outline_width - m_properties_width - 2.0f * SPLITTER - 2.0f * ImGui::GetStyle().ItemSpacing.x, 120.0f);
        constexpr ImGuiWindowFlags NO_SCROLL = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

        auto splitter = [&](const char* id, f32& width, f32 sign) {
            ImGui::SameLine();
            ImGui::InvisibleButton(id, ImVec2(SPLITTER, avail.y));
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (ImGui::IsItemActive())
                width += io.MouseDelta.x * sign;
            ImGui::SameLine();
        };

        if (ImGui::BeginChild("##outline", ImVec2(m_outline_width, avail.y), ImGuiChildFlags_None)) {
            draw_stage_list();
            draw_guide_list();
        }
        ImGui::EndChild();
        splitter("##split_left", m_outline_width, 1.0f);

        if (ImGui::BeginChild("##view", ImVec2(view_width, avail.y), ImGuiChildFlags_None, NO_SCROLL))
            draw_view(ImGui::GetContentRegionAvail());
        ImGui::EndChild();
        splitter("##split_right", m_properties_width, -1.0f);

        if (ImGui::BeginChild("##properties", ImVec2(0.0f, avail.y), ImGuiChildFlags_None))
            draw_properties();
        ImGui::EndChild();

        draw_dialogs();
        sync_working();
        poll_evaluation();
    }


    void construct_editor_window::draw_menu_bar() {

        if (!ImGui::BeginMenuBar())
            return;

        if (ImGui::BeginMenu("File")) {
            if (ImGui::BeginMenu("New")) {
                if (ImGui::MenuItem("Empty construct"))
                    take(construct_data{}, true);
                const auto snap = registry_view();
                std::string pack;
                for (const auto& p : snap->presets) {
                    if (p.pack != pack) {
                        pack = p.pack;
                        const pack_desc* pd = snap->find_pack(pack);
                        ImGui::SeparatorText(pd && !pd->display.empty() ? pd->display.c_str() : pack.c_str());
                    }
                    if (ImGui::MenuItem(p.desc->display.c_str()) && p.desc->make)
                        take(p.desc->make(), true);
                    tooltip(p.desc->description.c_str());
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S"))
                save();
            tooltip(m_path.empty() ? "Not saved yet: asks where to put the procedural asset" : m_path.generic_string().c_str());
            if (ImGui::MenuItem("Save as procedural asset ..."))
                open_dialog(dialog::save_construct);
            ImGui::Separator();
            if (ImGui::MenuItem("Export static mesh ..."))
                open_dialog(dialog::export_mesh);
            if (!m_settings.export_path.empty()) {
                const std::string again = std::format("Export again to {}", m_settings.export_path.generic_string());
                if (ImGui::MenuItem(again.c_str())) {
                    m_export_outputs.clear();
                    if (!export_static_mesh_to(m_settings.export_path))
                        message(m_dialog_error, true);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(m_world_linked ? "Remove from world" : "Place in world")) {
                if (m_world_linked)
                    remove_from_world();
                else {
                    m_world_linked = true;
                    update_world_link();
                }
            }
            tooltip("Shows the construct in the edited world (not saved with the world; follows your edits while this window is open)");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !m_undo.empty()))
                undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !m_redo.empty()))
                redo();
            ImGui::Separator();
            if (ImGui::BeginMenu("Add stage")) {
                const auto snap = packs().snapshot();
                for (const auto& r : snap->generators) {
                    if (!pack_active(r.pack))
                        continue;
                    if (ImGui::MenuItem(r.desc->display.c_str()))
                        add_stage(*r.desc);
                    tooltip(r.desc->description.c_str());
                }
                ImGui::EndMenu();
            }
            const auto snap = registry_view();
            if (ImGui::BeginMenu("Add guide", !snap->guide_templates.empty())) {
                for (const auto& r : snap->guide_templates) {
                    if (!pack_active(r.pack))
                        continue;
                    const guide_template_desc& t = *r.desc;
                    if (ImGui::MenuItem(t.display.c_str()) && t.make) {
                        guide g = t.make(m_working, selected_guide());
                        make_unique(g);
                        m_working.guides.push_back(std::move(g));
                        if (t.after_add)
                            t.after_add(m_working);
                        select_guide(static_cast<i32>(m_working.guides.size()) - 1);
                    }
                    tooltip(t.description.c_str());
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Guides", nullptr, &m_settings.show_guides);
            tooltip("Splines, planes and volumes you placed, with their handles");
            ImGui::MenuItem("Skeletons", nullptr, &m_show_skeleton);
            tooltip("Centre lines of everything generated, in the colour of its stage");
            bool grid = m_preview.grid();
            if (ImGui::MenuItem("Floor grid", nullptr, &grid))
                m_preview.set_grid(grid);
            ImGui::Separator();
            if (ImGui::MenuItem("All stages", nullptr, m_stage_view == stage_view::all))
                m_stage_view = stage_view::all;
            if (ImGui::MenuItem("Stages up to the selected one", nullptr, m_stage_view == stage_view::up_to_current))
                m_stage_view = stage_view::up_to_current;
            if (ImGui::MenuItem("Only the selected stage", nullptr, m_stage_view == stage_view::only_current))
                m_stage_view = stage_view::only_current;
            if (ImGui::MenuItem("Unhide all stages", nullptr, false, !m_hidden_stages.empty()))
                m_hidden_stages.clear();
            ImGui::Separator();
            if (ImGui::MenuItem("Frame", "F"))
                frame_all();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Run self test"))
                run_self_test();
            tooltip("Runs the core construction self test and this tool's checks (results in the log)");
            ImGui::EndMenu();
        }

        // Right side: where it is saved, unsaved changes, the last message
        std::string state = m_path.empty() ? std::string("not saved") : m_path.generic_string();
        const bool unsaved = dirty();
        if (unsaved)
            state += "  *";
        const bool show_message = !m_message.empty() && ImGui::GetTime() - m_message_time < 8.0;
        const f32 text_w = ImGui::CalcTextSize(state.c_str()).x + (show_message ? ImGui::CalcTextSize(m_message.c_str()).x + 24.0f : 0.0f);
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + 20.0f, ImGui::GetWindowWidth() - text_w - 16.0f));
        if (show_message) {
            ImGui::TextColored(m_message_error ? TEXT_ERROR : TEXT_OK, "%s", m_message.c_str());
            ImGui::SameLine(0.0f, 24.0f);
        }
        if (unsaved)
            ImGui::TextColored(TEXT_WARN, "%s", state.c_str());
        else
            ImGui::TextDisabled("%s", state.c_str());
        tooltip(unsaved ? "Unsaved changes (Ctrl+S)" : nullptr);
        ImGui::EndMenuBar();
    }


    void construct_editor_window::draw_toolbar() {

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);

        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Seed");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputScalar("##seed", ImGuiDataType_U64, &m_settings.seed);
        tooltip("Every random choice of every stage follows this number");
        ImGui::SameLine();
        if (GLT::UI::subtle_button("New seed"))
            m_settings.seed = hash_combine(m_settings.seed, 0x2545f4914f6cdd1dull) % 1000000u;
        tooltip("Grow it again with another seed (trunk, branches, leaves, ...)");

        auto group = [](const char* label) {
            ImGui::SameLine(0.0f, 18.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", label);
            ImGui::SameLine();
        };

        group("Pipeline");
        i32 step = static_cast<i32>(m_step);
        if (GLT::UI::segmented_control("##step", step, STEP_LABELS, 420.0f))
            m_step = static_cast<pipeline_step>(step);
        tooltip("Which result to look at. Guides: the guides as read. Generate / Expand: the elements (centre lines, points, "
                "relations). Realize / Finish: the meshes each element made, per stage. Output: the final meshes.\n"
                "Generation stops after this step, so later, expensive steps do not run");

        group("Show");
        static constexpr const char* views[] = { "All", "Up to stage", "Only stage" };
        i32 view = static_cast<i32>(m_stage_view);
        ImGui::BeginDisabled(m_step == pipeline_step::output);
        if (GLT::UI::segmented_control("##show", view, views, 250.0f))
            m_stage_view = static_cast<stage_view>(view);
        ImGui::EndDisabled();
        tooltip(m_step == pipeline_step::output ? "Output shows the final, merged meshes: pick Realize to look at stages separately"
                                                : "All stages, the stages up to the selected one, or only the selected one");

        group("Click selects");
        static constexpr const char* picks[] = { "All", "Guides", "Stage" };
        i32 pick = static_cast<i32>(m_pick);
        if (GLT::UI::segmented_control("##pick", pick, picks, 190.0f))
            m_pick = static_cast<pick_filter>(pick);
        tooltip("What a click in the view can select: anything visible, only guides, or only elements of the selected stage");

        group("Gizmo");
        static constexpr const char* gizmos[] = { "Move", "Rotate", "Scale" };
        i32 gizmo = static_cast<i32>(m_gizmo);
        if (GLT::UI::segmented_control("##gizmo", gizmo, gizmos, 190.0f))
            m_gizmo = static_cast<gizmo_mode>(gizmo);
        ImGui::SameLine();
        static constexpr const char* spaces[] = { "World", "Local" };
        i32 space = m_gizmo_local ? 1 : 0;
        if (GLT::UI::segmented_control("##space", space, spaces, 110.0f))
            m_gizmo_local = space == 1;

        ImGui::SameLine(0.0f, 18.0f);
        if (GLT::UI::subtle_button("Frame"))
            frame_all();
        tooltip("Frame what is visible (F over the view)");

        ImGui::PopStyleVar(2);
        ImGui::Spacing();
    }

    // outline ---------------------------------------------------------------------------------------------------------

    void construct_editor_window::draw_stage_list() {

        const auto snap = packs().snapshot();
        GLT::UI::card_options options{};
        options.header_actions_width = ImGui::GetFrameHeight() + 4.0f;
        options.summary = m_working.generators.empty() ? "none" : nullptr;
        options.header_actions = [&] {
            if (GLT::UI::subtle_button("+##add_stage", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
                ImGui::OpenPopup("##add_stage_popup");
            tooltip("Add a stage");
            if (ImGui::BeginPopup("##add_stage_popup")) {
                ImGui::TextDisabled("Add stage");
                bool any = false;
                for (const auto& r : snap->generators) {
                    if (!pack_active(r.pack))
                        continue;
                    any = true;
                    if (ImGui::Selectable(r.desc->display.c_str()))
                        add_stage(*r.desc);
                    tooltip(r.desc->description.c_str());
                }
                if (!any)
                    ImGui::TextDisabled("This construct's packs have no stage types");
                ImGui::EndPopup();
            }
        };

        if (GLT::UI::begin_card("Stages", options)) {
            if (m_working.generators.empty())
                ImGui::TextDisabled("No stages yet: add one with +");

            i32 move = 0;
            u32 move_id = 0, remove_id = 0, duplicate_id = 0;
            for (size_t i = 0; i < m_working.generators.size(); ++i) {
                generator_instance& g = m_working.generators[i];
                const auto* reg = snap->find_generator(g.type);
                const generator_desc* desc = reg ? reg->desc.get() : nullptr;
                const glm::vec3 color = desc ? desc->color : glm::vec3(0.5f);
                const bool hidden = m_hidden_stages.contains(g.id);
                const bool selected = m_selection.kind == selection_kind::stage && m_selection.stage == g.id;
                const bool current = m_current_stage == g.id;
                ImGui::PushID(static_cast<int>(g.id));

                // Visibility dot (stage colour)
                const f32 h = ImGui::GetTextLineHeight();
                const ImVec2 dot = ImGui::GetCursorScreenPos();
                if (ImGui::InvisibleButton("##vis", ImVec2(h, h))) {
                    if (hidden)
                        m_hidden_stages.erase(g.id);
                    else
                        m_hidden_stages.insert(g.id);
                }
                tooltip(hidden ? "Hidden in the view: click to show" : "Click to hide in the view");
                ImDrawList* draw = ImGui::GetWindowDrawList();
                const ImVec2 c(dot.x + h * 0.5f, dot.y + h * 0.5f);
                if (hidden)
                    draw->AddCircle(c, h * 0.32f, to_color(color, 0.8f), 0, 1.5f);
                else
                    draw->AddCircleFilled(c, h * 0.32f, to_color(color, g.enabled ? 1.0f : 0.35f));
                ImGui::SameLine();

                const f32 actions = ImGui::GetFrameHeight() + 2.0f;
                const std::string label = std::format("{}{}##row", g.label, g.enabled ? "" : "  (off)");
                if (!g.enabled || !desc)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                if (ImGui::Selectable(label.c_str(), selected || (current && m_selection.kind == selection_kind::element), 0,
                                      ImVec2(std::max(ImGui::GetContentRegionAvail().x - actions, 40.0f), 0.0f)))
                    select_stage(g.id);
                if (!g.enabled || !desc)
                    ImGui::PopStyleColor();
                if (desc && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("%s\n%s", desc->display.c_str(), desc->description.c_str());
                ImGui::SameLine();
                if (GLT::UI::menu_button("##actions"))
                    ImGui::OpenPopup("##stage_menu");
                if (ImGui::BeginPopup("##stage_menu")) {
                    if (ImGui::MenuItem("Enabled", nullptr, g.enabled))
                        g.enabled = !g.enabled;
                    if (ImGui::MenuItem("Show only this")) {
                        select_stage(g.id);
                        m_stage_view = stage_view::only_current;
                    }
                    if (ImGui::MenuItem("New seed for this stage"))
                        g.seed = static_cast<u32>(hash_combine(g.seed + 1u, g.id) % 100000u);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Move up", nullptr, false, i > 0)) { move = -1; move_id = g.id; }
                    if (ImGui::MenuItem("Move down", nullptr, false, i + 1 < m_working.generators.size())) { move = 1; move_id = g.id; }
                    if (ImGui::MenuItem("Duplicate"))
                        duplicate_id = g.id;
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete"))
                        remove_id = g.id;
                    ImGui::EndPopup();
                }

                // Second line: what it grows on, what it made
                std::string info;
                if (!desc)
                    info = std::format("{} (not loaded)", g.type);
                else if (!g.inputs.empty()) {
                    info = "on ";
                    for (size_t k = 0; k < g.inputs.size(); ++k) {
                        const i32 idx = stage_index(g.inputs[k]);
                        info += (k ? ", " : "") + (idx >= 0 ? m_working.generators[static_cast<size_t>(idx)].label : std::string("?"));
                    }
                } else if (desc->takes_input)
                    info = "no input";
                if (const stage_stats* st = find_stats(g.id); st && st->ran)
                    info += std::format("{}{} el  {} tri  {:.1f} ms", info.empty() ? "" : "   ", format_count(st->elements), format_count(st->triangles), st->milliseconds);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + h + ImGui::GetStyle().ItemSpacing.x);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextUnformatted(info.c_str());
                ImGui::PopStyleColor();
                ImGui::PopID();
            }

            if (move != 0) {
                const i32 i = stage_index(move_id);
                const i32 j = i + move;
                if (i >= 0 && j >= 0 && j < static_cast<i32>(m_working.generators.size()))
                    std::swap(m_working.generators[static_cast<size_t>(i)], m_working.generators[static_cast<size_t>(j)]);
            }
            if (duplicate_id != 0)
                if (const i32 i = stage_index(duplicate_id); i >= 0) {
                    generator_instance copy = m_working.generators[static_cast<size_t>(i)];
                    copy.id = next_stage_id(m_working.generators);
                    copy.label += " copy";
                    m_working.generators.insert(m_working.generators.begin() + i + 1, std::move(copy));
                    select_stage(m_working.generators[static_cast<size_t>(i + 1)].id);
                }
            if (remove_id != 0)
                remove_stage(remove_id);
        }
        GLT::UI::end_card();
        ImGui::Spacing();
    }


    void construct_editor_window::draw_guide_list() {

        const auto snap = registry_view();
        std::vector<const guide_template_desc*> tpls;
        for (const auto& r : snap->guide_templates)
            if (pack_active(r.pack))
                tpls.push_back(r.desc.get());
        GLT::UI::card_options options{};
        options.header_actions_width = tpls.empty() ? 0.0f : ImGui::GetFrameHeight() + 4.0f;
        const std::string summary = std::to_string(m_working.guides.size());
        options.summary = summary.c_str();
        options.header_actions = [&] {
            if (GLT::UI::subtle_button("+##add_guide", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
                ImGui::OpenPopup("##add_guide_popup");
            tooltip("Add a guide");
            if (ImGui::BeginPopup("##add_guide_popup")) {
                ImGui::TextDisabled("Add guide");
                for (const guide_template_desc* t : tpls) {
                    if (ImGui::Selectable(t->display.c_str()) && t->make) {
                        guide g = t->make(m_working, selected_guide());
                        make_unique(g);
                        m_working.guides.push_back(std::move(g));
                        if (t->after_add)
                            t->after_add(m_working);
                        select_guide(static_cast<i32>(m_working.guides.size()) - 1);
                    }
                    tooltip(t->description.c_str());
                }
                ImGui::EndPopup();
            }
        };

        if (GLT::UI::begin_card("Guides", options)) {
            if (m_working.guides.empty())
                ImGui::TextDisabled("Nothing placed. Guides are splines, planes and\nvolumes stages can use (add with +)");
            i32 remove = -1, duplicate = -1;
            for (size_t i = 0; i < m_working.guides.size(); ++i) {
                const guide& g = m_working.guides[i];
                std::string role = g.role.empty() ? std::string("plain") : std::string(g.role.str());
                if (const role_desc* r = snap->find_role(g.role); r && !r->display.empty())
                    role = r->display;
                ImGui::PushID(static_cast<int>(i));
                const std::string label = std::format("{}{}", g.name, g.index >= 0 ? std::format("[{}]", g.index) : std::string());
                const bool selected = m_selection.kind == selection_kind::guide && m_selection.guide == static_cast<i32>(i);
                if (ImGui::Selectable("##row", selected, 0, ImVec2(std::max(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 2.0f, 40.0f), 0.0f)))
                    select_guide(static_cast<i32>(i));
                ImGui::SameLine(ImGui::GetStyle().FramePadding.x + 4.0f);
                ImGui::TextUnformatted(label.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("%s, %s", role.c_str(), shape_label(g));
                ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
                if (GLT::UI::menu_button("##actions"))
                    ImGui::OpenPopup("##guide_menu");
                if (ImGui::BeginPopup("##guide_menu")) {
                    if (ImGui::MenuItem("Duplicate"))
                        duplicate = static_cast<i32>(i);
                    if (ImGui::MenuItem("Delete"))
                        remove = static_cast<i32>(i);
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            if (duplicate >= 0) {
                guide copy = m_working.guides[static_cast<size_t>(duplicate)];
                copy.placement.origin += glm::dvec3(0.5, 0.0, 0.5);
                make_unique(copy);
                m_working.guides.push_back(std::move(copy));
                select_guide(static_cast<i32>(m_working.guides.size()) - 1);
            }
            if (remove >= 0) {
                m_working.guides.erase(m_working.guides.begin() + remove);
                m_selection = {};
                m_point = -1;
            }
        }
        GLT::UI::end_card();
    }

    // selection -------------------------------------------------------------------------------------------------------

    void construct_editor_window::select_stage(u32 stage) {

        m_selection = { selection_kind::stage, stage, -1, {} };
        m_current_stage = stage;
        m_point = -1;
    }


    void construct_editor_window::select_guide(i32 guide) {

        m_selection = { selection_kind::guide, 0, guide, {} };
        m_point = -1;
    }


    void construct_editor_window::select_element(const element_path& path) {

        m_selection = { selection_kind::element, 0, -1, path };
        m_point = -1;
        if (const element_summary* e = find_element(path); e && e->stage != 0)
            m_current_stage = e->stage;
    }


    void construct_editor_window::add_stage(const generator_desc& type) {

        const u32 id = next_stage_id(m_working.generators);
        m_working.generators.push_back(make_stage(type, id));
        select_stage(id);
    }


    void construct_editor_window::remove_stage(u32 stage) {

        std::erase_if(m_working.generators, [&](const generator_instance& g) { return g.id == stage; });
        for (generator_instance& g : m_working.generators)
            std::erase(g.inputs, stage);
        m_hidden_stages.erase(stage);
        if (m_selection.kind == selection_kind::stage && m_selection.stage == stage)
            m_selection = {};
        if (m_current_stage == stage)
            m_current_stage = m_working.generators.empty() ? 0 : m_working.generators.front().id;
    }

    // view ------------------------------------------------------------------------------------------------------------

    void construct_editor_window::draw_view(ImVec2 size) {

        m_visible_meshes.clear();
        for (const published_mesh& pm : m_meshes) {
            u32 stage = 0;
            if (pm.output.starts_with("stage:"))
                std::from_chars(pm.output.data() + 6, pm.output.data() + pm.output.size(), stage);
            if (!pm.output.starts_with("stage:") || stage_visible(stage))
                m_visible_meshes.push_back(pm);
        }

        const bool guides_pickable = m_settings.show_guides && m_pick != pick_filter::current_stage;
        const bool orbit = m_drag.guide < 0 && !m_gizmo_hot;
        const construct_preview::interaction in = m_preview.draw(size, m_visible_meshes, orbit, [&](ImDrawList& draw) {
            if (m_settings.show_guides)
                draw_overlay(draw);
            draw_gizmo(draw);
        });

        // Press on a handle: select it and drag it in the view plane
        if (in.clicked && !m_gizmo_hot && guides_pickable) {
            const handle h = pick_guide(in.mouse, false);
            if (h.guide >= 0 && h.kind != handle_kind::none) {
                select_guide(h.guide);
                if (h.kind == handle_kind::control_point || h.kind == handle_kind::point)
                    m_point = h.index;
                m_drag = h;
            }
        }
        if (m_drag.guide >= 0) {
            if (in.dragging && (in.delta.x != 0.0f || in.delta.y != 0.0f))
                drag(m_drag, in.delta);
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                m_drag = {};
        } else if (in.picked && !m_gizmo_hot) {
            // A click (no drag): guide lines first, then generated elements, else nothing
            const handle h = guides_pickable ? pick_guide(in.mouse, true) : handle{};
            if (h.guide >= 0)
                select_guide(h.guide);
            else if (const auto element = m_step < pipeline_step::realize ? pick_skeleton(in.mouse) : pick_element(in.mouse))
                select_element(*element);
            else {
                m_selection = {};
                m_point = -1;
            }
        }
        if (in.hovered && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false))
            frame_all();
        if (in.hovered && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            m_selection = {};
            m_point = -1;
        }
    }


    void construct_editor_window::draw_overlay(ImDrawList& draw) {

        auto line = [&](const glm::vec3& a, const glm::vec3& b, ImU32 color, f32 width) {
            const auto pa = m_preview.project(a), pb = m_preview.project(b);
            if (pa && pb)
                draw.AddLine(*pa, *pb, color, width);
        };
        auto dot = [&](const glm::vec3& p, ImU32 color, f32 radius) {
            if (const auto s = m_preview.project(p))
                draw.AddCircleFilled(*s, radius, color);
        };

        for (size_t gi = 0; gi < m_working.guides.size(); ++gi) {
            const guide& g = m_working.guides[gi];
            const bool selected = m_selection.kind == selection_kind::guide && static_cast<i32>(gi) == m_selection.guide;
            const ImU32 color = selected ? COLOR_SELECTED : COLOR_GUIDE;
            const f32 width = selected ? 2.0f : 1.0f;

            if (auto cage = guide_mesh(g)) {
                for (u32 h = 0; h < cage->halfedge_count(); ++h) {
                    const u32 t = cage->he(h).twin;
                    if (t != geometry::INVALID_INDEX && t < h)
                        continue;
                    line(to_view(g, cage->position(cage->from_vertex(h))), to_view(g, cage->position(cage->he(h).vertex)), color, width);
                }
            }
            if (const auto* c = std::get_if<curve_guide>(&g.shape); c && c->points.size() >= 2) {
                const geometry::curve curve = make_curve(*c);
                const auto samples = curve.samples();
                for (size_t i = 0; i + 1 < samples.size(); ++i)
                    line(to_view(g, samples[i].position), to_view(g, samples[i + 1].position), color, width + 1.0f);
                for (size_t i = 0; i < c->points.size(); ++i) {
                    const bool point = selected && m_point == static_cast<i32>(i);
                    dot(to_view(g, c->points[i]), point ? COLOR_SELECTED : (selected ? COLOR_HANDLE : color),
                        point ? HANDLE_RADIUS + 2.0f : (selected ? HANDLE_RADIUS : 3.0f));
                }
            }
            if (const auto* pts = std::get_if<point_guide>(&g.shape))
                for (size_t i = 0; i < pts->points.size(); ++i) {
                    const bool point = selected && m_point == static_cast<i32>(i);
                    dot(to_view(g, pts->points[i]), point ? COLOR_SELECTED : (selected ? COLOR_HANDLE : color),
                        point ? HANDLE_RADIUS + 2.0f : (selected ? HANDLE_RADIUS : 3.5f));
                }
            if (!selected)
                if (const auto s = m_preview.project(to_view(g, glm::vec3(0.0f))))
                    draw.AddRect(ImVec2(s->x - 3.0f, s->y - 3.0f), ImVec2(s->x + 3.0f, s->y + 3.0f), color, 0.0f, 0, 1.0f);
        }

        // Selected element: its name next to it
        if (m_selection.kind == selection_kind::element)
            if (const element_summary* e = find_element(m_selection.element); e && !e->skeleton.empty())
                if (const auto s = m_preview.project(e->skeleton.back())) {
                    const std::string text = e->path.to_string();
                    draw.AddRectFilled(ImVec2(s->x + 8.0f, s->y - 10.0f), ImVec2(s->x + 16.0f + ImGui::CalcTextSize(text.c_str()).x, s->y + 10.0f),
                                       IM_COL32(10, 10, 12, 190), 4.0f);
                    draw.AddText(ImVec2(s->x + 12.0f, s->y - 8.0f), COLOR_SELECTED, text.c_str());
                }
    }


    void construct_editor_window::draw_gizmo(ImDrawList& draw) {

        guide* g = m_selection.kind == selection_kind::guide && m_selection.guide >= 0 && m_selection.guide < static_cast<i32>(m_working.guides.size())
                 ? &m_working.guides[static_cast<size_t>(m_selection.guide)] : nullptr;
        if (!g || !m_settings.show_guides || m_drag.guide >= 0) {
            m_gizmo_hot = false;
            m_gizmo_using = false;
            return;
        }

        glm::vec3* point = nullptr;
        if (auto* c = std::get_if<curve_guide>(&g->shape); c && m_point >= 0 && m_point < static_cast<i32>(c->points.size()))
            point = &c->points[static_cast<size_t>(m_point)];
        if (auto* p = std::get_if<point_guide>(&g->shape); p && m_point >= 0 && m_point < static_cast<i32>(p->points.size()))
            point = &p->points[static_cast<size_t>(m_point)];

        const glm::vec3 origin = glm::vec3(g->placement.origin - m_working.origin);
        glm::mat4 matrix(1.0f);
        if (point)
            matrix = glm::translate(glm::mat4(1.0f), to_view(*g, *point)) * glm::mat4_cast(g->placement.rotation);
        else
            matrix = glm::translate(glm::mat4(1.0f), origin) * glm::mat4_cast(g->placement.rotation) * glm::scale(glm::mat4(1.0f), g->placement.scale);

        const glm::mat4 view = m_preview.view_matrix();
        const glm::mat4 projection = m_preview.projection_matrix();
        const ImVec2 at = m_preview.view_origin(), size = m_preview.view_size();
        const ImGuizmo::OPERATION op = point ? ImGuizmo::TRANSLATE
                                     : (m_gizmo == gizmo_mode::rotate ? ImGuizmo::ROTATE : (m_gizmo == gizmo_mode::scale ? ImGuizmo::SCALE : ImGuizmo::TRANSLATE));
        const ImGuizmo::MODE mode = op == ImGuizmo::SCALE || m_gizmo_local ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

        ImGuizmo::PushID(this);
        ImGuizmo::SetDrawlist(&draw);
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetRect(at.x, at.y, size.x, size.y);
        if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), op, mode, glm::value_ptr(matrix))) {
            const glm::vec3 translation(matrix[3]);
            if (point) {
                const glm::dvec3 world = glm::dvec3(translation) + m_working.origin;
                *point = (glm::inverse(g->placement.rotation) * glm::vec3(world - g->placement.origin)) / g->placement.scale;
            } else {
                const glm::vec3 scale(glm::length(glm::vec3(matrix[0])), glm::length(glm::vec3(matrix[1])), glm::length(glm::vec3(matrix[2])));
                const glm::mat3 rotation = glm::mat3(glm::vec3(matrix[0]) / std::max(scale.x, 1e-6f), glm::vec3(matrix[1]) / std::max(scale.y, 1e-6f),
                                                     glm::vec3(matrix[2]) / std::max(scale.z, 1e-6f));
                g->placement.origin = glm::dvec3(translation) + m_working.origin;
                g->placement.rotation = glm::normalize(glm::quat_cast(rotation));
                g->placement.scale = glm::max(scale, glm::vec3(0.001f));
            }
        }
        m_gizmo_using = ImGuizmo::IsUsing();
        m_gizmo_hot = ImGuizmo::IsOver() || m_gizmo_using;
        ImGuizmo::PopID();
    }


    construct_editor_window::handle construct_editor_window::pick_guide(ImVec2 mouse, bool edges) const {

        handle best{};
        f32 best_d = PICK_RADIUS;
        auto consider = [&](const glm::vec3& p, handle h, f32 limit) {
            if (const auto s = m_preview.project(p)) {
                const f32 d = std::hypot(s->x - mouse.x, s->y - mouse.y);
                if (d < std::min(best_d, limit)) {
                    best_d = d;
                    best = h;
                }
            }
        };
        const i32 selected = m_selection.kind == selection_kind::guide ? m_selection.guide : -1;
        for (size_t gi = 0; gi < m_working.guides.size(); ++gi) {
            const guide& g = m_working.guides[gi];
            const i32 id = static_cast<i32>(gi);
            if (const auto* c = std::get_if<curve_guide>(&g.shape))
                for (size_t i = 0; i < c->points.size(); ++i)
                    consider(to_view(g, c->points[i]), { id, handle_kind::control_point, static_cast<i32>(i) }, PICK_RADIUS);
            if (const auto* pts = std::get_if<point_guide>(&g.shape))
                for (size_t i = 0; i < pts->points.size(); ++i)
                    consider(to_view(g, pts->points[i]), { id, handle_kind::point, static_cast<i32>(i) }, PICK_RADIUS);
            consider(to_view(g, glm::vec3(0.0f)), { id, handle_kind::origin, -1 }, id == selected ? PICK_RADIUS : PICK_RADIUS * 0.6f);
        }
        if (best.guide >= 0 || !edges)
            return best;

        // No handle: an edge or curve close to the mouse selects its guide
        f32 edge_best = EDGE_PICK_DISTANCE;
        for (size_t gi = 0; gi < m_working.guides.size(); ++gi) {
            const guide& g = m_working.guides[gi];
            auto test = [&](const glm::vec3& a, const glm::vec3& b) {
                const auto pa = m_preview.project(a), pb = m_preview.project(b);
                if (!pa || !pb)
                    return;
                const f32 d = segment_distance(mouse, *pa, *pb);
                if (d < edge_best) {
                    edge_best = d;
                    best = { static_cast<i32>(gi), handle_kind::none, -1 };
                }
            };
            if (auto cage = guide_mesh(g))
                for (u32 h = 0; h < cage->halfedge_count(); ++h)
                    test(to_view(g, cage->position(cage->from_vertex(h))), to_view(g, cage->position(cage->he(h).vertex)));
            if (const auto* c = std::get_if<curve_guide>(&g.shape); c && c->points.size() >= 2) {
                const geometry::curve curve = make_curve(*c);
                const auto samples = curve.samples();
                for (size_t i = 0; i + 1 < samples.size(); ++i)
                    test(to_view(g, samples[i].position), to_view(g, samples[i + 1].position));
            }
        }
        return best;
    }


    std::optional<element_path> construct_editor_window::pick_element(ImVec2 mouse) const {

        if (!m_has_result || m_pick == pick_filter::guides)
            return std::nullopt;
        const auto [origin, dir] = m_preview.ray(mouse);
        f32 best = std::numeric_limits<f32>::max();
        std::optional<element_path> hit;
        for (size_t i = 0; i < m_result.products.size() && i < m_products.size(); ++i) {
            const product_info& info = m_products[i];
            if (info.lo.x > info.hi.x || !stage_pickable(info.stage))
                continue;
            const auto box = ray_box(origin, dir, info.lo, info.hi);
            if (!box || *box > best)
                continue;
            const geometry::triangle_mesh tris = product_triangles(m_result.products[i], m_working.origin);
            for (size_t t = 0; t + 2 < tris.indices.size(); t += 3) {
                const auto d = ray_triangle(origin, dir, tris.positions[tris.indices[t]], tris.positions[tris.indices[t + 1]], tris.positions[tris.indices[t + 2]]);
                if (d && *d < best) {
                    best = *d;
                    hit = m_result.products[i].element;
                }
            }
        }
        return hit;
    }


    std::optional<element_path> construct_editor_window::pick_skeleton(ImVec2 mouse) const {

        if (!m_has_result || m_pick == pick_filter::guides)
            return std::nullopt;
        f32 best = 7.0f;
        std::optional<element_path> hit;
        for (const element_summary& e : m_result.elements) {
            if (e.skeleton.empty() || !stage_pickable(e.stage))
                continue;
            if (e.shape == shape_kind::curve && e.skeleton.size() >= 2) {
                for (size_t i = 0; i + 1 < e.skeleton.size(); ++i) {
                    const auto a = m_preview.project(e.skeleton[i]), b = m_preview.project(e.skeleton[i + 1]);
                    if (a && b)
                        if (const f32 d = segment_distance(mouse, *a, *b); d < best) {
                            best = d;
                            hit = e.path;
                        }
                }
            } else {
                for (const glm::vec3& p : e.skeleton)
                    if (const auto s = m_preview.project(p))
                        if (const f32 d = std::hypot(s->x - mouse.x, s->y - mouse.y); d < best) {
                            best = d;
                            hit = e.path;
                        }
            }
        }
        return hit;
    }


    glm::vec3 construct_editor_window::handle_position(const handle& h) const {

        const guide& g = m_working.guides[static_cast<size_t>(h.guide)];
        switch (h.kind) {
            case handle_kind::control_point: return to_view(g, std::get<curve_guide>(g.shape).points[static_cast<size_t>(h.index)]);
            case handle_kind::point:         return to_view(g, std::get<point_guide>(g.shape).points[static_cast<size_t>(h.index)]);
            default:                         return to_view(g, glm::vec3(0.0f));
        }
    }


    void construct_editor_window::drag(const handle& h, ImVec2 delta) {

        if (h.guide < 0 || h.guide >= static_cast<i32>(m_working.guides.size()))
            return;
        guide& g = m_working.guides[static_cast<size_t>(h.guide)];
        const glm::vec3 world_delta = m_preview.screen_delta_to_world(handle_position(h), delta);
        const glm::vec3 local_delta = (glm::inverse(g.placement.rotation) * world_delta) / g.placement.scale;
        switch (h.kind) {
            case handle_kind::origin:
                g.placement.origin += glm::dvec3(world_delta);
                break;
            case handle_kind::control_point: {
                auto& pts = std::get<curve_guide>(g.shape).points;
                if (h.index < static_cast<i32>(pts.size()))
                    pts[static_cast<size_t>(h.index)] += local_delta;
                break;
            }
            case handle_kind::point: {
                auto& pts = std::get<point_guide>(g.shape).points;
                if (h.index < static_cast<i32>(pts.size()))
                    pts[static_cast<size_t>(h.index)] += local_delta;
                break;
            }
            default:
                break;
        }
    }


    void construct_editor_window::submit_debug_lines() {

        GLT::render::i_debug_draw* debug = m_preview.debug_draw();
        if (!debug)
            return;
        m_preview.submit_grid();
        if (!m_has_result)
            return;

        // Colours of the stages (one registry lookup per stage, not per element)
        std::unordered_map<u32, glm::vec3> colors;
        for (const generator_instance& g : m_working.generators)
            colors[g.id] = stage_color(g.id);
        auto color_of = [&](u32 stage) { const auto it = colors.find(stage); return it != colors.end() ? it->second : glm::vec3(0.6f, 0.75f, 1.0f); };

        // Skeletons: of everything (View > Skeletons), and always of the selected stage
        const bool stage_selected = m_selection.kind == selection_kind::stage;
        const bool elements_view = m_step < pipeline_step::realize;     // no meshes yet: the elements are what there is to see
        u32 crosses = 0;
        for (const element_summary& e : m_result.elements) {
            const bool highlight = stage_selected && e.stage == m_selection.stage;
            if (!(highlight || m_show_skeleton || elements_view) || !stage_visible(e.stage) || e.skeleton.empty())
                continue;
            const glm::vec4 color(color_of(e.stage), highlight || (elements_view && !stage_selected) ? 0.95f : 0.45f);
            if (e.skeleton.size() >= 2 && e.shape == shape_kind::curve) {
                for (size_t i = 0; i + 1 < e.skeleton.size(); ++i)
                    debug->line(e.skeleton[i], e.skeleton[i + 1], color, 0.0f, false);
            } else {
                for (const glm::vec3& p : e.skeleton) {
                    if (++crosses > 6000)
                        break;
                    debug->line(p - glm::vec3(0.015f, 0.0f, 0.0f), p + glm::vec3(0.015f, 0.0f, 0.0f), color, 0.0f, false);
                    debug->line(p - glm::vec3(0.0f, 0.015f, 0.0f), p + glm::vec3(0.0f, 0.015f, 0.0f), color, 0.0f, false);
                }
            }
        }

        // Relations (forks, joints, ...) in the element views: a line from one element to the nearest point of the other
        if (elements_view) {
            u32 budget = 4000;
            for (const relation_summary& r : m_result.relations) {
                const element_summary* a = find_element(r.a);
                const element_summary* b = find_element(r.b);
                if (!a || !b || a->skeleton.empty() || b->skeleton.empty() || !stage_visible(a->stage) || !stage_visible(b->stage) || --budget == 0)
                    continue;
                const glm::vec3 from = a->skeleton.front();
                glm::vec3 to = b->skeleton.front();
                for (const glm::vec3& p : b->skeleton)
                    if (glm::distance(p, from) < glm::distance(to, from))
                        to = p;
                debug->line(from, to, glm::vec4(0.85f, 0.85f, 0.9f, 0.6f), 0.0f, false);
            }
        }

        // The selected element: wireframe of its triangles over everything
        if (m_selection.kind == selection_kind::element) {
            u32 budget = 30000;
            const glm::vec4 color(1.0f, 0.75f, 0.27f, 0.9f);
            for (const product& p : m_result.products) {
                if (p.element != m_selection.element)
                    continue;
                const geometry::triangle_mesh tris = product_triangles(p, m_working.origin);
                for (size_t t = 0; t + 2 < tris.indices.size() && budget > 3; t += 3, budget -= 3) {
                    const glm::vec3& a = tris.positions[tris.indices[t]];
                    const glm::vec3& b = tris.positions[tris.indices[t + 1]];
                    const glm::vec3& c = tris.positions[tris.indices[t + 2]];
                    debug->line(a, b, color, 0.0f, false);
                    debug->line(b, c, color, 0.0f, false);
                    debug->line(c, a, color, 0.0f, false);
                }
            }
            if (const element_summary* e = find_element(m_selection.element); e && e->skeleton.size() >= 2 && e->shape == shape_kind::curve)
                for (size_t i = 0; i + 1 < e->skeleton.size(); ++i)
                    debug->line(e->skeleton[i], e->skeleton[i + 1], glm::vec4(1.0f, 0.9f, 0.5f, 1.0f), 0.0f, false);
        }
    }

    // properties ------------------------------------------------------------------------------------------------------

    void construct_editor_window::draw_properties() {

        if (m_selection.kind == selection_kind::guide && (m_selection.guide < 0 || m_selection.guide >= static_cast<i32>(m_working.guides.size())))
            m_selection = {};
        if (m_selection.kind == selection_kind::stage && stage_index(m_selection.stage) < 0)
            m_selection = {};

        switch (m_selection.kind) {
            case selection_kind::stage:
                draw_stage_properties(m_working.generators[static_cast<size_t>(stage_index(m_selection.stage))]);
                break;
            case selection_kind::guide:
                draw_guide_section(m_working.guides[static_cast<size_t>(m_selection.guide)]);
                break;
            case selection_kind::element:
                draw_element_properties();
                break;
            case selection_kind::none:
                draw_construct_section();
                break;
        }
        ImGui::Spacing();
        draw_diagnostics();
    }


    void construct_editor_window::draw_stage_properties(generator_instance& g) {

        const generator_desc* desc = stage_type(g);
        ImGui::PushID(static_cast<int>(g.id));

        GLT::UI::card_options head{};
        head.summary = desc ? desc->display.c_str() : "type not loaded";
        if (GLT::UI::begin_card("Stage", head)) {
            GLT::UI::property_text("Name", g.label, "Shown in the stage list; elements made by this stage carry it as their source");
            GLT::UI::property_toggle("Enabled", g.enabled, "Off: the stage (and what grows on it) is skipped");

            if (desc && desc->takes_input) {
                std::string current;
                for (const u32 id : g.inputs)
                    if (const i32 i = stage_index(id); i >= 0)
                        current += (current.empty() ? "" : ", ") + m_working.generators[static_cast<size_t>(i)].label;
                if (current.empty())
                    current = "(choose)";
                GLT::UI::property_custom("Grows on", [&](f32 width) {
                    bool changed = false;
                    ImGui::SetNextItemWidth(width);
                    if (ImGui::BeginCombo("##inputs", current.c_str())) {
                        const i32 self = stage_index(g.id);
                        for (i32 i = 0; i < self; ++i) {
                            const generator_instance& other = m_working.generators[static_cast<size_t>(i)];
                            if (!desc->accepts.empty() && std::ranges::find(desc->accepts, other.type) == desc->accepts.end())
                                continue;
                            bool on = std::ranges::find(g.inputs, other.id) != g.inputs.end();
                            if (ImGui::Checkbox(other.label.c_str(), &on)) {
                                if (on)
                                    g.inputs.push_back(other.id);
                                else
                                    std::erase(g.inputs, other.id);
                                changed = true;
                            }
                        }
                        if (self <= 0)
                            ImGui::TextDisabled("Stages run in order: move this stage below the one it grows on");
                        ImGui::EndCombo();
                    }
                    return changed;
                }, "The stages whose elements this one grows on (only stages above it in the list)");
                if (g.inputs.empty())
                    ImGui::TextColored(TEXT_WARN, "Choose what this stage grows on");
            }

            GLT::UI::property_custom("Stage seed", [&](f32 width) {
                bool changed = false;
                ImGui::SetNextItemWidth(std::max(width - 70.0f, 40.0f));
                changed |= ImGui::InputScalar("##seed", ImGuiDataType_U32, &g.seed);
                ImGui::SameLine();
                if (GLT::UI::subtle_button("Re-roll", ImVec2(64.0f, 0.0f))) {
                    g.seed = static_cast<u32>(hash_combine(g.seed + 1u, g.id) % 100000u);
                    changed = true;
                }
                return changed;
            }, "Added to the construct seed: re-rolls this stage alone");

            if (const stage_stats* st = find_stats(g.id))
                GLT::UI::property_value("Made", st->ran ? std::format("{} elements, {} triangles, {:.1f} ms", format_count(st->elements),
                                                                        format_count(st->triangles), st->milliseconds)
                                                        : std::string("nothing (not run)"));
            if (desc && !desc->description.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%s", desc->description.c_str());
                ImGui::PopStyleColor();
            }
        }
        GLT::UI::end_card();

        if (!desc) {
            ImGui::TextColored(TEXT_WARN, "Stage type [%s] is not registered: is its plugin loaded?", g.type.c_str());
            ImGui::PopID();
            return;
        }

        // Settings, one card per group (in the order the type declares them)
        std::vector<std::string> groups;
        bool any_advanced = false;
        for (const setting_desc& s : desc->settings) {
            any_advanced = any_advanced || s.advanced;
            if (std::ranges::find(groups, s.group) == groups.end())
                groups.push_back(s.group);
        }
        for (const std::string& group : groups) {
            GLT::UI::card_options options{};
            options.collapsible = true;
            u32 changed_count = 0;
            for (const setting_desc& s : desc->settings)
                changed_count += s.group == group && g.find(s.name) ? 1u : 0u;
            const std::string summary = changed_count ? std::format("{} changed", changed_count) : std::string();
            options.summary = summary.c_str();
            const std::string title = group.empty() ? std::string("Settings") : group;
            ImGui::Spacing();
            if (GLT::UI::begin_card(title.c_str(), options))
                for (const setting_desc& s : desc->settings)
                    if (s.group == group && (!s.advanced || m_show_advanced))
                        draw_setting(s, g);
            GLT::UI::end_card();
        }

        ImGui::Spacing();
        if (any_advanced) {
            GLT::UI::toggle_switch("##advanced", m_show_advanced);
            ImGui::SameLine();
            ImGui::TextDisabled("Show advanced settings");
            ImGui::SameLine(0.0f, 20.0f);
        }
        ImGui::BeginDisabled(g.settings.empty());
        if (GLT::UI::subtle_button("Reset all settings"))
            g.settings.clear();
        ImGui::EndDisabled();
        ImGui::PopID();
    }


    bool construct_editor_window::draw_setting(const setting_desc& d, generator_instance& g) {

        const setting_value* set = g.find(d.name);
        setting_value value = set ? *set : d.default_value;
        bool changed = false;
        const char* tip = d.tooltip.empty() ? nullptr : d.tooltip.c_str();
        const char* label = d.display.c_str();

        std::visit([&](auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, f32>) {
                const f32 def = std::holds_alternative<f32>(d.default_value) ? std::get<f32>(d.default_value) : 0.0f;
                const f32 speed = d.speed > 0.0f ? d.speed : (d.max > d.min ? (d.max - d.min) / 400.0f : 0.01f);
                const std::string fmt = number_format(d, false);
                changed = GLT::UI::property_drag(label, v, speed, d.min, d.max, fmt.c_str(), tip, &def);
                if (d.max > d.min)
                    v = std::clamp(v, d.min, d.max);
            } else if constexpr (std::is_same_v<T, i32>) {
                const i32 def = std::holds_alternative<i32>(d.default_value) ? std::get<i32>(d.default_value) : 0;
                if (d.kind == setting_kind::choice && !d.choices.empty()) {
                    std::vector<const char*> options;
                    size_t chars = 0;
                    for (const std::string& c : d.choices) {
                        options.push_back(c.c_str());
                        chars += c.size();
                    }
                    v = std::clamp(v, 0, static_cast<i32>(options.size()) - 1);
                    changed = options.size() <= 3 && chars <= 26 ? GLT::UI::property_segmented(label, v, options, tip, &def)
                                                                  : GLT::UI::property_combo(label, v, options, tip, &def);
                } else if (d.max > d.min) {
                    const std::string fmt = number_format(d, true);
                    changed = GLT::UI::property_slider(label, v, static_cast<i32>(d.min), static_cast<i32>(d.max), fmt.c_str(), tip, &def);
                } else {
                    changed = GLT::UI::property_custom(label, [&](f32 width) {
                        ImGui::SetNextItemWidth(width);
                        return ImGui::DragInt("##v", &v, 0.2f);
                    }, tip, v == def, [&] { v = def; });
                }
            } else if constexpr (std::is_same_v<T, bool>) {
                const bool def = std::holds_alternative<bool>(d.default_value) && std::get<bool>(d.default_value);
                changed = GLT::UI::property_toggle(label, v, tip, &def);
            } else if constexpr (std::is_same_v<T, glm::vec2>) {
                const glm::vec2 def = std::holds_alternative<glm::vec2>(d.default_value) ? std::get<glm::vec2>(d.default_value) : glm::vec2(0.0f);
                const f32 speed = d.speed > 0.0f ? d.speed : (d.max > d.min ? (d.max - d.min) / 400.0f : 0.01f);
                const std::string fmt = number_format(d, false);
                changed = GLT::UI::property_drag(label, v, speed, d.min, d.max, fmt.c_str(), tip, &def);
                if (d.max > d.min)
                    v = glm::clamp(v, glm::vec2(d.min), glm::vec2(d.max));
            } else if constexpr (std::is_same_v<T, glm::vec3>) {
                const glm::vec3 def = std::holds_alternative<glm::vec3>(d.default_value) ? std::get<glm::vec3>(d.default_value) : glm::vec3(0.0f);
                const f32 speed = d.speed > 0.0f ? d.speed : 0.01f;
                const std::string fmt = number_format(d, false);
                changed = GLT::UI::property_drag(label, v, speed, d.min, d.max, fmt.c_str(), tip, &def);
            } else if constexpr (std::is_same_v<T, profile_curve>) {
                const bool is_default = d.default_value == setting_value(v);
                const f32 y_min = d.max > d.min ? d.min : 0.0f;
                const f32 y_max = d.max > d.min ? d.max : std::max(1.25f, std::ceil(v.max_value() * 1.25f * 4.0f) / 4.0f);
                changed = GLT::UI::property_custom(label, [&](f32 width) { return profile_editor(d.name.c_str(), v, width, 70.0f, y_min, y_max); },
                                                   tip, is_default, [&] { if (const auto* def = std::get_if<profile_curve>(&d.default_value)) v = *def; });
            } else if constexpr (std::is_same_v<T, std::string>) {
                changed = GLT::UI::property_text(label, v, tip);
            }
        }, value);

        if (changed) {
            if (value == d.default_value)
                g.reset(d.name);
            else
                g.set(d.name, std::move(value));
        }
        return changed;
    }


    void construct_editor_window::draw_guide_section(guide& g) {

        ImGui::PushID(m_selection.guide);
        GLT::UI::card_options head{};
        head.summary = shape_label(g);
        if (GLT::UI::begin_card("Guide", head)) {
            std::string name = g.name;
            if (GLT::UI::property_text("Name", name, "Path of the guide: elements made from it are named after it") && !name.empty() && name != g.name) {
                guide renamed = g;
                renamed.name = name;
                renamed.index = -1;
                const i32 at = m_selection.guide;
                m_working.guides.erase(m_working.guides.begin() + at);
                make_unique(renamed);
                m_working.guides.insert(m_working.guides.begin() + at, std::move(renamed));
            }
            guide& current = m_working.guides[static_cast<size_t>(m_selection.guide)];

            const auto snap = registry_view();
            std::vector<const role_desc*> role_list;
            for (const auto& r : snap->roles)
                if (pack_active(r.pack))
                    role_list.push_back(r.desc.get());
            std::vector<const char*> role_names{ "Plain geometry" };
            i32 role = 0;
            for (size_t i = 0; i < role_list.size(); ++i) {
                role_names.push_back(role_list[i]->display.c_str());
                if (role_list[i]->name == current.role.str())
                    role = static_cast<i32>(i + 1);
            }
            if (GLT::UI::property_combo("Role", role, role_names, "What the stages do with this guide (plain geometry is meshed as it is)"))
                current.role = role == 0 ? role_id{} : intern(role_list[static_cast<size_t>(role - 1)]->name);
            if (const role_desc* r = snap->find_role(current.role); r && !r->description.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%s", r->description.c_str());
                ImGui::PopStyleColor();
            }

            glm::vec3 position(current.placement.origin);
            if (GLT::UI::property_drag("Position", position, 0.01f, 0.0f, 0.0f, "%.3f m"))
                current.placement.origin = glm::dvec3(position);
            glm::vec3 euler = glm::degrees(glm::eulerAngles(current.placement.rotation));
            if (GLT::UI::property_drag("Rotation", euler, 0.5f, 0.0f, 0.0f, "%.1f deg"))
                current.placement.rotation = glm::quat(glm::radians(euler));
            const glm::vec3 one(1.0f);
            if (GLT::UI::property_drag("Scale", current.placement.scale, 0.01f, 0.001f, 1000.0f, "%.3f", nullptr, &one))
                current.placement.scale = glm::max(current.placement.scale, glm::vec3(0.001f));

            if (GLT::UI::subtle_button("Duplicate")) {
                guide copy = current;
                copy.placement.origin += glm::dvec3(0.5, 0.0, 0.5);
                make_unique(copy);
                m_working.guides.push_back(std::move(copy));
                select_guide(static_cast<i32>(m_working.guides.size()) - 1);
            }
            ImGui::SameLine();
            if (GLT::UI::subtle_button("Delete")) {
                m_working.guides.erase(m_working.guides.begin() + m_selection.guide);
                m_selection = {};
                GLT::UI::end_card();
                ImGui::PopID();
                return;
            }
        }
        GLT::UI::end_card();

        guide& current = m_working.guides[static_cast<size_t>(m_selection.guide)];
        ImGui::Spacing();
        if (GLT::UI::begin_card("Shape", { .collapsible = true }))
            draw_shape(current);
        GLT::UI::end_card();

        if (std::holds_alternative<volume_guide>(current.shape) || std::holds_alternative<surface_guide>(current.shape)) {
            ImGui::Spacing();
            if (GLT::UI::begin_card("Edits", { .collapsible = true, .default_open = false }))
                draw_edits(current);
            GLT::UI::end_card();
        }

        if (const role_desc* r = registry_view()->find_role(current.role); r && !r->attributes.empty()) {
            ImGui::Spacing();
            const std::string header = std::format("{} settings", r->display.empty() ? r->name : r->display);
            if (GLT::UI::begin_card(header.c_str(), { .description = "This guide's own values (else the stage settings apply)", .collapsible = true }))
                for (const attribute_id id : r->attributes)
                    draw_attribute_row(current.attributes, id);
            GLT::UI::end_card();
        }

        ImGui::Spacing();
        if (GLT::UI::begin_card("Attributes", { .collapsible = true, .default_open = false }))
            draw_attributes(current);
        GLT::UI::end_card();
        ImGui::PopID();
    }


    void construct_editor_window::draw_element_properties() {

        const element_summary* e = find_element(m_selection.element);
        if (!e) {
            GLT::UI::empty_state("Element not found", "It is not made any more (the construct changed)");
            return;
        }
        const element_summary element = *e;                 // the buttons below may change the selection

        std::string stage_label = "none (guide / production)";
        if (const i32 i = stage_index(element.stage); i >= 0)
            stage_label = m_working.generators[static_cast<size_t>(i)].label;
        GLT::UI::card_options head{};
        const std::string kind_text(element.kind.str());
        head.summary = kind_text.c_str();
        if (GLT::UI::begin_card("Element", head)) {
            GLT::UI::property_value("Path", element.path.to_string(), "Stable identity: seeds and overrides key on it");
            GLT::UI::property_value("Stage", stage_label);
            GLT::UI::property_value("Kind", element.kind.str());
            GLT::UI::property_value("Made by", element.source);
            GLT::UI::property_value("Depth", std::to_string(element.depth));
            u32 products = 0;
            for (const product& p : m_result.products)
                products += p.element == element.path ? 1u : 0u;
            GLT::UI::property_value("Products", std::to_string(products));

            if (element.stage != 0 && GLT::UI::subtle_button("Select stage"))
                select_stage(element.stage);
            if (!element.parent.empty()) {
                ImGui::SameLine();
                if (GLT::UI::subtle_button("Select parent"))
                    select_element(element.parent);
            }
            ImGui::SameLine();
            if (GLT::UI::subtle_button("Hide element")) {
                element_override* o = nullptr;
                for (element_override& x : m_working.overrides)
                    if (x.path == element.path)
                        o = &x;
                if (!o) {
                    m_working.overrides.push_back({ element.path, true, {} });
                } else
                    o->hidden = true;
                m_selection = {};
            }
            tooltip("Removes this element (and what grows on it) from the construct. Undo, or Construct > Show hidden elements, brings it back");
        }
        GLT::UI::end_card();

        ImGui::Spacing();
        const std::string count = std::to_string(element.attributes.entries().size());
        if (GLT::UI::begin_card("Attributes", { .summary = count.c_str(), .collapsible = true })) {
            if (element.attributes.entries().empty())
                ImGui::TextDisabled("None");
            for (const auto& a : element.attributes.entries())
                GLT::UI::property_value(std::string(a.id.str()).c_str(), to_string(a.value));
        }
        GLT::UI::end_card();
    }


    void construct_editor_window::draw_presets() {

        const auto snap = registry_view();
        if (GLT::UI::begin_card("Start from a preset", { .description = "Presets of the loaded packs. File > New lists them too" })) {
            if (snap->presets.empty())
                ImGui::TextDisabled("No pack with presets is loaded (enable a construct plugin)");
            std::string pack;
            for (const auto& p : snap->presets) {
                if (p.pack != pack) {
                    pack = p.pack;
                    const pack_desc* pd = snap->find_pack(pack);
                    ImGui::SeparatorText(pd && !pd->display.empty() ? pd->display.c_str() : pack.c_str());
                }
                ImGui::PushID(p.desc->name.c_str());
                if (GLT::UI::accent_button(p.desc->display.c_str(), ImVec2(-FLT_MIN, 0.0f)) && p.desc->make)
                    take(p.desc->make(), true);
                ImGui::PopID();
                if (!p.desc->description.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::TextWrapped("%s", p.desc->description.c_str());
                    ImGui::PopStyleColor();
                }
            }
        }
        GLT::UI::end_card();
        ImGui::Spacing();
    }


    void construct_editor_window::draw_construct_section() {

        if (m_working.generators.empty() && m_working.guides.empty())
            draw_presets();

        if (GLT::UI::begin_card("Construct", { .description = "Select a stage, a guide, or click something in the view" })) {
            // Guides whose role no stage uses: offer the stage types that would
            const auto snap = registry_view();
            std::vector<role_id> seen;
            for (const guide& g : m_working.guides) {
                if (g.role.empty() || std::ranges::find(seen, g.role) != seen.end())
                    continue;
                seen.push_back(g.role);
                std::vector<const generator_desc*> users;
                bool used = false;
                for (const auto& r : snap->generators)
                    if (pack_active(r.pack) && std::ranges::find(r.desc->guide_roles, g.role) != r.desc->guide_roles.end()) {
                        users.push_back(r.desc.get());
                        used = used || std::ranges::any_of(m_working.generators, [&](const generator_instance& s) { return s.type == r.desc->name && s.enabled; });
                    }
                if (used || users.empty())
                    continue;
                const role_desc* rd = snap->find_role(g.role);
                ImGui::TextColored(TEXT_WARN, "Guides with role %s are placed, but no stage uses them", rd ? rd->display.c_str() : std::string(g.role.str()).c_str());
                for (const generator_desc* u : users) {
                    const std::string label = std::format("Add a {} stage##{}", u->display, u->name);
                    if (GLT::UI::subtle_button(label.c_str()))
                        add_stage(*u);
                }
            }
            glm::vec3 origin(m_working.origin);
            const glm::vec3 zero(0.0f);
            if (GLT::UI::property_drag("Origin", origin, 0.05f, 0.0f, 0.0f, "%.2f m", "The point the meshes are expressed relative to (the entity origin)", &zero))
                m_working.origin = glm::dvec3(origin);
            std::string packs_text;
            for (const std::string& p : m_working.packs)
                packs_text += (packs_text.empty() ? "" : ", ") + p;
            GLT::UI::property_value("Packs", packs_text.empty() ? "(all loaded)" : packs_text);
            GLT::UI::property_value("Stages / guides", std::format("{} / {}", m_working.generators.size(), m_working.guides.size()));

            const auto hidden = std::ranges::count_if(m_working.overrides, [](const element_override& o) { return o.hidden; });
            if (hidden > 0) {
                GLT::UI::property_value("Hidden elements", std::to_string(hidden));
                if (GLT::UI::subtle_button("Show hidden elements"))
                    for (element_override& o : m_working.overrides)
                        o.hidden = false;
                std::erase_if(m_working.overrides, [](const element_override& o) { return !o.hidden && o.attributes.entries().empty(); });
            }
        }
        GLT::UI::end_card();
    }


    void construct_editor_window::draw_diagnostics() {

        u32 errors = 0, warnings = 0;
        for (const diagnostic& d : m_result.diagnostics) {
            errors += d.level == severity::error ? 1u : 0u;
            warnings += d.level == severity::warning ? 1u : 0u;
        }
        const std::string summary = !m_has_result ? std::string("not generated yet")
                                  : (errors || warnings ? std::format("{} error(s), {} warning(s)", errors, warnings) : std::string("none"));
        if (GLT::UI::begin_card("Problems", { .summary = summary.c_str(), .collapsible = true, .default_open = errors + warnings > 0 })) {
            for (const diagnostic& d : m_result.diagnostics) {
                const ImVec4 color = d.level == severity::error ? TEXT_ERROR : d.level == severity::warning ? TEXT_WARN : ImVec4(0.7f, 0.7f, 0.75f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextWrapped("[%s] %s%s%s", d.source.c_str(), d.element.to_string().c_str(), d.element.empty() ? "" : ": ", d.message.c_str());
                ImGui::PopStyleColor();
            }
            if (m_has_result && ImGui::TreeNode("Pipeline timings")) {
                for (const stage_timing& t : m_result.timings)
                    ImGui::Text("%-22s %7.2f ms", t.stage.c_str(), t.milliseconds);
                ImGui::TreePop();
            }
        }
        GLT::UI::end_card();
    }

    // guide details -----------------------------------------------------------------------------------------------------

    bool construct_editor_window::draw_shape(guide& g) {

        bool changed = false;
        std::visit([&](auto& s) {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, volume_guide>) {
                static constexpr const char* kinds[] = { "Box", "Cylinder", "Sphere", "Capsule", "Plane" };
                i32 kind = static_cast<i32>(s.primitive);
                if (GLT::UI::property_combo("Primitive", kind, kinds)) {
                    s.primitive = static_cast<geometry::primitive_kind>(kind);
                    changed = true;
                }
                geometry::primitive_params& p = s.params;
                const auto pk = s.primitive;
                if (pk == geometry::primitive_kind::box || pk == geometry::primitive_kind::plane)
                    changed |= GLT::UI::property_drag("Size", p.size, 0.01f, 0.001f, 10000.0f, "%.3f m");
                if (pk == geometry::primitive_kind::cylinder || pk == geometry::primitive_kind::sphere || pk == geometry::primitive_kind::capsule) {
                    changed |= GLT::UI::property_drag("Radius", p.radius, 0.01f, 0.001f, 10000.0f, "%.3f m");
                    i32 seg = static_cast<i32>(p.segments);
                    if (GLT::UI::property_slider("Segments", seg, 3, 64)) { p.segments = static_cast<u32>(seg); changed = true; }
                }
                if (pk == geometry::primitive_kind::cylinder || pk == geometry::primitive_kind::capsule)
                    changed |= GLT::UI::property_drag("Height", p.height, 0.01f, 0.0f, 10000.0f, "%.3f m");
                if (pk == geometry::primitive_kind::sphere || pk == geometry::primitive_kind::capsule) {
                    i32 rings = static_cast<i32>(p.rings);
                    if (GLT::UI::property_slider("Rings", rings, 2, 48)) { p.rings = static_cast<u32>(rings); changed = true; }
                }
                if (pk == geometry::primitive_kind::cylinder)
                    changed |= GLT::UI::property_toggle("Caps", p.caps);
                if (pk == geometry::primitive_kind::plane)
                    changed |= GLT::UI::property_custom("Divisions", [&](f32 width) {
                        i32 div[2] = { static_cast<i32>(p.divisions.x), static_cast<i32>(p.divisions.y) };
                        ImGui::SetNextItemWidth(width);
                        if (!ImGui::SliderInt2("##div", div, 1, 64))
                            return false;
                        p.divisions = glm::uvec2(div[0], div[1]);
                        return true;
                    });
                static constexpr const char* sides[] = { "Solid", "Cavity" };
                i32 side = static_cast<i32>(s.side);
                if (GLT::UI::property_segmented("Material side", side, sides, "Solid: the inside is material. Cavity: the inside is empty space (tunnels, rooms)")) {
                    s.side = static_cast<orientation>(side);
                    changed = true;
                }
            } else if constexpr (std::is_same_v<T, curve_guide>) {
                static constexpr const char* types[] = { "Polyline", "Smooth" };
                i32 type = static_cast<i32>(s.type);
                if (GLT::UI::property_segmented("Type", type, types, "Smooth: a centripetal Catmull-Rom spline through every point")) {
                    s.type = static_cast<geometry::curve_type>(type);
                    changed = true;
                }
                changed |= GLT::UI::property_toggle("Closed", s.closed);

                // Per control point columns: the attributes the guide's role lists (radius, ...)
                struct column { attribute_id id; std::string label; f32 default_value; f32 min; f32 max; f32 speed; };
                std::vector<column> columns;
                const auto snap = registry_view();
                if (const role_desc* r = snap->find_role(g.role))
                    for (const attribute_id id : r->point_attributes) {
                        const attribute_desc* d = snap->find_attribute(id);
                        const bool bounded = d && d->max > d->min;
                        columns.push_back({ id, d && !d->display.empty() ? d->display : std::string(id.str()),
                                            d ? static_cast<f32>(as_number(d->default_value)) : 0.0f, bounded ? d->min : 0.0f,
                                            bounded ? d->max : 0.0f, bounded ? (d->max - d->min) / 1000.0f : 0.002f });
                    }
                const int column_count = 2 + static_cast<int>(columns.size());
                if (ImGui::BeginTable("##points", column_count, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 26.0f);
                    ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthStretch, 3.0f);
                    for (const column& col : columns)
                        ImGui::TableSetupColumn(col.label.c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableHeadersRow();
                    i32 remove = -1;
                    for (size_t i = 0; i < s.points.size(); ++i) {
                        ImGui::PushID(static_cast<int>(i));
                        ImGui::TableNextRow();
                        if (m_point == static_cast<i32>(i))
                            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(255, 190, 70, 50));
                        ImGui::TableNextColumn();
                        if (ImGui::SmallButton(std::to_string(i).c_str()))
                            m_point = m_point == static_cast<i32>(i) ? -1 : static_cast<i32>(i);
                        tooltip("Select this point (the gizmo moves it). Right click removes it");
                        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                            remove = static_cast<i32>(i);
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        changed |= ImGui::DragFloat3("##p", &s.points[i].x, 0.01f);
                        for (size_t c = 0; c < columns.size(); ++c) {
                            ImGui::TableNextColumn();
                            ImGui::PushID(static_cast<int>(c));
                            std::vector<attribute_value>& values = point_column(s, columns[c].id, columns[c].default_value);
                            f32 v = values[i].index() ? static_cast<f32>(as_number(values[i])) : columns[c].default_value;
                            ImGui::SetNextItemWidth(-FLT_MIN);
                            if (ImGui::DragFloat("##a", &v, columns[c].speed, columns[c].min, columns[c].max, "%.3f")) {
                                values[i] = v;
                                changed = true;
                            }
                            ImGui::PopID();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                    if (remove >= 0 && s.points.size() > 2) {
                        s.points.erase(s.points.begin() + remove);
                        for (auto& [id, values] : s.point_attributes)
                            if (remove < static_cast<i32>(values.size()))
                                values.erase(values.begin() + remove);
                        m_point = -1;
                        changed = true;
                    }
                }
                if (GLT::UI::subtle_button("Add point", ImVec2(-FLT_MIN, 0.0f))) {
                    const size_t n = s.points.size();
                    const glm::vec3 next = n >= 2 ? s.points[n - 1] * 2.0f - s.points[n - 2] : (n == 1 ? s.points[0] + glm::vec3(0, 1, 0) : glm::vec3(0.0f));
                    s.points.push_back(next);
                    for (auto& [id, values] : s.point_attributes)
                        values.push_back(values.empty() ? attribute_value{} : values.back());
                    m_point = static_cast<i32>(s.points.size()) - 1;
                    changed = true;
                }
            } else if constexpr (std::is_same_v<T, surface_guide>) {
                changed |= GLT::UI::property_drag("Size", s.size, 0.01f, 0.001f, 10000.0f, "%.3f m");
                changed |= GLT::UI::property_custom("Divisions", [&](f32 width) {
                    i32 div[2] = { static_cast<i32>(s.divisions.x), static_cast<i32>(s.divisions.y) };
                    ImGui::SetNextItemWidth(width);
                    if (!ImGui::SliderInt2("##div", div, 1, 64))
                        return false;
                    s.divisions = glm::uvec2(div[0], div[1]);
                    return true;
                });
            } else {
                i32 remove = -1;
                for (size_t i = 0; i < s.points.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::SmallButton(std::to_string(i).c_str()))
                        m_point = static_cast<i32>(i);
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                        remove = static_cast<i32>(i);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    changed |= ImGui::DragFloat3("##p", &s.points[i].x, 0.01f);
                    ImGui::PopID();
                }
                if (remove >= 0 && s.points.size() > 1) {
                    s.points.erase(s.points.begin() + remove);
                    m_point = -1;
                    changed = true;
                }
                if (GLT::UI::subtle_button("Add point", ImVec2(-FLT_MIN, 0.0f))) {
                    s.points.push_back(s.points.empty() ? glm::vec3(0.0f) : s.points.back() + glm::vec3(0.5f, 0.0f, 0.0f));
                    changed = true;
                }
            }
        }, g.shape);
        return changed;
    }


    bool construct_editor_window::draw_edits(guide& g) {

        bool changed = false;
        i32 remove = -1;
        for (size_t i = 0; i < g.edits.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            std::visit([&](auto& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, edit_loop_cut>) {
                    GLT::UI::property_value("Step", "Loop cut");
                    changed |= GLT::UI::property_drag("Near point", e.near_point, 0.01f, 0.0f, 0.0f, "%.3f",
                                                      "The cut goes through the edge ring of the edge closest to this point (guide space)");
                    i32 cuts = static_cast<i32>(e.positions.size());
                    if (GLT::UI::property_slider("Cuts", cuts, 1, 16)) {
                        e.positions.clear();
                        for (i32 k = 1; k <= cuts; ++k)
                            e.positions.push_back(static_cast<f32>(k) / static_cast<f32>(cuts + 1));
                        changed = true;
                    }
                } else if constexpr (std::is_same_v<T, edit_transform>) {
                    GLT::UI::property_value("Step", "Transform");
                } else if constexpr (std::is_same_v<T, edit_assign_group>) {
                    GLT::UI::property_value("Step", std::format("Assign group [{}]", e.group.str()));
                } else {
                    GLT::UI::property_value("Step", std::format("Set [{}] = {}", e.id.str(), to_string(e.value)));
                }
            }, g.edits[i]);
            if (GLT::UI::subtle_button("Remove step"))
                remove = static_cast<i32>(i);
            ImGui::Separator();
            ImGui::PopID();
        }
        if (remove >= 0) {
            g.edits.erase(g.edits.begin() + remove);
            changed = true;
        }
        if (GLT::UI::subtle_button("Add loop cut", ImVec2(-FLT_MIN, 0.0f))) {
            g.edits.push_back(edit_loop_cut{ .near_point = glm::vec3(0.0f, 0.5f, 0.0f), .positions = { 0.5f } });
            changed = true;
        }

        bool mirror = g.modifiers.mirror_plane.has_value();
        if (GLT::UI::property_toggle("Mirror", mirror)) {
            g.modifiers.mirror_plane = mirror ? std::optional<glm::vec4>(glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)) : std::nullopt;
            changed = true;
        }
        if (g.modifiers.mirror_plane) {
            static constexpr const char* axes[] = { "X", "Y", "Z" };
            glm::vec4& plane = *g.modifiers.mirror_plane;
            i32 axis = plane.y != 0.0f ? 1 : (plane.z != 0.0f ? 2 : 0);
            if (GLT::UI::property_segmented("Across", axis, axes, "X: left / right, Y: up / down, Z: front / back")) {
                plane = glm::vec4(axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f, axis == 2 ? 1.0f : 0.0f, plane.w);
                changed = true;
            }
            f32 offset = -plane.w;
            if (GLT::UI::property_drag("Offset", offset, 0.01f, 0.0f, 0.0f, "%.3f m", "Mirror plane position along the axis, in the guide's own space")) {
                plane.w = -offset;
                changed = true;
            }
        }
        return changed;
    }


    bool construct_editor_window::draw_attributes(guide& g) {

        bool changed = false;
        std::vector<attribute_set::entry> entries(g.attributes.entries().begin(), g.attributes.entries().end());
        for (size_t i = 0; i < entries.size(); ++i) {
            attribute_set::entry& e = entries[i];
            ImGui::PushID(static_cast<int>(i));
            const std::string label(e.id.str());
            const bool edited = GLT::UI::property_custom(label.c_str(), [&](f32 width) {
                ImGui::SetNextItemWidth(std::max(width - ImGui::GetFrameHeight() - 4.0f, 30.0f));
                bool result = std::visit([&](auto& v) -> bool {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, f32>)            return ImGui::DragFloat("##v", &v, 0.01f);
                    else if constexpr (std::is_same_v<T, i32>)       return ImGui::DragInt("##v", &v);
                    else if constexpr (std::is_same_v<T, bool>)      return ImGui::Checkbox("##v", &v);
                    else if constexpr (std::is_same_v<T, glm::vec3>) return ImGui::DragFloat3("##v", &v.x, 0.01f);
                    else { ImGui::TextDisabled("%s", to_string(attribute_value(v)).c_str()); return false; }
                }, e.value);
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) {
                    g.attributes.erase(e.id, e.region);
                    changed = true;
                    return false;
                }
                tooltip("Remove (the default applies again)");
                return result;
            });
            if (edited) {
                g.attributes.set(e.id, e.value, e.region);
                changed = true;
            }
            ImGui::PopID();
        }
        if (entries.empty())
            ImGui::TextDisabled("No attributes set (the tool section sets the common ones)");
        return changed;
    }

}
