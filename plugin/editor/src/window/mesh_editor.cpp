#include "util/pch.h"
#include "mesh_editor.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <unordered_set>

#include <util/util.h>
#include <application.h>
#include <config/imgui_config.h>
#include <plugin_system/plugin_manager.h>
#include <render/i_renderer.h>
#include <render/i_debug_draw.h>
#include <render/i_scene_renderer.h>
#include <world/camera_snapshot.h>
#include <world/light_snapshot.h>
#include <asset/type.h>
#include <asset/i_asset_registry.h>

#include "util/ui/panel_collection.h"
#include "util/ui/asset_picker.h"
#include "resource_manager/icon_manager.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    constexpr f32                                   MIN_PREVIEW_SIDE = 64.0f;

    constexpr f32                                   MIN_CAM_DISTANCE = 0.05f;

    constexpr f32                                   MAX_CAM_DISTANCE = 1500.0f;

    constexpr u32                                   DEFAULT_PREVIEW_SIZE = 512;

    constexpr const char*                           DRAG_PAYLOAD_ID = "CONTENT_BROWSER_ITEM";

    // Panel layout defaults for the three-column body
    constexpr f32                                   SPLITTER_WIDTH = 4.0f;

    constexpr f32                                   MIN_OUTLINER_WIDTH = 150.0f;

    constexpr f32                                   MIN_INSPECTOR_WIDTH = 200.0f;

    constexpr f32                                   BODY_MIN_WIDTH = 400.0f;

    constexpr f32                                   BODY_MIN_HEIGHT = 200.0f;

    // LOD wireframe overlay: edges above this are not drawn (debug lines are immediate, one virtual call each)
    constexpr u32                                   MAX_WIREFRAME_EDGES = 100000;     // renderer MAX_DEBUG_VERTICES = 262144

    // m_wire_lod while the wireframe cache holds the triangles of a cluster selection instead of a discrete level
    constexpr i32                                   WIRE_LEVEL_CLUSTERS = -3;

    // Cluster bounds overlay: one wire sphere (3 circles of CLUSTER_BOUNDS_SEGMENTS lines) per selected cluster, the
    // nearest ones first
    constexpr u32                                   MAX_CLUSTER_BOUNDS = 1000;
    constexpr u32                                   CLUSTER_BOUNDS_SEGMENTS = 16;

    // Render modes of this category are the renderer's virtual geometry debug views (the editor only knows the name)
    constexpr const char*                           VIRTUAL_GEOMETRY_MODE_CATEGORY = "Virtual Geometry";

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // Core-GLM TRS extraction. Assumes a well-formed affine matrix (no skew/projection).
    // Safe to call on any TRS product including ones where the gizmo baked scale in.
    inline void decompose_trs(const glm::mat4& m, glm::vec3& out_t, glm::quat& out_r, glm::vec3& out_s);


    void draw_box_wireframe(GLT::render::i_debug_draw& debug, const glm::mat4& xform, const glm::vec3& he, const glm::vec4& color);


    void draw_cylinder_wireframe(GLT::render::i_debug_draw& debug, const glm::vec3& position, const glm::quat& rotation,
        f32 radius, f32 half_height, const glm::vec4& color, u32 segments = 16);


    void draw_capsule_wireframe(GLT::render::i_debug_draw& debug, const glm::vec3& position, const glm::quat& rotation,
        f32 radius, f32 half_height, const glm::vec4& color);


    void draw_panel_splitter(const char* id, f32& width, f32 min_w, f32 max_w, bool invert, f32 height);


    // Push a scale vector into the shape parameters of an editor item. For box we can use per-axis scale; the primitive
    // "single radius" shapes have to collapse to a representative scalar, otherwise their invariant breaks
    void apply_scale_to_collider(collider_editor_item& item, const glm::vec3& scale);


    bool editor_collider_to_mesh(const collider_editor_item& item, GLT::asset::mesh::mesh_collider& out);


    bool mesh_collider_to_editor(const GLT::asset::mesh::mesh_collider& mc, collider_editor_item& out, u64 id);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // 1234 -> "1,234", 1234567 -> "1.23M"
    static std::string format_count(u64 value) {

        char buf[32];
        if (value >= 10'000'000ull)
            std::snprintf(buf, sizeof(buf), "%.2fM", static_cast<f64>(value) / 1e6);
        else if (value >= 100'000ull)
            std::snprintf(buf, sizeof(buf), "%.1fk", static_cast<f64>(value) / 1e3);
        else if (value >= 1'000ull)
            std::snprintf(buf, sizeof(buf), "%llu,%03llu", static_cast<unsigned long long>(value / 1000), static_cast<unsigned long long>(value % 1000));
        else
            std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(value));
        return buf;
    }


    inline void decompose_trs(const glm::mat4& m, glm::vec3& out_t, glm::quat& out_r, glm::vec3& out_s) {

        out_t = glm::vec3(m[3]);

        const glm::vec3 c0(m[0]);
        const glm::vec3 c1(m[1]);
        const glm::vec3 c2(m[2]);

        out_s = glm::vec3(glm::length(c0), glm::length(c1), glm::length(c2));

        // Guard against a degenerate (zero-scale) matrix so normalize() doesn't NaN out
        const glm::vec3 n0 = (out_s.x > 1e-8f) ? c0 / out_s.x : glm::vec3(1, 0, 0);
        const glm::vec3 n1 = (out_s.y > 1e-8f) ? c1 / out_s.y : glm::vec3(0, 1, 0);
        const glm::vec3 n2 = (out_s.z > 1e-8f) ? c2 / out_s.z : glm::vec3(0, 0, 1);

        out_r = glm::quat_cast(glm::mat3(n0, n1, n2));
    }


    void draw_box_wireframe(GLT::render::i_debug_draw& debug, const glm::mat4& xform, const glm::vec3& he, const glm::vec4& color) {

        static constexpr glm::vec3 LOCAL[8] = {
            { -1.f, -1.f, -1.f }, { +1.f, -1.f, -1.f }, { +1.f, +1.f, -1.f }, { -1.f, +1.f, -1.f },
            { -1.f, -1.f, +1.f }, { +1.f, -1.f, +1.f }, { +1.f, +1.f, +1.f }, { -1.f, +1.f, +1.f },
        };

        static constexpr int EDGES[12][2] = {
            { 0,1 },{ 1,2 },{ 2,3 },{ 3,0 },
            { 4,5 },{ 5,6 },{ 6,7 },{ 7,4 },
            { 0,4 },{ 1,5 },{ 2,6 },{ 3,7 },
        };

        glm::vec3 corners[8];
        for (int i = 0; i < 8; ++i)
            corners[i] = glm::vec3(xform * glm::vec4(LOCAL[i] * he, 1.0f));

        for (const auto& e : EDGES)
            debug.line(corners[e[0]], corners[e[1]], color, 0.0f, true);
    }


    void draw_cylinder_wireframe(GLT::render::i_debug_draw& debug, const glm::vec3& position, const glm::quat& rotation,
        f32 radius, f32 half_height, const glm::vec4& color, u32 segments) {

        const glm::vec3 axis = rotation * glm::vec3(0, 1, 0);

        glm::vec3 tangent = glm::cross(axis, glm::vec3(1, 0, 0));
        if (glm::dot(tangent, tangent) < 1e-4f)
            tangent = glm::cross(axis, glm::vec3(0, 0, 1));
        tangent = glm::normalize(tangent);
        const glm::vec3 bitangent = glm::normalize(glm::cross(axis, tangent));

        const glm::vec3 top_center = position + axis * half_height;
        const glm::vec3 bot_center = position - axis * half_height;

        std::vector<glm::vec3> top_pts, bot_pts;
        top_pts.reserve(segments);
        bot_pts.reserve(segments);

        for (u32 i = 0; i < segments; ++i) {
            const f32 a = glm::two_pi<f32>() * static_cast<f32>(i) / static_cast<f32>(segments);
            const glm::vec3 offset = (std::cos(a) * tangent + std::sin(a) * bitangent) * radius;
            top_pts.push_back(top_center + offset);
            bot_pts.push_back(bot_center + offset);
        }

        debug.polyline(top_pts, color, 0.0f, true, true);
        debug.polyline(bot_pts, color, 0.0f, true, true);

        for (u32 i = 0; i < 4; ++i) {
            const u32 idx = i * segments / 4;
            debug.line(top_pts[idx], bot_pts[idx], color, 0.0f, true);
        }
    }


    void draw_capsule_wireframe(GLT::render::i_debug_draw& debug, const glm::vec3& position, const glm::quat& rotation,
        f32 radius, f32 half_height, const glm::vec4& color) {

        const glm::vec3 axis = rotation * glm::vec3(0, 1, 0);
        const glm::vec3 a = position - axis * half_height;
        const glm::vec3 b = position + axis * half_height;

        debug.capsule(a, b, radius, color);
    }


    void draw_panel_splitter(const char* id, f32& width, f32 min_w, f32 max_w, bool invert, f32 height) {

        const ImVec2 cursor = ImGui::GetCursorScreenPos();

        ImGui::InvisibleButton(id, ImVec2(SPLITTER_WIDTH, height));

        const bool hovered = ImGui::IsItemHovered();
        const bool active  = ImGui::IsItemActive();

        if (active) {
            const f32 delta = ImGui::GetIO().MouseDelta.x;
            width = std::clamp(width + (invert ? -delta : delta), min_w, max_w);
        }

        if (hovered || active)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        // Thin line, accent colored while hovered / dragged
        const f32 cx = cursor.x + SPLITTER_WIDTH * 0.5f;
        if (hovered || active)
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(cx - 1.0f, cursor.y), ImVec2(cx + 1.0f, cursor.y + height),
                ImGui::GetColorU32(active ? GLT::imgui_config::get_action_color00_active_ref() : GLT::imgui_config::get_action_color00_hover_ref()));
        else
            ImGui::GetWindowDrawList()->AddLine(ImVec2(cx, cursor.y), ImVec2(cx, cursor.y + height), ImGui::GetColorU32(ImGuiCol_Separator));
    }


    void apply_scale_to_collider(collider_editor_item& item, const glm::vec3& scale) {

        switch (item.kind) {

            case collider_shape_kind::box:
                item.box_half_extents *= scale;
                break;

            case collider_shape_kind::sphere:
                item.sphere_radius *= (scale.x + scale.y + scale.z) / 3.0f;
                break;

            case collider_shape_kind::capsule:
                item.capsule_radius      *= (scale.x + scale.z) * 0.5f;
                item.capsule_half_height *= scale.y;
                break;

            case collider_shape_kind::cylinder:
                item.cylinder_radius      *= (scale.x + scale.z) * 0.5f;
                item.cylinder_half_height *= scale.y;
                break;

            default: break;
        }
    }


    bool editor_collider_to_mesh(const collider_editor_item& item, GLT::asset::mesh::mesh_collider& out) {

        switch (item.kind) {

            case collider_shape_kind::box:
                out.shape = GLT::asset::mesh::collider_shape::box;
                out.box.half_extents = item.box_half_extents;
                break;

            case collider_shape_kind::sphere:
                out.shape = GLT::asset::mesh::collider_shape::sphere;
                out.sphere.radius = item.sphere_radius;
                break;

            case collider_shape_kind::capsule:
                out.shape = GLT::asset::mesh::collider_shape::capsule;
                out.capsule.radius      = item.capsule_radius;
                out.capsule.half_height = item.capsule_half_height;
                break;

            case collider_shape_kind::cylinder:
                out.shape = GLT::asset::mesh::collider_shape::cylinder;
                out.cylinder.radius      = item.cylinder_radius;
                out.cylinder.half_height = item.cylinder_half_height;
                break;

            case collider_shape_kind::triangle_mesh:
                out.shape = GLT::asset::mesh::collider_shape::triangle_mesh;
                break;

            case collider_shape_kind::convex_hull:
                return false;       // runtime collider format has no convex_hull yet
        }

        out.material        = item.material;
        out.local_transform = glm::translate(glm::mat4(1.0f), item.position) * glm::mat4_cast(item.rotation);
        return true;
    }


    bool mesh_collider_to_editor(const GLT::asset::mesh::mesh_collider& mc, collider_editor_item& out, u64 id) {

        switch (mc.shape) {

            case GLT::asset::mesh::collider_shape::box:
                out.kind = collider_shape_kind::box;
                out.box_half_extents = mc.box.half_extents;
                break;

            case GLT::asset::mesh::collider_shape::sphere:
                out.kind = collider_shape_kind::sphere;
                out.sphere_radius = mc.sphere.radius;
                break;

            case GLT::asset::mesh::collider_shape::capsule:
                out.kind = collider_shape_kind::capsule;
                out.capsule_radius      = mc.capsule.radius;
                out.capsule_half_height = mc.capsule.half_height;
                break;

            case GLT::asset::mesh::collider_shape::cylinder:
                out.kind = collider_shape_kind::cylinder;
                out.cylinder_radius      = mc.cylinder.radius;
                out.cylinder_half_height = mc.cylinder.half_height;
                break;

            case GLT::asset::mesh::collider_shape::triangle_mesh:
                out.kind = collider_shape_kind::triangle_mesh;
                break;

            default: return false;
        }

        out.id = id;
        out.name = std::string(collider_shape_kind_name(out.kind)) + " " + std::to_string(id);
        out.material = mc.material;

        glm::vec3 translation{}, scale{};
        glm::quat rotation{};
        decompose_trs(mc.local_transform, translation, rotation, scale);

        out.position = translation;
        out.rotation = rotation;

        // Bake any scale that snuck into the matrix into the shape params so the
        // runtime canonical form (unit-shape params + rigid transform) is preserved.
        if (glm::distance(scale, glm::vec3(1.0f)) > 1e-4f)
            apply_scale_to_collider(out, scale);

        return true;
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    const char* collider_shape_kind_name(collider_shape_kind k) noexcept {

        switch (k) {
            case collider_shape_kind::box:              return "Box";
            case collider_shape_kind::sphere:           return "Sphere";
            case collider_shape_kind::capsule:          return "Capsule";
            case collider_shape_kind::cylinder:         return "Cylinder";
            case collider_shape_kind::convex_hull:      return "Convex Hull";
            case collider_shape_kind::triangle_mesh:    return "Triangle Mesh";
        }
        return "Unknown";
    }

    // CLASS IMPLEMENTATION ============================================================================================

    mesh_editor_window::mesh_editor_window() { open(std::filesystem::path{}); }


    mesh_editor_window::mesh_editor_window(const std::filesystem::path& path) { open(path); }


    mesh_editor_window::~mesh_editor_window() { close_mesh(); }

    // CLASS PUBLIC ====================================================================================================

    void mesh_editor_window::open(const std::filesystem::path& path) {

        const bool same_mesh = (path == m_mesh_path);

        close_mesh();

        if (!same_mesh)
            reset_collider_state();

        m_mesh_path = path;
        m_mesh_name = path.empty() ? std::string("(no mesh)") : path.filename().replace_extension("").string();
        m_load_failed = false;

        cache_window_state();
        make_window_name((std::string("MESH: ") + m_mesh_name).c_str());

        if (path.empty()) {
            m_show_window = true;
            return;
        }

        std::error_code error{};
        VALIDATE(GLT::vfs::exists(PROJECT_CONTENT_DIR / path, error) && !error, m_load_failed = true; m_show_window = true; return,
            "", "file not found [{}]", (path.generic_string()))

        VALIDATE(!GLT::vfs::is_directory(PROJECT_CONTENT_DIR / path, error) && !error, m_load_failed = true; m_show_window = true; return,
            "", "path is a directory [{}]", (path.generic_string()))

        auto registry = GLT::asset::registry::get_ref();
        VALIDATE(registry, m_load_failed = true; m_show_window = true; return, "", "no asset registry")

        auto loaded = registry->load(path);
        VALIDATE(loaded, m_load_failed = true; m_show_window = true; return, "", "failed to load mesh [{}]", (path.generic_string()))

        if (!load_from_handle(std::move(*loaded))) {

            m_load_failed = true;
            m_show_window = true;
            return;
        }

        ensure_preview();
        reset_camera_to_fit();

        m_show_window = true;
    }


    void mesh_editor_window::close_mesh() {

        if (m_lod_job.valid())
            m_lod_job.wait();                   // works on copies; just make sure no result is applied to the next mesh
        m_lod_job = {};
        if (m_cluster_job.valid())
            m_cluster_job.wait();               // same for a running cluster build
        m_cluster_job = {};

        destroy_preview();

        m_asset_handle.reset();
        m_has_mesh = false;
        m_dirty = false;
        m_submesh_bounds_cache.clear();
        m_hovered_submesh = -1;
        m_selected_submesh = -1;
        m_hovered_collider = -1;
        m_selected_collider = -1;

        if (m_inspector_target == inspector_target::submesh ||
            m_inspector_target == inspector_target::collider) {
            m_inspector_target = inspector_target::mesh;
        }
    }


    void mesh_editor_window::window(const f32 /*delta_time*/) {

        m_preview_image_drawn = false;
        if (!m_show_window)
            return;

        apply_pending_dock();
        ImGui::SetNextWindowSizeConstraints(ImVec2(880.0f, 520.0f), ImVec2(std::numeric_limits<f32>::max(),
            std::numeric_limits<f32>::max()));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        const bool open = ImGui::Begin(m_window_id.c_str(), &m_show_window);
        ImGui::PopStyleVar();

        if (open) {

            handle_drag_drop();
            draw_toolbar();
            draw_body();
        }
        ImGui::End();
    }


    void mesh_editor_window::update(const f32 delta_time) {

        poll_lod_generation();
        poll_cluster_build();

        if (!m_preview_scene)
            return;

        if (m_auto_rotate)
            m_preview_camera.yaw_rad += (m_auto_rotate_speed / 100000.0f) * glm::two_pi<f32>() * delta_time;

        m_preview_scene->set_active_camera(build_camera_snapshot());
        m_preview_scene->set_active(m_preview_image_drawn);
        submit_preview_lights();

        if (m_preview_image_drawn)
            submit_debug_draws();
        else if (auto* debug = m_preview_scene->get_debug_draw())
            debug->clear();
    }


    bool mesh_editor_window::serialize(const std::filesystem::path& /*project_file*/, const GLT::serializer::option /*option*/) {
        return false;
    }


    bool mesh_editor_window::save() {

        if (!m_has_mesh || !m_asset_handle.valid())
            return false;

        auto* registry = GLT::asset::registry::get_ptr();
        if (!registry)
            return false;

        if (!sync_colliders_to_mesh())
            return false;

        auto result = registry->save(m_asset_handle.get());
        if (!result) {
            LOG(error, "mesh_editor: save failed for [{}] (error {}). Meshes currently return [no_handler] from the handler.",
                m_mesh_name, static_cast<int>(result.error()));
            return false;
        }

        m_dirty = false;
        return true;
    }

	// CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    // top-level panels -------------------------------------------------------------------------------------------------

    // Header bar: mesh icon, name, path, unsaved marker | Reload, Save
    void mesh_editor_window::draw_toolbar() {

        const f32 frame_h = ImGui::GetFrameHeight();
        const f32 bar_h = frame_h + 14.f;
        const ImVec2 bar_min = ImGui::GetCursorScreenPos();
        const f32 width = ImGui::GetContentRegionAvail().x;
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        draw_list->AddRectFilled(bar_min, ImVec2(bar_min.x + width, bar_min.y + bar_h), ImGui::GetColorU32(ImGuiCol_TitleBg));

        // buttons on the right
        const char* save_label = "Save";
        const char* reload_label = "Reload";
        const f32 pad = ImGui::GetStyle().FramePadding.x * 4.f;
        const f32 save_w = ImGui::CalcTextSize(save_label).x + pad;
        const f32 reload_w = ImGui::CalcTextSize(reload_label).x + pad;
        const f32 buttons_w = save_w + reload_w + 6.f;
        const f32 buttons_x = bar_min.x + width - buttons_w - 10.f;

        // icon + name + path on the left
        const f32 icon = frame_h;
        const ImVec2 icon_min(bar_min.x + 10.f, bar_min.y + (bar_h - icon) * 0.5f);
        const auto icon_data = icon_manager::get(icon_manager::icon::mesh_asset);
        draw_list->AddImage(icon_data.tex_ref, icon_min, ImVec2(icon_min.x + icon, icon_min.y + icon), icon_data.uv0, icon_data.uv1);

        f32 x = icon_min.x + icon + 8.f;
        const f32 text_y = bar_min.y + (bar_h - ImGui::GetTextLineHeight()) * 0.5f;
        const std::string name = m_has_mesh ? m_mesh_name : std::string("No mesh");
        ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::bold));
        draw_list->AddText(ImVec2(x, text_y), ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
        x += ImGui::CalcTextSize(name.c_str()).x + 10.f;
        ImGui::PopFont();

        if (m_dirty) {
            // amber "unsaved" pill
            const char* unsaved = "Unsaved changes";
            const ImVec2 size = ImGui::CalcTextSize(unsaved);
            const ImVec2 pill_min(x, text_y - 2.f);
            const ImVec2 pill_max(x + size.x + 22.f, text_y + size.y + 2.f);
            draw_list->AddRectFilled(pill_min, pill_max, IM_COL32(230, 180, 60, 40), (pill_max.y - pill_min.y) * 0.5f);
            draw_list->AddCircleFilled(ImVec2(x + 9.f, text_y + size.y * 0.5f), 3.f, IM_COL32(230, 180, 60, 255));
            draw_list->AddText(ImVec2(x + 16.f, text_y), IM_COL32(230, 200, 120, 255), unsaved);
            x = pill_max.x + 10.f;
        }

        if (!m_mesh_path.empty() && x < buttons_x - 40.f) {
            std::string path = m_mesh_path.generic_string();
            const f32 max_w = buttons_x - 10.f - x;
            while (path.size() > 4 && ImGui::CalcTextSize(path.c_str()).x > max_w)
                path.erase(0, 4);
            if (path.size() != m_mesh_path.generic_string().size())
                path = "..." + path;
            draw_list->AddText(ImVec2(x, text_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), path.c_str());
        }

        ImGui::SetCursorScreenPos(ImVec2(buttons_x, bar_min.y + (bar_h - frame_h) * 0.5f));
        ImGui::BeginDisabled(!m_has_mesh);
        if (UI::subtle_button(reload_label, ImVec2(reload_w, 0.f)))
            open(m_mesh_path);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Discard unsaved changes and load the asset again");
        ImGui::SameLine(0.f, 6.f);
        if (UI::accent_button(save_label, ImVec2(save_w, 0.f)))
            save();
        ImGui::EndDisabled();

        ImGui::SetCursorScreenPos(ImVec2(bar_min.x, bar_min.y + bar_h));
        ImGui::Dummy(ImVec2(0.f, 0.f));
    }


    void mesh_editor_window::draw_body() {

        const ImVec2 avail = ImGui::GetContentRegionAvail();

        if (avail.x < BODY_MIN_WIDTH || avail.y < BODY_MIN_HEIGHT)
            return;

        const ImVec2 start_pos = ImGui::GetCursorScreenPos();
        const f32    total_w   = avail.x;
        const f32    total_h   = avail.y;

        // Clamp the stored widths so they always leave room for the preview.
        // Any excess is trimmed proportionally so both sides stay visible.
        {
            const f32 min_preview_reserved = MIN_PREVIEW_SIDE + 2.0f * SPLITTER_WIDTH;
            const f32 max_combined         = total_w - min_preview_reserved;

            if (m_outliner_width + m_inspector_width > max_combined) {
                const f32 scale = max_combined / (m_outliner_width + m_inspector_width);
                m_outliner_width  = std::max(MIN_OUTLINER_WIDTH,  m_outliner_width  * scale);
                m_inspector_width = std::max(MIN_INSPECTOR_WIDTH, m_inspector_width * scale);
            }

            m_outliner_width  = std::max(m_outliner_width,  MIN_OUTLINER_WIDTH);
            m_inspector_width = std::max(m_inspector_width, MIN_INSPECTOR_WIDTH);
        }

        const f32 max_outliner_w = std::max(MIN_OUTLINER_WIDTH,
            total_w - m_inspector_width - MIN_PREVIEW_SIDE - 2.0f * SPLITTER_WIDTH);
        const f32 max_inspector_w = std::max(MIN_INSPECTOR_WIDTH,
            total_w - m_outliner_width - MIN_PREVIEW_SIDE - 2.0f * SPLITTER_WIDTH);

        // ---- outliner ----------------------------------------------------------------------------------------------
        ImGui::SetCursorScreenPos(start_pos);
        {
            ImVec4 sidebar_bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
            sidebar_bg.x = std::max(0.f, sidebar_bg.x - 0.03f);
            sidebar_bg.y = std::max(0.f, sidebar_bg.y - 0.03f);
            sidebar_bg.z = std::max(0.f, sidebar_bg.z - 0.03f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, sidebar_bg);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 8.f));
            ImGui::BeginChild("##outliner", ImVec2(m_outliner_width, total_h), ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            draw_outliner_panel();
            ImGui::EndChild();
        }

        // ---- left splitter -----------------------------------------------------------------------------------------
        ImGui::SetCursorScreenPos(ImVec2(start_pos.x + m_outliner_width, start_pos.y));
        draw_panel_splitter("##splitter_left", m_outliner_width,
            MIN_OUTLINER_WIDTH, max_outliner_w, false, total_h);

        // ---- preview -----------------------------------------------------------------------------------------------
        const f32 preview_x = start_pos.x + m_outliner_width + SPLITTER_WIDTH;
        const f32 preview_w = std::max(MIN_PREVIEW_SIDE,
            total_w - m_outliner_width - m_inspector_width - 2.0f * SPLITTER_WIDTH);

        ImGui::SetCursorScreenPos(ImVec2(preview_x, start_pos.y));
        ImGui::BeginChild("##preview_region", ImVec2(preview_w, total_h), false);
        draw_preview_panel();
        ImGui::EndChild();

        // ---- right splitter ----------------------------------------------------------------------------------------
        ImGui::SetCursorScreenPos(ImVec2(preview_x + preview_w, start_pos.y));
        draw_panel_splitter("##splitter_right", m_inspector_width,
            MIN_INSPECTOR_WIDTH, max_inspector_w, true, total_h);

        // ---- inspector ---------------------------------------------------------------------------------------------
        ImGui::SetCursorScreenPos(ImVec2(preview_x + preview_w + SPLITTER_WIDTH, start_pos.y));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 10.f));
        ImGui::BeginChild("##inspector", ImVec2(0.0f, total_h), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
        draw_inspector_panel();
        ImGui::EndChild();
    }


    // outliner --------------------------------------------------------------------------------------------------------

    namespace {

        constexpr f32 ROW_ROUNDING = 4.f;

        // Small caps section title with an item count; returns true if the optional "+" button was clicked
        bool outliner_section(const char* title, const u32 count, const char* add_tooltip = nullptr) {

            ImGui::Dummy(ImVec2(0.f, 6.f));
            ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::small));
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", title);
            ImGui::SameLine(0.f, 6.f);
            ImGui::TextDisabled("%u", count);
            ImGui::PopFont();

            if (!add_tooltip)
                return false;

            const f32 size = ImGui::GetFrameHeight();
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - size));
            const bool clicked = GLT::UI::subtle_button("+##section_add", ImVec2(size, size));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", add_tooltip);
            return clicked;
        }


        struct row_options {

            const char*                         right_text = nullptr;   // dim, right aligned
            const icon_manager::icon_data*      icon = nullptr;
            ImU32                               dot_color = 0;          // colored dot instead of an icon
            bool                                bold = false;
            bool                                dimmed = false;
            f32                                 right_reserved = 0.f;   // space for extra buttons drawn by the caller
        };

        // Rounded, full width list row. Returns true when clicked; [out_hovered] reports hover
        bool outliner_row(const char* id, const char* label, const bool selected, const row_options& opts, bool* out_hovered = nullptr) {

            const f32 row_h = ImGui::GetFrameHeight();
            const f32 width = ImGui::GetContentRegionAvail().x;
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 max(min.x + width, min.y + row_h);

            ImGui::SetNextItemAllowOverlap();
            const bool clicked = ImGui::InvisibleButton(id, ImVec2(width, row_h));
            const bool hovered = ImGui::IsItemHovered();
            if (out_hovered)
                *out_hovered = hovered;

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            if (selected) {
                draw_list->AddRectFilled(min, max, ImGui::GetColorU32(GLT::imgui_config::get_action_color00_faded_ref()), ROW_ROUNDING);
                draw_list->AddRectFilled(ImVec2(min.x, min.y + 4.f), ImVec2(min.x + 3.f, max.y - 4.f),
                    ImGui::GetColorU32(GLT::imgui_config::get_action_color00_hover_ref()), 1.5f);
            } else if (hovered)
                draw_list->AddRectFilled(min, max, ImGui::GetColorU32(GLT::imgui_config::get_action_color_gray_hover_ref()), ROW_ROUNDING);

            f32 x = min.x + 8.f;
            const f32 line_h = ImGui::GetTextLineHeight();
            const f32 text_y = min.y + (row_h - line_h) * 0.5f;

            if (opts.dot_color != 0) {
                draw_list->AddCircleFilled(ImVec2(x + 4.f, min.y + row_h * 0.5f), 4.f, opts.dot_color);
                x += 14.f;
            } else if (opts.icon && opts.icon->image_size.x > 0.f) {
                draw_list->AddImage(opts.icon->tex_ref, ImVec2(x, text_y), ImVec2(x + line_h, text_y + line_h), opts.icon->uv0, opts.icon->uv1);
                x += line_h + 6.f;
            }

            f32 right_x = max.x - 8.f - opts.right_reserved;
            if (opts.right_text && *opts.right_text) {
                const f32 w = ImGui::CalcTextSize(opts.right_text).x;
                right_x -= w;
                draw_list->AddText(ImVec2(right_x, text_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), opts.right_text);
                right_x -= 8.f;
            }

            if (opts.bold)
                ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::bold));
            draw_list->PushClipRect(ImVec2(x, min.y), ImVec2(std::max(x, right_x), max.y), true);
            draw_list->AddText(ImVec2(x, text_y), ImGui::GetColorU32(opts.dimmed ? ImGuiCol_TextDisabled : ImGuiCol_Text), label);
            draw_list->PopClipRect();
            if (opts.bold)
                ImGui::PopFont();

            return clicked;
        }


        // Icon tile + title + subtitle at the top of the inspector
        void inspector_header(const icon_manager::icon_type icon, const char* title, const std::string& subtitle) {

            const f32 frame_h = ImGui::GetFrameHeight();
            const ImVec2 start = ImGui::GetCursorScreenPos();
            const f32 tile = frame_h * 1.4f;
            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            draw_list->AddRectFilled(start, ImVec2(start.x + tile, start.y + tile),
                ImGui::GetColorU32(GLT::imgui_config::get_action_color_gray_default_ref()), 6.f);
            const auto data = icon_manager::get(icon);
            const f32 inset = tile * 0.22f;
            draw_list->AddImage(data.tex_ref, ImVec2(start.x + inset, start.y + inset), ImVec2(start.x + tile - inset, start.y + tile - inset),
                data.uv0, data.uv1);

            ImGui::SetCursorScreenPos(ImVec2(start.x + tile + 8.f, start.y - 1.f));
            ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::bold));
            ImGui::TextUnformatted(title);
            ImGui::PopFont();
            ImGui::SetCursorScreenPos(ImVec2(start.x + tile + 8.f, ImGui::GetCursorScreenPos().y));
            ImGui::TextDisabled("%s", subtitle.c_str());

            ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + tile + 10.f));
            ImGui::Dummy(ImVec2(0.f, 0.f));
        }


        // Red full-width button for destructive actions
        bool danger_button(const char* label) {

            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.f);
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(120, 40, 40, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(160, 50, 50, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(190, 60, 60, 255));
            const bool clicked = ImGui::Button(label, ImVec2(-FLT_MIN, 0.f));
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            return clicked;
        }


        // Translucent pill button for preview overlays, accent filled while [active]
        bool overlay_toggle(const char* label, const bool active) {

            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, GLT::imgui_config::get_action_color00_default_ref());
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GLT::imgui_config::get_action_color00_hover_ref());
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, GLT::imgui_config::get_action_color00_active_ref());
            }
            const bool clicked = ImGui::Button(label);
            if (active)
                ImGui::PopStyleColor(3);
            return clicked;
        }


        void warning_text(const char* text) {

            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 200, 110, 255));
            ImGui::TextWrapped("%s", text);
            ImGui::PopStyleColor();
        }

    }


    void mesh_editor_window::draw_outliner_panel() {

        if (m_load_failed) {
            UI::empty_state("Failed to load the mesh", "See the log for details");
            return;
        }

        if (!m_has_mesh) {
            UI::empty_state("No mesh loaded", "Drop a mesh asset here");
            return;
        }

        auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return;

        auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle);
        if (!mesh)
            return;

        // ---- mesh root row ----
        {
            const auto icon = icon_manager::get(icon_manager::icon::mesh_asset);
            const std::string tris = format_count(mesh->indices.size() / 3) + " tris";
            if (outliner_row("##mesh_root", m_mesh_name.c_str(), m_inspector_target == inspector_target::mesh,
                row_options{ .right_text = tris.c_str(), .icon = &icon, .bold = true }))
                select_mesh();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Mesh settings: summary, geometry detail (LODs or virtual geometry), preview");
        }

        // ---- submeshes ----
        i32 new_hovered_sub = -1;
        outliner_section("SUBMESHES", mesh->submesh_count());
        {
            const auto icon = icon_manager::get(icon_manager::icon::material);
            for (u32 i = 0; i < mesh->submesh_count(); ++i) {

                ImGui::PushID(static_cast<int>(i));
                const bool selected = (m_inspector_target == inspector_target::submesh && m_selected_submesh == static_cast<i32>(i));
                const std::string label = std::format("Submesh {}", i);
                const std::string tris = format_count(mesh->submeshes[i].index_count / 3) + " tris";
                bool hovered = false;
                if (outliner_row("##submesh", label.c_str(), selected, row_options{ .right_text = tris.c_str(), .icon = &icon }, &hovered)) {
                    if (selected) select_mesh();
                    else          select_submesh(static_cast<i32>(i));
                }
                if (hovered)
                    new_hovered_sub = static_cast<i32>(i);
                ImGui::PopID();
            }
            if (mesh->submeshes.empty())
                ImGui::TextDisabled("  none");
        }

        // ---- geometry detail: the levels of the active mode (click = lock the preview to that level, click again =
        //      automatic). Virtual geometry: the discrete levels are the fallback ----
        const bool virtual_mode = mesh->detail() == GLT::asset::mesh::detail_mode::virtual_geometry;
        if (virtual_mode) {

            outliner_section("VIRTUAL GEOMETRY", static_cast<u32>(mesh->clusters.size()));
            const std::string clusters = m_cluster_job.valid() ? std::string("building...")
                : (mesh->has_clusters() ? format_count(mesh->clusters.size()) + " clusters" : std::string("not built"));
            if (outliner_row("##vg_auto", "Clusters", m_preview_lod < 0, row_options{ .right_text = clusters.c_str(), .dimmed = m_preview_lod >= 0 }))
                set_preview_lod(-1);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("The preview draws the clusters its camera selects (zoom with the mouse wheel)");

            outliner_section("FALLBACK LEVELS", mesh->lod_count());
        } else {

            outliner_section("LEVELS OF DETAIL", mesh->lod_count());
            if (outliner_row("##lod_auto", "Automatic", m_preview_lod < 0, row_options{ .right_text = "by screen size", .dimmed = m_preview_lod >= 0 }))
                set_preview_lod(-1);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("The preview picks the level from its size on screen (zoom with the mouse wheel)");
        }
        {
            for (u32 level = 0; level < mesh->lod_count(); ++level) {

                ImGui::PushID(static_cast<int>(level) + 0x1000);
                const std::string label = std::format("LOD {}", level);
                const std::string tris = format_count(mesh->lod_triangle_count(level)) + " tris";
                const bool selected = (m_preview_lod == static_cast<i32>(level));
                if (outliner_row("##lod", label.c_str(), selected, row_options{ .right_text = tris.c_str(), .dimmed = virtual_mode && !selected }))
                    set_preview_lod(selected ? -1 : static_cast<i32>(level));
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("%s", virtual_mode
                        ? "Lock the preview to this fallback level (click again to see the clusters)"
                        : "Lock the preview to this level (click again for automatic)");
                ImGui::PopID();
            }
        }

        // ---- colliders ----
        m_hovered_collider = -1;
        if (outliner_section("COLLIDERS", static_cast<u32>(m_colliders.size()), "Add a collider"))
            ImGui::OpenPopup("add_collider_popup");
        draw_add_collider_popup();

        if (m_colliders.empty()) {
            ImGui::PushTextWrapPos(0.f);
            ImGui::TextDisabled("No colliders yet. Use + to add a box, sphere, capsule or a shape built from the mesh.");
            ImGui::PopTextWrapPos();
        } else {
            for (i32 i = 0; i < static_cast<i32>(m_colliders.size()); ++i)
                draw_collider_row(i);
        }

        m_hovered_submesh = new_hovered_sub;

        // Deferred collider delete — safe after iteration
        if (m_pending_delete_collider >= 0) {
            remove_collider(m_pending_delete_collider);
            m_pending_delete_collider = -1;
        }
    }


    void mesh_editor_window::draw_collider_row(i32 index) {

        auto& item = m_colliders[static_cast<size_t>(index)];

        ImGui::PushID(static_cast<int>(item.id));

        const bool selected = (m_inspector_target == inspector_target::collider && m_selected_collider == index);
        const f32 eye = ImGui::GetFrameHeight();
        const ImVec2 row_min = ImGui::GetCursorScreenPos();

        bool hovered = false;
        row_options opts{};
        opts.right_text = collider_shape_kind_name(item.kind);
        opts.dot_color = ImGui::GetColorU32(ImVec4(item.color.x, item.color.y, item.color.z, 1.f));
        opts.dimmed = !item.visible;
        opts.right_reserved = eye;
        if (outliner_row("##collider", item.name.c_str(), selected, opts, &hovered)) {
            if (selected) select_mesh();
            else          select_collider(index);
        }
        if (hovered)
            m_hovered_collider = index;

        if (ImGui::BeginPopupContextItem("##collider_ctx")) {

            if (ImGui::MenuItem(item.visible ? "Hide" : "Show"))
                item.visible = !item.visible;

            ImGui::Separator();
            if (ImGui::MenuItem("Delete"))
                m_pending_delete_collider = index;

            ImGui::EndPopup();
        }

        // visibility toggle on the right edge of the row
        const ImVec2 next = ImGui::GetCursorScreenPos();
        const f32 width = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorScreenPos(ImVec2(row_min.x + width - eye, row_min.y));
        if (ImGui::InvisibleButton("##visible", ImVec2(eye, eye)))
            item.visible = !item.visible;
        {
            const bool eye_hovered = ImGui::IsItemHovered();
            const auto icon = icon_manager::get(item.visible ? icon_manager::icon::show : icon_manager::icon::hide);
            const f32 inset = eye * 0.22f;
            ImGui::GetWindowDrawList()->AddImage(icon.tex_ref, ImVec2(row_min.x + width - eye + inset, row_min.y + inset),
                ImVec2(row_min.x + width - inset, row_min.y + eye - inset), icon.uv0, icon.uv1,
                ImGui::GetColorU32(eye_hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled));
            if (eye_hovered)
                ImGui::SetTooltip(item.visible ? "Hide in the preview" : "Show in the preview");
        }
        ImGui::SetCursorScreenPos(next);
        ImGui::Dummy(ImVec2(0.f, 0.f));

        ImGui::PopID();
    }


    void mesh_editor_window::draw_add_collider_popup() {

        if (!ImGui::BeginPopup("add_collider_popup"))
            return;

        ImGui::TextDisabled("Primitive");
        if (ImGui::MenuItem("Box"))         add_collider(collider_shape_kind::box);
        if (ImGui::MenuItem("Sphere"))      add_collider(collider_shape_kind::sphere);
        if (ImGui::MenuItem("Capsule"))     add_collider(collider_shape_kind::capsule);
        if (ImGui::MenuItem("Cylinder"))    add_collider(collider_shape_kind::cylinder);

        ImGui::Separator();
        ImGui::TextDisabled("From Mesh");

        if (ImGui::MenuItem("Convex Hull"))     add_collider(collider_shape_kind::convex_hull);
        if (ImGui::MenuItem("Triangle Mesh"))   add_collider(collider_shape_kind::triangle_mesh);

        ImGui::EndPopup();
    }


    // inspector -------------------------------------------------------------------------------------------------------

    void mesh_editor_window::draw_inspector_panel() {

        if (m_load_failed) {
            UI::empty_state("Failed to load the mesh", "See the log for details");
            return;
        }

        if (!m_has_mesh) {
            UI::empty_state("No mesh loaded", "Drop a mesh asset from the Content Browser into this window");
            return;
        }

        UI::begin_properties(UI::property_layout::compact);
        switch (m_inspector_target) {
            case inspector_target::mesh:        draw_inspector_mesh();       break;
            case inspector_target::submesh:     draw_inspector_submesh();    break;
            case inspector_target::collider:    draw_inspector_collider();   break;
        }
        UI::end_properties();
    }


    void mesh_editor_window::draw_inspector_mesh() {

        auto registry = GLT::asset::registry::get_ref();
        auto* mesh = registry ? registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle) : nullptr;
        using GLT::asset::mesh::detail_mode;
        const std::string subtitle = !mesh ? std::string("Mesh")
            : (mesh->detail() == detail_mode::virtual_geometry
                ? std::format("Mesh · {} triangles · Virtual geometry", format_count(mesh->indices.size() / 3))
                : std::format("Mesh · {} triangles · {} LODs", format_count(mesh->indices.size() / 3), mesh->lod_count()));
        inspector_header(icon_manager::icon::mesh_asset, m_mesh_name.c_str(), subtitle);

        draw_mesh_summary_section();

        // One detail system per mesh: the switch, then only the cards of the mode it is in (the switch may change it)
        draw_detail_mode_section();
        if (mesh && mesh->detail() == detail_mode::virtual_geometry) {
            draw_virtual_geometry_section();
            draw_virtual_geometry_debug_section();
        } else {
            draw_lod_section();
        }
        draw_mesh_bounds_section();
        draw_preview_settings_section();
        draw_lighting_section();
        draw_camera_section();
    }


    void mesh_editor_window::draw_inspector_submesh() {

        auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return;

        auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle);
        if (!mesh)
            return;

        if (m_selected_submesh < 0 || m_selected_submesh >= static_cast<i32>(mesh->submesh_count())) {
            select_mesh();
            return;
        }

        auto& sm = mesh->submeshes[static_cast<size_t>(m_selected_submesh)];
        const std::string title = std::format("Submesh {}", m_selected_submesh);
        inspector_header(icon_manager::icon::material, title.c_str(),
            std::format("Submesh · {} triangles", format_count(sm.index_count / 3)));

        if (UI::begin_card("Material", UI::card_options{ .description = "The material this part of the mesh is drawn with" })) {

            const auto old_handle = sm.material;
            const bool changed = UI::property_custom("Material", [&](f32 width) {
                return GLT::UI::asset_picker_widget({
                        .label = "##submesh_mat",
                        .filter = GLT::asset::core_types::material,
                        .allow_clear = true,
                        .placeholder = "(none)"
                    }, sm.material, width);
            });

            if (changed) {
                if (old_handle != INVALID_HANDLE && old_handle != sm.material)
                    registry->release(old_handle);
                if (sm.material != INVALID_HANDLE)
                    registry->add_dependency(m_asset_handle, sm.material);
                m_dirty = true;
            }

            if (m_dirty)
                warning_text("Material changes are preview-only for now.");
        }
        UI::end_card();

        if (UI::begin_card("Geometry")) {

            UI::property_value("Triangles", format_count(sm.index_count / 3));
            UI::property_value("Index range", std::format("{} .. {}", sm.first_index, sm.first_index + sm.index_count));

            if (m_selected_submesh < static_cast<i32>(m_submesh_bounds_cache.size())) {
                const auto& b = m_submesh_bounds_cache[static_cast<size_t>(m_selected_submesh)];
                const glm::vec3 sz = b.max - b.min;
                UI::property_value("Size", std::format("{:.3f} x {:.3f} x {:.3f}", sz.x, sz.y, sz.z), "Bounding box, X x Y x Z");
            }
        }
        UI::end_card();
    }


    void mesh_editor_window::draw_inspector_collider() {

        if (m_selected_collider < 0 || m_selected_collider >= static_cast<i32>(m_colliders.size())) {
            select_mesh();
            return;
        }

        auto& item = m_colliders[static_cast<size_t>(m_selected_collider)];
        static const collider_editor_item defaults{};
        bool changed = false;

        inspector_header(icon_manager::icon::mesh_mini, item.name.c_str(),
            std::format("{} collider", collider_shape_kind_name(item.kind)));

        // ---- general ----
        if (UI::begin_card("General")) {
            changed |= UI::property_text("Name", item.name, nullptr, "Collider name");
            UI::property_toggle("Show in preview", item.visible);
            UI::property_color("Preview color", item.color, true, "Color of the wireframe in the preview", &defaults.color);
        }
        UI::end_card();

        // ---- transform ----
        if (UI::begin_card("Transform", UI::card_options{ .description = "Relative to the mesh origin. The gizmo in the preview edits the same values" })) {

            static const glm::vec3 zero{ 0.f };
            changed |= UI::property_drag("Position", item.position, 0.01f, 0.f, 0.f, "%.3f", nullptr, &zero);

            glm::vec3 euler = glm::degrees(glm::eulerAngles(item.rotation));
            const bool is_identity = std::abs(item.rotation.w) > 0.99999f;
            if (UI::property_custom("Rotation", [&](f32 width) {
                    return UI::drag_axes("##rotation", &euler.x, 3, 0.5f, 0.f, 0.f, "%.1f", width);
                }, "Euler angles in degrees", is_identity, [&]() { euler = glm::vec3(0.f); })) {
                item.rotation = glm::quat(glm::radians(euler));
                changed = true;
            }
        }
        UI::end_card();

        // ---- shape ----
        if (UI::begin_card("Shape", UI::card_options{ .summary = collider_shape_kind_name(item.kind) })) {

            switch (item.kind) {
                case collider_shape_kind::box:
                    changed |= UI::property_drag("Half extents", item.box_half_extents, 0.01f, 0.001f, 10000.f, "%.3f",
                        "Half of the box size along each axis", &defaults.box_half_extents);
                    break;
                case collider_shape_kind::sphere:
                    changed |= UI::property_drag("Radius", item.sphere_radius, 0.01f, 0.001f, 10000.f, "%.3f", nullptr, &defaults.sphere_radius);
                    break;
                case collider_shape_kind::capsule:
                    changed |= UI::property_drag("Radius", item.capsule_radius, 0.01f, 0.001f, 10000.f, "%.3f", nullptr, &defaults.capsule_radius);
                    changed |= UI::property_drag("Half height", item.capsule_half_height, 0.01f, 0.f, 10000.f, "%.3f",
                        "Half the length of the straight part, without the caps", &defaults.capsule_half_height);
                    break;
                case collider_shape_kind::cylinder:
                    changed |= UI::property_drag("Radius", item.cylinder_radius, 0.01f, 0.001f, 10000.f, "%.3f", nullptr, &defaults.cylinder_radius);
                    changed |= UI::property_drag("Half height", item.cylinder_half_height, 0.01f, 0.001f, 10000.f, "%.3f", nullptr, &defaults.cylinder_half_height);
                    break;
                case collider_shape_kind::convex_hull:
                    ImGui::TextDisabled("Built from the mesh vertices.");
                    warning_text("Not supported by the runtime format yet - skipped on save.");
                    break;
                case collider_shape_kind::triangle_mesh:
                    ImGui::TextDisabled("Uses the mesh triangles directly. Best for static geometry.");
                    break;
            }
        }
        UI::end_card();

        // ---- physics material ----
        if (UI::begin_card("Physics Material")) {
            changed |= UI::property_slider("Friction", item.material.friction, 0.f, 1.f, "%.2f",
                "0 = ice, 1 = rubber", &defaults.material.friction);
            changed |= UI::property_slider("Restitution", item.material.restitution, 0.f, 1.f, "%.2f",
                "Bounciness: 0 = no bounce, 1 = perfect bounce", &defaults.material.restitution);
            changed |= UI::property_drag("Density", item.material.density, 10.f, 0.f, 30000.f, "%.0f kg/m3",
                "Mass per volume (water = 1000)", &defaults.material.density);
        }
        UI::end_card();

        if (changed)
            m_dirty = true;

        ImGui::Dummy(ImVec2(0.f, 4.f));
        if (danger_button("Delete Collider"))
            m_pending_delete_collider = m_selected_collider;
    }


    // mesh-only inspector sections -------------------------------------------------------------------------------------

    void mesh_editor_window::draw_mesh_summary_section() {

        auto registry = GLT::asset::registry::get_ref();
        auto* mesh = registry ? registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle) : nullptr;
        if (!mesh)
            return;

        if (UI::begin_card("Summary", UI::card_options{ .collapsible = true })) {
            UI::property_value("Triangles", format_count(mesh->indices.size() / 3));
            UI::property_value("Vertices", format_count(mesh->vertices.size()));
            UI::property_value("Submeshes", std::to_string(mesh->submesh_count()));
            if (mesh->detail() == GLT::asset::mesh::detail_mode::virtual_geometry) {
                UI::property_value("Detail", mesh->has_clusters()
                    ? "Virtual geometry, " + format_count(mesh->clusters.size()) + " clusters"
                    : std::string(m_cluster_job.valid() ? "Virtual geometry (building...)" : "Virtual geometry (not built)"),
                    "This mesh is drawn as clusters picked per frame. See 'Geometry Detail'");
                UI::property_value("Fallback levels", std::to_string(mesh->lod_count()), "Discrete levels used where clusters are not (shadows, translucent materials)");
            } else {
                UI::property_value("Detail", std::format("Discrete LODs, {} levels", mesh->lod_count()),
                    "This mesh switches between whole-mesh levels. See 'Geometry Detail'");
            }
            UI::property_value("Memory", GLT::util::format_bytes(mesh->memory_usage()), "CPU side, all levels and clusters");
        }
        UI::end_card();
    }


    void mesh_editor_window::draw_mesh_bounds_section() {

        auto registry = GLT::asset::registry::get_ref();
        auto* mesh = registry ? registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle) : nullptr;
        if (!mesh)
            return;

        glm::vec3 mn, mx;
        compute_mesh_bounds(*mesh, mn, mx);
        const glm::vec3 size = mx - mn;

        const std::string summary = std::format("{:.2f} x {:.2f} x {:.2f}", size.x, size.y, size.z);
        if (UI::begin_card("Bounds", UI::card_options{ .summary = summary.c_str(), .collapsible = true, .default_open = false })) {
            UI::property_value("Size", std::format("{:.3f} x {:.3f} x {:.3f}", size.x, size.y, size.z));
            UI::property_value("Min", std::format("{:.3f}, {:.3f}, {:.3f}", mn.x, mn.y, mn.z));
            UI::property_value("Max", std::format("{:.3f}, {:.3f}, {:.3f}", mx.x, mx.y, mx.z));
        }
        UI::end_card();
    }


    void mesh_editor_window::draw_preview_settings_section() {

        if (UI::begin_card("Preview", UI::card_options{ .description = "Only affects this window, not the asset", .collapsible = true, .default_open = false })) {

            static const glm::vec4 default_background{ 0.09f, 0.09f, 0.09f, 1.0f };
            static const bool off = false;
            UI::property_color("Background", m_background_color, false, nullptr, &default_background);
            UI::property_toggle("Auto rotate", m_auto_rotate, "Slowly orbit around the mesh", &off);
            if (m_auto_rotate)
                UI::property_slider("Rotate speed", m_auto_rotate_speed, 0.01f, 100.f, "%.2f %%", nullptr, nullptr, ImGuiSliderFlags_Logarithmic);
        }
        UI::end_card();
    }


    void mesh_editor_window::draw_camera_section() {

        if (UI::begin_card("Camera", UI::card_options{ .description = "LMB orbit, MMB pan, wheel zoom", .collapsible = true, .default_open = false })) {

            static const preview_camera_state defaults{};
            UI::property_slider("Distance", m_preview_camera.distance, MIN_CAM_DISTANCE, MAX_CAM_DISTANCE, "%.2f", nullptr, nullptr,
                ImGuiSliderFlags_Logarithmic);
            UI::property_slider("Field of view", m_preview_camera.fov_deg, 15.0f, 110.0f, "%.0f deg", nullptr, &defaults.fov_deg);
            UI::property_drag("Target", m_preview_camera.target, 0.01f, 0.f, 0.f, "%.2f", "The point the camera orbits around");

            if (UI::subtle_button("Fit mesh in view", ImVec2(-FLT_MIN, 0.f)))
                reset_camera_to_fit();
        }
        UI::end_card();
    }


    void mesh_editor_window::draw_lighting_section() {

        if (UI::begin_card("Lighting", UI::card_options{ .description = "Preview lights, not saved with the asset", .collapsible = true, .default_open = false })) {

            static const preview_lighting_state defaults{};
            UI::property_slider("Key light", m_preview_lighting.key_intensity, 0.0f, 1.0f, "%.2f", "Main light", &defaults.key_intensity);
            UI::property_slider("Fill light", m_preview_lighting.fill_intensity, 0.0f, 1.0f, "%.2f", "Softens the shadow side", &defaults.fill_intensity);
            UI::property_slider("Rim light", m_preview_lighting.rim_intensity, 0.0f, 1.0f, "%.2f", "Outlines the silhouette from behind", &defaults.rim_intensity);
            UI::property_toggle("Headlight", m_preview_lighting.headlight, "The key light follows the camera", &defaults.headlight);
        }
        UI::end_card();
    }

    // collider state helpers -------------------------------------------------------------------------------------------

    void mesh_editor_window::add_collider(collider_shape_kind kind) {

        collider_editor_item item{};
        item.id = m_next_collider_id++;
        item.kind = kind;
        item.name = std::string(collider_shape_kind_name(kind)) + " " + std::to_string(item.id);

        auto registry = GLT::asset::registry::get_ref();
        if (registry) {
            auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle);
            if (mesh) {

                glm::vec3 mn, mx;
                compute_mesh_bounds(*mesh, mn, mx);

                const glm::vec3 center = 0.5f * (mn + mx);
                const glm::vec3 extent = mx - mn;
                const f32 radius = 0.5f * glm::length(extent);

                item.position = center;

                item.box_half_extents = 0.5f * extent;

                item.sphere_radius = std::max(0.05f, radius);

                item.capsule_radius = std::max(0.05f, radius * 0.5f);
                item.capsule_half_height = std::max(0.0f, 0.5f * extent.y);

                item.cylinder_radius = std::max(0.05f, radius * 0.5f);
                item.cylinder_half_height = std::max(0.05f, 0.5f * extent.y);
            }
        }

        m_colliders.push_back(std::move(item));
        select_collider(static_cast<i32>(m_colliders.size() - 1));
        m_dirty = true;
    }


    void mesh_editor_window::remove_collider(i32 index) {

        if (index < 0 || index >= static_cast<i32>(m_colliders.size()))
            return;

        m_colliders.erase(m_colliders.begin() + index);
        m_dirty = true;

        if (m_selected_collider == index)
            select_mesh();
        else if (m_selected_collider > index)
            --m_selected_collider;

        if (m_hovered_collider == index)    m_hovered_collider = -1;
        else if (m_hovered_collider > index) --m_hovered_collider;
    }


    void mesh_editor_window::reset_collider_state() {

        m_colliders.clear();
        m_inspector_target = inspector_target::mesh;
        m_selected_submesh = -1;
        m_hovered_submesh = -1;
        m_selected_collider = -1;
        m_hovered_collider = -1;
        m_pending_delete_collider = -1;
        m_next_collider_id = 1;
    }


    void mesh_editor_window::select_mesh() {

        m_inspector_target = inspector_target::mesh;
        m_selected_submesh = -1;
        m_selected_collider = -1;
    }


    void mesh_editor_window::select_submesh(i32 index) {

        if (index < 0) {
            select_mesh();
            return;
        }

        m_inspector_target = inspector_target::submesh;
        m_selected_submesh = index;
        m_selected_collider = -1;
    }


    void mesh_editor_window::select_collider(i32 index) {

        if (index < 0) {
            select_mesh();
            return;
        }

        m_inspector_target = inspector_target::collider;
        m_selected_collider = index;
        m_selected_submesh = -1;
    }

    // preview ----------------------------------------------------------------------------------------------------------

    void mesh_editor_window::draw_preview_panel() {

        if (!m_has_mesh) {
            UI::empty_state("No mesh loaded", "Drop a mesh asset from the Content Browser here");
            return;
        }

        if (!m_preview_scene) {
            UI::empty_state("Preview unavailable", "The renderer could not create a preview target");
            return;
        }

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x < MIN_PREVIEW_SIDE || avail.y < MIN_PREVIEW_SIDE)
            return;

        const glm::uvec2 new_size{ static_cast<u32>(avail.x), static_cast<u32>(avail.y) };
        if (new_size != m_last_preview_size) {
            m_preview_scene->resize(new_size);
            m_last_preview_size = new_size;
        }

        const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
        const ImVec2 canvas_max(canvas_min.x + avail.x, canvas_min.y + avail.y);
        void* tex = m_preview_scene->get_output_image();

        // Important: allow overlay buttons to claim hover before the
        // invisible interaction area does. Without this, the InvisibleButton
        // below would set g.HoveredId and block subsequent items.
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##mesh_preview_canvas", avail,
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

        if (tex) {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddImage(reinterpret_cast<ImTextureID>(tex), canvas_min, canvas_max);
            m_preview_image_drawn = ImGui::IsItemVisible();
        } else {
            ImGui::GetWindowDrawList()->AddRectFilled(canvas_min, canvas_max, IM_COL32(24, 24, 28, 255));
        }
        render_collider_gizmo(canvas_min, avail);

        // camera input
        const bool hovered = ImGui::IsItemHovered();
        const bool active_left = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
        const bool active_middle  = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle);
        ImGuiIO& io = ImGui::GetIO();

        if (hovered && io.MouseWheel != 0.0f) {
            m_preview_camera.distance = GLT::math::clamp(
                m_preview_camera.distance * std::pow(0.9f, io.MouseWheel),
                MIN_CAM_DISTANCE, MAX_CAM_DISTANCE);
        }

        if (active_left) {
            m_preview_camera.yaw_rad   -= io.MouseDelta.x * 0.008f;
            m_preview_camera.pitch_rad  = GLT::math::clamp(
                m_preview_camera.pitch_rad - io.MouseDelta.y * 0.008f, -1.45f, 1.45f);
        }

        if (active_middle) {
            const glm::vec3 fwd(
                std::cos(m_preview_camera.pitch_rad) * std::sin(m_preview_camera.yaw_rad),
                std::sin(m_preview_camera.pitch_rad),
                std::cos(m_preview_camera.pitch_rad) * std::cos(m_preview_camera.yaw_rad));
            const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
            const glm::vec3 up    = glm::normalize(glm::cross(right, fwd));
            m_preview_camera.target += (-right * io.MouseDelta.x + up * io.MouseDelta.y)
                                     * (m_preview_camera.distance * 0.0025f);
        }

        // ---------- overlays ----------
        // After the camera input above: that block queries the canvas InvisibleButton as the "last item"
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.10f, 0.10f, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.16f, 0.16f, 0.80f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.22f, 0.22f, 0.90f));

        constexpr f32 INSET = 10.0f;

        // top-left: render mode + LOD
        ImGui::SetCursorScreenPos(ImVec2(canvas_min.x + INSET, canvas_min.y + INSET));
        if (const auto modes = m_preview_scene->supported_modes(); !modes.empty()) {

            const u32 current_id = m_preview_scene->get_render_mode();
            const char* current_label = "Render mode";
            for (const auto& mode : modes)
                if (mode.id == current_id)
                    current_label = mode.display_name;

            if (ImGui::Button(current_label))
                ImGui::OpenPopup("##preview_render_mode");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Render mode of the preview");

            const ImVec2 btn_min = ImGui::GetItemRectMin();
            const ImVec2 btn_max = ImGui::GetItemRectMax();
            ImGui::SetNextWindowPos(ImVec2(btn_min.x, btn_max.y + 4.0f), ImGuiCond_Appearing);
            if (ImGui::BeginPopup("##preview_render_mode")) {
                const char* last_category = nullptr;
                for (const auto& mode : modes) {
                    if (mode.has_category() && (last_category == nullptr || std::strcmp(last_category, mode.category) != 0)) {
                        last_category = mode.category;
                        ImGui::SeparatorText(mode.category);
                    }
                    if (ImGui::Selectable(mode.display_name, mode.id == current_id))
                        set_preview_render_mode(mode.id);
                    if (mode.has_tooltip() && ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", mode.tooltip);
                }
                ImGui::EndPopup();
            }
            ImGui::SameLine();
        }
        draw_preview_lod_overlay(canvas_min, canvas_max);

        // top-right: what to show + collider gizmo
        {
            struct toggle { const char* label; bool* value; const char* tooltip; };
            const toggle toggles[] = {
                { "Grid",      &m_show_grid,                 "Ground grid" },
                { "Origin",    &m_show_origin,               "Axes at the mesh origin" },
                { "Colliders", &m_show_colliders_in_preview, "Collider wireframes" },
                { "Wireframe", &m_show_wireframe,            "Triangle edges of what is shown: the level, or the clusters the camera selects" },
            };

            const f32 pad = ImGui::GetStyle().FramePadding.x * 2.0f;
            f32 total_w = ImGui::GetStyle().ItemSpacing.x * static_cast<f32>(std::size(toggles) - 1);
            for (const auto& t : toggles)
                total_w += ImGui::CalcTextSize(t.label).x + pad;

            ImGui::SetCursorScreenPos(ImVec2(canvas_max.x - total_w - INSET, canvas_min.y + INSET));
            bool first = true;
            for (const auto& t : toggles) {
                if (!first)
                    ImGui::SameLine();
                first = false;
                if (overlay_toggle(t.label, *t.value))
                    *t.value = !*t.value;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("%s", t.tooltip);
            }

            ImGui::SetCursorScreenPos(ImVec2(canvas_max.x - INSET, canvas_min.y + INSET + ImGui::GetFrameHeight() + 6.0f));
            draw_collider_gizmo_toolbar();
        }

        // bottom-right: camera presets
        {
            const char* labels[] = { "Fit", "Front", "Top", "Side" };
            const f32 pad = ImGui::GetStyle().FramePadding.x * 2.0f;
            f32 total_w = ImGui::GetStyle().ItemSpacing.x * 3.0f;
            for (const char* l : labels)
                total_w += ImGui::CalcTextSize(l).x + pad;

            ImGui::SetCursorScreenPos(ImVec2(canvas_max.x - total_w - INSET, canvas_max.y - ImGui::GetFrameHeight() - INSET));
            if (ImGui::Button("Fit"))   reset_camera_to_fit();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Frame the whole mesh");
            ImGui::SameLine();
            if (ImGui::Button("Front")) { m_preview_camera.yaw_rad = 0.0f; m_preview_camera.pitch_rad = 0.0f; }
            ImGui::SameLine();
            if (ImGui::Button("Top"))   { m_preview_camera.yaw_rad = 0.0f; m_preview_camera.pitch_rad = 1.4f; }
            ImGui::SameLine();
            if (ImGui::Button("Side"))  { m_preview_camera.yaw_rad = glm::half_pi<f32>(); m_preview_camera.pitch_rad = 0.0f; }
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(4);

        // bottom-left: what is on screen + controls hint
        {
            auto registry = GLT::asset::registry::get_ref();
            auto* mesh = registry ? registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle) : nullptr;
            const f32 line = ImGui::GetTextLineHeightWithSpacing();
            f32 y = canvas_max.y - INSET - line;
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);

            draw_list->AddText(ImVec2(canvas_min.x + INSET, y), dim, "LMB orbit  ·  MMB pan  ·  Wheel zoom");

            if (mesh && mesh->lod_count() > 0) {
                const u32 lod_count = mesh->lod_count();
                const f32 screen_size = preview_screen_size(*mesh);
                const u32 shown = m_preview_lod >= 0 ? std::min<u32>(static_cast<u32>(m_preview_lod), lod_count - 1) : auto_lod_for(*mesh, screen_size);
                const u32 tris = mesh->lod_triangle_count(shown);
                const u32 base = std::max(mesh->lod_triangle_count(0), 1u);

                std::string stats = screen_size >= 1e8f
                    ? std::format("LOD {}  ·  {} tris ({:.0f}%)  ·  camera inside", shown, format_count(tris), 100.0 * tris / base)
                    : std::format("LOD {}  ·  {} tris ({:.0f}%)  ·  screen {:.3f}", shown, format_count(tris), 100.0 * tris / base, screen_size);

                // Virtual geometry draws clusters instead of a level (unless the preview is locked to a fallback level)
                if (preview_uses_virtual_geometry(*mesh)) {
                    const GLT::asset::mesh::cluster_cut cut = preview_cluster_cut(*mesh);
                    stats = std::format("Virtual geometry  ·  {} clusters  ·  {} tris ({:.0f}%)  ·  levels 0-{}", format_count(cut.clusters),
                        format_count(cut.triangles), 100.0 * cut.triangles / base, cut.deepest_level);
                } else if (mesh->detail() == GLT::asset::mesh::detail_mode::virtual_geometry) {
                    stats = m_preview_lod >= 0
                        ? std::format("Fallback LOD {}  ·  {} tris ({:.0f}%)", shown, format_count(tris), 100.0 * tris / base)
                        : std::format("Virtual geometry not built yet  ·  fallback LOD {}  ·  {} tris", shown, format_count(tris));
                }
                y -= line;
                draw_list->AddText(ImVec2(canvas_min.x + INSET, y), ImGui::GetColorU32(ImGuiCol_Text), stats.c_str());

                if (m_show_wireframe && m_wire_truncated) {
                    y -= line;
                    const std::string note = std::format("Wireframe: too many edges, showing the first {}", format_count(MAX_WIREFRAME_EDGES));
                    draw_list->AddText(ImVec2(canvas_min.x + INSET, y), IM_COL32(230, 200, 110, 255), note.c_str());
                }
                if (m_show_cluster_bounds && m_cluster_bounds_truncated) {
                    y -= line;
                    const std::string note = std::format("Cluster bounds: showing the nearest {}", format_count(MAX_CLUSTER_BOUNDS));
                    draw_list->AddText(ImVec2(canvas_min.x + INSET, y), IM_COL32(230, 200, 110, 255), note.c_str());
                }
            }
        }
    }


    // preview plumbing -------------------------------------------------------------------------------------------------

    void mesh_editor_window::ensure_preview() {

        if (m_preview_scene || !m_has_mesh)
            return;

        GLT::render::offscreen_target_desc offscreen{};
        offscreen.extent = glm::uvec2(DEFAULT_PREVIEW_SIZE, DEFAULT_PREVIEW_SIZE);

        GLT::render::scene_renderer_desc desc{
            .target = offscreen,
            .clear_color = m_background_color,
        };

        m_preview_scene = GLT::application::get().create_scene_renderer(desc);
        if (!m_preview_scene)
            return;

        m_last_preview_size = offscreen.extent;

        submit_preview_instance();
        submit_preview_lights();

        if (!m_preview_scene->supported_modes().empty())
            set_preview_render_mode(default_preview_render_mode());
    }


    // The renderer's default mode ("Lit"), or its first one
    u32 mesh_editor_window::default_preview_render_mode() const {

        if (!m_preview_scene)
            return 0;
        const auto modes = m_preview_scene->supported_modes();
        for (const auto& mode : modes)
            if (mode.is_default)
                return mode.id;
        return modes.empty() ? 0u : modes.front().id;
    }


    void mesh_editor_window::set_preview_render_mode(const u32 mode_id) {

        m_current_mode_id = mode_id;
        if (m_preview_scene)
            m_preview_scene->set_render_mode(mode_id);
    }


    void mesh_editor_window::destroy_preview() {

        if (!m_preview_scene)
            return;

        GLT::application::get().destroy_scene_renderer(m_preview_scene);
        m_preview_scene = nullptr;
        m_last_preview_size = { 0, 0 };
    }


    GLT::world::camera_snapshot mesh_editor_window::build_camera_snapshot() const {

        const glm::vec3 forward{
            std::cos(m_preview_camera.pitch_rad) * std::sin(m_preview_camera.yaw_rad),
            std::sin(m_preview_camera.pitch_rad),
            std::cos(m_preview_camera.pitch_rad) * std::cos(m_preview_camera.yaw_rad)
        };

        const glm::vec3 position = m_preview_camera.target - forward * m_preview_camera.distance;

        GLT::world::camera_snapshot cam{};
        cam.position   = position;
        cam.view       = glm::lookAt(position, m_preview_camera.target, glm::vec3(0, 1, 0));
        cam.fov        = m_preview_camera.fov_deg;
        cam.near_plane = std::max(0.01f, m_preview_camera.distance * 0.01f);
        cam.far_plane  = m_preview_camera.distance * 10.0f + 1000.0f;
        return cam;
    }


    void mesh_editor_window::reset_camera_to_fit() {

        auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return;

        auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle);
        if (!mesh)
            return;

        glm::vec3 mn, mx;
        compute_mesh_bounds(*mesh, mn, mx);

        m_preview_camera.target = 0.5f * (mn + mx);

        const glm::vec3 extent = mx - mn;
        const f32 radius = 0.5f * glm::length(extent);
        m_preview_camera.distance  = GLT::math::clamp(radius * 2.5f, MIN_CAM_DISTANCE, MAX_CAM_DISTANCE);
        m_preview_camera.yaw_rad   = -0.42f;
        m_preview_camera.pitch_rad = -0.56f;
        m_preview_camera.fov_deg   = 45.0f;
    }

    // helpers ---------------------------------------------------------------------------------------------------------

    void mesh_editor_window::compute_mesh_bounds(const GLT::asset::mesh::mesh_asset& mesh,
        glm::vec3& out_min, glm::vec3& out_max) const {

        out_min = glm::vec3( std::numeric_limits<f32>::max());
        out_max = glm::vec3(-std::numeric_limits<f32>::max());

        if (mesh.vertices.empty())
            return;

        for (const auto& v : mesh.vertices) {
            out_min = glm::min(out_min, v.position);
            out_max = glm::max(out_max, v.position);
        }
    }


    void mesh_editor_window::rebuild_submesh_bounds_cache(const GLT::asset::mesh::mesh_asset& mesh) {

        m_submesh_bounds_cache.clear();
        m_submesh_bounds_cache.resize(mesh.submeshes.size());

        for (std::size_t i = 0; i < mesh.submeshes.size(); ++i) {

            const auto& sm = mesh.submeshes[i];
            auto& b = m_submesh_bounds_cache[i];

            b.min = glm::vec3( std::numeric_limits<f32>::max());
            b.max = glm::vec3(-std::numeric_limits<f32>::max());

            bool any = false;
            for (u32 k = 0; k < sm.index_count; ++k) {

                const u32 idx = mesh.indices[sm.first_index + k];
                if (idx >= mesh.vertices.size())
                    continue;

                b.min = glm::min(b.min, mesh.vertices[idx].position);
                b.max = glm::max(b.max, mesh.vertices[idx].position);
                any = true;
            }

            if (!any) {
                b.min = glm::vec3(0.0f);
                b.max = glm::vec3(0.0f);
            }
        }
    }


    bool mesh_editor_window::load_from_handle(GLT::asset::handle h) {

        auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return false;

        auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(h);
        if (!mesh)
            return false;

        m_asset_handle = h;
        m_has_mesh = true;
        m_load_failed = false;
        m_dirty = false;

        m_hovered_submesh = -1;
        m_selected_submesh = -1;

        m_preview_lod = -1;
        m_lod_needs_regen = false;
        m_clusters_need_rebuild = false;
        m_last_cluster_build_ms = 0.0;
        m_wire_lines.clear();
        m_wire_lod = -2;

        rebuild_submesh_bounds_cache(*mesh);
        load_colliders_from_mesh(*mesh);

        if (m_inspector_target == inspector_target::submesh &&
            (m_selected_submesh < 0 || m_selected_submesh >= static_cast<i32>(mesh->submesh_count()))) {
            select_mesh();
        }

        return true;
    }


    void mesh_editor_window::handle_drag_drop() {

        const ImVec2 min = ImGui::GetWindowPos();
        const ImVec2 max = ImVec2(min.x + ImGui::GetWindowSize().x, min.y + ImGui::GetWindowSize().y);

        if (!ImGui::BeginDragDropTargetCustom(ImRect(min, max), ImGui::GetID("##mesh_drop_target")))
            return;

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DRAG_PAYLOAD_ID)) {

            const char* raw = static_cast<const char*>(payload->Data);
            if (raw && raw[0] != '\0') {

                const std::filesystem::path dropped(raw);
                const std::string ext = GLT::util::to_lower(dropped.extension().string());
                if (ext == ".glt_mesh" || ext == ".gltf" || ext == ".glb" || ext == ".obj")
                    open(dropped);
            }
        }

        ImGui::EndDragDropTarget();
    }


    void mesh_editor_window::submit_debug_draws() {

        if (!m_preview_scene || !m_has_mesh)
            return;

        auto* debug = m_preview_scene->get_debug_draw();
        if (!debug)
            return;

        auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return;

        auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle);
        if (!mesh)
            return;

        // ---- grid ----
        if (m_show_grid) {
            constexpr i32 GRID_HALF = 10;
            constexpr f32 GRID_STEP = 1.0f;
            constexpr f32 GRID_EXT  = GRID_HALF * GRID_STEP;
            const glm::vec4 minor{ 0.30f, 0.30f, 0.34f, 0.55f };
            const glm::vec4 major{ 0.45f, 0.45f, 0.50f, 0.85f };
            const glm::vec4 axis_x{ 0.75f, 0.20f, 0.20f, 1.0f };
            const glm::vec4 axis_z{ 0.20f, 0.20f, 0.75f, 1.0f };
            for (i32 i = -GRID_HALF; i <= GRID_HALF; ++i) {
                if (i == 0) continue;
                const f32 p = i * GRID_STEP;
                const glm::vec4& col = (i % 5 == 0) ? major : minor;
                debug->line({ -GRID_EXT, 0, p }, {  GRID_EXT, 0, p }, col, 0, true);
                debug->line({ p, 0, -GRID_EXT }, {  p, 0,  GRID_EXT }, col, 0, true);
            }
            debug->line({ -GRID_EXT, 0, 0 }, {  GRID_EXT, 0, 0 }, axis_x, 0, true);
            debug->line({ 0, 0, -GRID_EXT }, {  0, 0,  GRID_EXT }, axis_z, 0, true);
        }

        // ---- origin ----
        if (m_show_origin) {
            constexpr f32 L = 1.0f;
            debug->line({ 0, 0, 0 }, { L, 0, 0 }, { 1.0f, 0.2f, 0.2f, 1.0f }, 0, true);
            debug->line({ 0, 0, 0 }, { 0, L, 0 }, { 0.2f, 1.0f, 0.2f, 1.0f }, 0, true);
            debug->line({ 0, 0, 0 }, { 0, 0, L }, { 0.2f, 0.2f, 1.0f, 1.0f }, 0, true);
        }

        // ---- mesh AABB ----
        glm::vec3 mn, mx;
        compute_mesh_bounds(*mesh, mn, mx);
        if (mn.x <= mx.x && mn.y <= mx.y && mn.z <= mx.z)
            debug->aabb(mn, mx, glm::vec4(0.70f, 0.75f, 0.85f, 0.55f), 0.0f, false);

        // ---- hovered / selected submesh ----
        auto draw_submesh_aabb = [&](i32 idx, const glm::vec4& col) {
            if (idx < 0 || static_cast<std::size_t>(idx) >= m_submesh_bounds_cache.size())
                return;
            const auto& b = m_submesh_bounds_cache[idx];
            if (b.min.x <= b.max.x && b.min.y <= b.max.y && b.min.z <= b.max.z)
                debug->aabb(b.min, b.max, col, 0.0f, false);
        };

        draw_submesh_aabb(m_hovered_submesh, { 1.0f, 0.85f, 0.20f, 1.0f });

        if (m_selected_submesh >= 0 && m_selected_submesh != m_hovered_submesh)
            draw_submesh_aabb(m_selected_submesh, { 1.0f, 0.45f, 0.15f, 1.0f });

        // ---- wireframe of the shown level / the selected clusters ----
        if (m_show_wireframe)
            submit_wireframe_debug_draws(*mesh);

        // ---- virtual geometry: bounds of the selected clusters ----
        m_cluster_bounds_truncated = false;
        if (m_show_cluster_bounds && preview_uses_virtual_geometry(*mesh))
            submit_cluster_bounds_debug_draws(*mesh);

        // ---- colliders ----
        submit_collider_debug_draws();
    }


    void mesh_editor_window::submit_collider_debug_draws() {

        if (!m_show_colliders_in_preview)
            return;

        auto* debug = m_preview_scene ? m_preview_scene->get_debug_draw() : nullptr;
        if (!debug)
            return;

        for (i32 i = 0; i < static_cast<i32>(m_colliders.size()); ++i) {

            auto& c = m_colliders[static_cast<size_t>(i)];
            if (!c.visible)
                continue;

            glm::vec4 color = c.color;
            if (i == m_hovered_collider)
                color = glm::mix(color, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 0.35f);
            if (i == m_selected_collider)
                color = glm::mix(color, glm::vec4(1.0f, 0.85f, 0.25f, 1.0f), 0.45f);

            switch (c.kind) {

                case collider_shape_kind::box:
                    draw_box_wireframe(*debug,
                        glm::translate(glm::mat4(1.0f), c.position) * glm::mat4_cast(c.rotation),
                        c.box_half_extents, color);
                    break;

                case collider_shape_kind::sphere:
                    debug->sphere(c.position, c.sphere_radius, color);
                    break;

                case collider_shape_kind::capsule:
                    draw_capsule_wireframe(*debug, c.position, c.rotation,
                        c.capsule_radius, c.capsule_half_height, color);
                    break;

                case collider_shape_kind::cylinder:
                    draw_cylinder_wireframe(*debug, c.position, c.rotation,
                        c.cylinder_radius, c.cylinder_half_height, color);
                    break;

                case collider_shape_kind::convex_hull:
                case collider_shape_kind::triangle_mesh:
                    // TODO: draw hull / triangle soup once geometry is bound to the item
                    debug->cross(c.position, 0.25f, color);
                    break;
            }
        }
    }


    void mesh_editor_window::submit_preview_lights() {

        if (!m_preview_scene)
            return;

        glm::vec3 key_dir = glm::normalize(glm::vec3(-0.5f, -0.75f, -0.5f));

        if (m_preview_lighting.headlight) {
            key_dir = glm::normalize(glm::vec3(
                std::cos(m_preview_camera.pitch_rad) * std::sin(m_preview_camera.yaw_rad),
                std::sin(m_preview_camera.pitch_rad),
                std::cos(m_preview_camera.pitch_rad) * std::cos(m_preview_camera.yaw_rad)));
        }

        std::array<GLT::world::light_snapshot, 3> lights{};

        lights[0].type = GLT::world::light_type::directional;
        lights[0].direction = key_dir;
        lights[0].color = glm::vec3(1.00f, 0.95f, 0.85f);
        lights[0].intensity = m_preview_lighting.key_intensity;

        lights[1].type = GLT::world::light_type::directional;
        lights[1].direction = glm::normalize(glm::vec3(0.6f, -0.3f, -0.6f));
        lights[1].color = glm::vec3(0.80f, 0.85f, 1.00f);
        lights[1].intensity = m_preview_lighting.fill_intensity;

        lights[2].type = GLT::world::light_type::directional;
        lights[2].direction = glm::normalize(glm::vec3(0.0f, -0.5f, 1.0f));
        lights[2].color = glm::vec3(1.0f);
        lights[2].intensity = m_preview_lighting.rim_intensity;

        m_preview_scene->submit_lights(lights);
    }


    // Collider gizmo row, right aligned at the cursor x (only while a collider is selected)
    void mesh_editor_window::draw_collider_gizmo_toolbar() {

        if (m_inspector_target != inspector_target::collider || m_selected_collider < 0)
            return;

        struct entry { const char* label; bool active; const char* tooltip; };
        const entry ops[] = {
            { "Move",   m_collider_gizmo_operation == ImGuizmo::TRANSLATE, "Move the collider" },
            { "Rotate", m_collider_gizmo_operation == ImGuizmo::ROTATE,    "Rotate the collider" },
            { "Scale",  m_collider_gizmo_operation == ImGuizmo::SCALE,     "Resize the collider shape" },
        };
        const entry spaces[] = {
            { "World", m_collider_gizmo_mode == ImGuizmo::WORLD, "Gizmo aligned to the mesh axes" },
            { "Local", m_collider_gizmo_mode == ImGuizmo::LOCAL, "Gizmo aligned to the collider" },
        };

        const f32 pad = ImGui::GetStyle().FramePadding.x * 2.0f;
        const f32 spacing = ImGui::GetStyle().ItemSpacing.x;
        f32 total_w = spacing * 3.0f + 10.0f;
        for (const auto& e : ops)    total_w += ImGui::CalcTextSize(e.label).x + pad;
        for (const auto& e : spaces) total_w += ImGui::CalcTextSize(e.label).x + pad;

        const ImVec2 anchor = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(anchor.x - total_w, anchor.y));

        for (int x = 0; x < 3; ++x) {
            if (x > 0)
                ImGui::SameLine();
            if (overlay_toggle(ops[x].label, ops[x].active))
                m_collider_gizmo_operation = x == 0 ? ImGuizmo::TRANSLATE : (x == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", ops[x].tooltip);
        }

        ImGui::SameLine(0.0f, spacing + 10.0f);
        for (int x = 0; x < 2; ++x) {
            if (x > 0)
                ImGui::SameLine();
            if (overlay_toggle(spaces[x].label, spaces[x].active))
                m_collider_gizmo_mode = x == 0 ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", spaces[x].tooltip);
        }
    }


    void mesh_editor_window::render_collider_gizmo(const ImVec2& image_min, const ImVec2& image_size) {

        if (m_inspector_target != inspector_target::collider)
            return;

        if (m_selected_collider < 0 || m_selected_collider >= static_cast<i32>(m_colliders.size()))
            return;

        if (!m_preview_scene)
            return;

        if (image_size.x <= 1.0f || image_size.y <= 1.0f)
            return;

        auto& item = m_colliders[static_cast<size_t>(m_selected_collider)];

        // Matrix carries translation + rotation only; scale lives in the shape params
        glm::mat4 model = glm::translate(glm::mat4(1.0f), item.position) * glm::mat4_cast(item.rotation);

        const GLT::world::camera_snapshot cam = build_camera_snapshot();
        const f32 aspect = image_size.x / image_size.y;
        const glm::mat4 projection = glm::perspective(glm::radians(cam.fov), aspect, cam.near_plane, cam.far_plane);

        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(image_min.x, image_min.y, image_size.x, image_size.y);
        ImGuizmo::Manipulate(glm::value_ptr(cam.view), glm::value_ptr(projection), m_collider_gizmo_operation,
            m_collider_gizmo_mode, glm::value_ptr(model));

        if (!ImGuizmo::IsUsing()) {
            m_gizmo_last_scale = glm::vec3(1.0f);   // reset for next drag
            return;
        }
        glm::vec3 translation{}, scale{};
        glm::quat rotation{};
        decompose_trs(model, translation, rotation, scale);

        item.position = translation;
        item.rotation = rotation;

        if (m_collider_gizmo_operation == ImGuizmo::SCALE) {

            const glm::vec3 delta = scale / m_gizmo_last_scale;
            if (glm::distance(delta, glm::vec3(1.0f)) > 1e-4f)
                apply_scale_to_collider(item, delta);

            m_gizmo_last_scale = scale;
            m_dirty = true;
        }
        m_dirty = true;
    }


    void mesh_editor_window::load_colliders_from_mesh(const GLT::asset::mesh::mesh_asset& mesh) {

        m_colliders.clear();
        m_next_collider_id = 1;

        for (const auto& mc : mesh.colliders) {

            collider_editor_item item{};
            if (!mesh_collider_to_editor(mc, item, m_next_collider_id)) {
                LOG(warn, "mesh_editor: skipping unrepresentable runtime collider (shape id {})", static_cast<int>(mc.shape));
                continue;
            }

            m_colliders.push_back(std::move(item));
            ++m_next_collider_id;
        }

        if (m_selected_collider >= static_cast<i32>(m_colliders.size())) {
            m_selected_collider = -1;
            if (m_inspector_target == inspector_target::collider)
                m_inspector_target = inspector_target::mesh;
        }
    }


    bool mesh_editor_window::sync_colliders_to_mesh() {

        auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return false;

        auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle);
        if (!mesh)
            return false;

        mesh->colliders.clear();
        mesh->colliders.reserve(m_colliders.size());

        for (const auto& item : m_colliders) {

            GLT::asset::mesh::mesh_collider mc{};
            if (editor_collider_to_mesh(item, mc))
                mesh->colliders.push_back(mc);
            else
                LOG(warn, "mesh_editor: collider [{}] ({}) will not be saved - unsupported by the runtime format",
                    item.name, collider_shape_kind_name(item.kind));
        }

        return true;
    }


    // level of detail --------------------------------------------------------------------------------------------------

    void mesh_editor_window::submit_preview_instance() {

        if (!m_preview_scene)
            return;

        GLT::asset::mesh::instance inst{};
        inst.mesh = m_asset_handle;
        inst.transform = glm::mat4(1.0f);
        if (m_preview_lod >= 0)
            inst.flags = (static_cast<u32>(m_preview_lod) + 1u) & GLT::asset::mesh::INSTANCE_FLAG_FORCE_LOD_MASK;
        m_preview_scene->submit_scene(std::span<const GLT::asset::mesh::instance>(&inst, 1));
    }


    void mesh_editor_window::set_preview_lod(i32 lod) {

        if (lod == m_preview_lod)
            return;
        m_preview_lod = lod;
        submit_preview_instance();
    }


    f32 mesh_editor_window::preview_screen_size(const GLT::asset::mesh::mesh_asset& mesh) const {

        // Same metric as the renderer: bounding-sphere diameter / viewport height = radius * |P11| / distance
        const GLT::world::camera_snapshot cam = build_camera_snapshot();
        const f32 radius = GLT::asset::mesh::lod_sphere_radius(mesh.bounds);
        const glm::vec3 center = 0.5f * (mesh.bounds.min + mesh.bounds.max);
        const f32 dist = glm::distance(center, cam.position);
        if (dist <= radius)
            return 1e9f;
        const f32 p11 = 1.0f / std::tan(glm::radians(cam.fov) * 0.5f);
        return radius * p11 / dist;
    }


    u32 mesh_editor_window::auto_lod_for(const GLT::asset::mesh::mesh_asset& mesh, f32 screen_size) const {

        u32 lod = 0;
        for (u32 level = 1; level < mesh.lod_count(); ++level)
            if (screen_size < mesh.lod.levels[level].screen_size)
                lod = level;
        return lod;
    }


    void mesh_editor_window::start_lod_generation() {

        if (m_lod_job.valid())
            return;

        auto registry = GLT::asset::registry::get_ref();
        auto* mesh = registry ? registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle) : nullptr;
        if (!mesh)
            return;

        GLT::asset::mesh::sanitize(mesh->lod);
        m_lod_job_settings = mesh->lod;

        // Copies: the job runs on a worker thread while the asset stays live (renderer reads it, the editor may close)
        m_lod_job = std::async(std::launch::async,
            [vertices = mesh->vertices, indices = mesh->indices, submeshes = mesh->submeshes, settings = m_lod_job_settings]() {
                return GLT::asset::mesh::build_lods(vertices, indices, submeshes, settings);
            });
    }


    void mesh_editor_window::poll_lod_generation() {

        if (!m_lod_job.valid() || m_lod_job.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;

        GLT::asset::mesh::lod_build_output result = m_lod_job.get();
        m_lod_job = {};

        auto registry = GLT::asset::registry::get_ref();
        auto* mesh = registry ? registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle) : nullptr;
        if (!mesh)
            return;

        // Settings were locked while the job ran; keep the user's live values (shadow bias, cull size) as they are
        mesh->lods = std::move(result.lods);
        mesh->lod_ranges = std::move(result.ranges);
        mesh->lod_indices = std::move(result.indices);
        mesh->lod_vertices = std::move(result.vertices);
        if (mesh->lod.flags & GLT::asset::mesh::LOD_FLAG_AUTO_SCREEN_SIZE)
            GLT::asset::mesh::fit_screen_sizes(mesh->lod, mesh->lods, mesh->bounds);
        mesh->revision++;                       // renderers re-upload (shared_resource_manager::refresh_meshes)

        m_last_lod_build_ms = result.build_ms;
        m_lod_needs_regen = false;
        m_dirty = true;
        m_wire_lod = -2;

        if (m_preview_lod >= static_cast<i32>(mesh->lod_count()))
            m_preview_lod = static_cast<i32>(mesh->lod_count()) - 1;
        submit_preview_instance();

        LOG(info, "mesh_editor: [{}] {} of {} LOD levels in {:.1f} ms", m_mesh_name, mesh->lod_count(), result.requested,
            result.build_ms);
    }


    void mesh_editor_window::draw_lod_section() {

        auto registry = GLT::asset::registry::get_ref();
        auto* mesh = registry ? registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle) : nullptr;
        if (!mesh)
            return;

        using namespace GLT::asset::mesh;
        lod_settings& lod = mesh->lod;
        const bool busy = m_lod_job.valid();
        bool live_changed = false;              // runtime-only values: re-upload, no regeneration
        bool regen_changed = false;             // simplifier inputs: regeneration needed
        const u32 generated = mesh->lod_count();

        // ======== overview: status, levels table, actions ===========================================================
        const std::string lod_summary = std::format("{} of {} generated", generated, lod.lod_count);
        if (UI::begin_card("Level of Detail", UI::card_options{ .summary = lod_summary.c_str(), .collapsible = true })) {

            // ---- status ----
            if (busy)
                ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "Generating %u levels...", m_lod_job_settings.lod_count);
            else if (generated < lod.lod_count)
                ImGui::TextWrapped("%u of %u levels generated. The others would not remove enough triangles within their error limit.",
                    generated, lod.lod_count);
            else {
                ImGui::Text("%u levels generated", generated);
                if (m_last_lod_build_ms > 0.0) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("in %.0f ms", m_last_lod_build_ms);
                }
            }
            ImGui::Dummy(ImVec2(0.f, 2.f));

            // ---- per-level table ----
            //
            // Screen size = bounding-sphere diameter / viewport height at which the level takes over (live, no regeneration)
            // Ratio / max error = simplifier targets (regeneration needed). Right half: what the last generation achieved
            ImGui::BeginDisabled(busy);
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.f, 3.f));
            const ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("##lod_levels", 6, table_flags)) {

                ImGui::TableSetupColumn("LOD",       ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("LOD").x + 6.f);
                ImGui::TableSetupColumn("Screen",    ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("Ratio",     ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("Error %",   ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("Tris",      ImGuiTableColumnFlags_WidthStretch, 1.1f);
                ImGui::TableSetupColumn("Got %",     ImGuiTableColumnFlags_WidthStretch, 0.8f);

                // dim header row with tooltips
                ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
                static constexpr const char* HEADER_TIPS[6] = {
                    "Level. Click a level to lock the preview to it",
                    "Switch to this level when the mesh is smaller than this fraction of the screen height (applies immediately)",
                    "Target share of LOD 0's triangles (needs regeneration)",
                    "Maximum allowed deviation, in % of the mesh size (needs regeneration)",
                    "Triangles the level actually has",
                    "Deviation the level actually has, in % of the mesh size",
                };
                for (int column = 0; column < 6; ++column) {
                    ImGui::TableSetColumnIndex(column);
                    ImGui::TextDisabled("%s", ImGui::TableGetColumnName(column));
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        ImGui::SetTooltip("%s", HEADER_TIPS[column]);
                }

                const bool auto_size = (lod.flags & LOD_FLAG_AUTO_SCREEN_SIZE) != 0;
                const u32 base_tris = mesh->lod_triangle_count(0);

                for (u32 level = 0; level < lod.lod_count; ++level) {

                    ImGui::PushID(static_cast<int>(level));
                    ImGui::TableNextRow();
                    lod_level_settings& ls = lod.levels[level];
                    const bool exists = level < generated;

                    // LOD (click: preview this level)
                    ImGui::TableNextColumn();
                    char label[8];
                    std::snprintf(label, sizeof(label), "%u", level);
                    if (ImGui::Selectable(label, m_preview_lod == static_cast<i32>(level), ImGuiSelectableFlags_None) && exists)
                        set_preview_lod(m_preview_lod == static_cast<i32>(level) ? -1 : static_cast<i32>(level));
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", exists ? "Preview this level (click again for automatic)" : "Not generated");

                    if (level == 0) {
                        ImGui::TableNextColumn(); ImGui::TextDisabled("full");
                        ImGui::TableNextColumn(); ImGui::TextDisabled("1.00");
                        ImGui::TableNextColumn(); ImGui::TextDisabled("0");
                    } else {
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        ImGui::BeginDisabled(auto_size);
                        ImGui::DragFloat("##screen", &ls.screen_size, 0.002f, 0.0001f, 1.0f, "%.3f");
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            sanitize(lod);
                            live_changed = true;
                        }
                        ImGui::EndDisabled();
                        if (auto_size && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                            ImGui::SetTooltip("Set automatically (see 'Switching'). Turn off 'Auto screen sizes' to edit");

                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        ImGui::DragFloat("##ratio", &ls.triangle_ratio, 0.005f, 0.001f, 1.0f, "%.3f");
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            sanitize(lod);
                            regen_changed = true;
                        }

                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        f32 error_percent = ls.max_error * 100.0f;
                        if (ImGui::DragFloat("##error", &error_percent, 0.01f, 0.0f, 100.0f, "%.2f"))
                            ls.max_error = error_percent / 100.0f;
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            sanitize(lod);
                            regen_changed = true;
                        }
                    }

                    ImGui::TableNextColumn();
                    if (exists) {
                        const u32 tris = mesh->lod_triangle_count(level);
                        ImGui::TextUnformatted(format_count(tris).c_str());
                        if (ImGui::IsItemHovered() && base_tris > 0)
                            ImGui::SetTooltip("%u triangles, %.1f%% of LOD 0", tris, 100.0 * tris / base_tris);
                    } else {
                        ImGui::TextDisabled("-");
                    }

                    ImGui::TableNextColumn();
                    if (exists && level > 0 && level < mesh->lods.size())
                        ImGui::Text("%.3f", mesh->lods[level].error * 100.0f);
                    else
                        ImGui::TextDisabled("-");

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::PopStyleVar();
            ImGui::EndDisabled();

            // ---- actions ----
            ImGui::Dummy(ImVec2(0.f, 4.f));
            if (m_lod_needs_regen)
                warning_text("Generation settings changed - regenerate to apply them.");

            ImGui::BeginDisabled(busy);
            if (UI::accent_button(busy ? "Generating..." : "Regenerate LODs", ImVec2(-FLT_MIN, 0.f)))
                start_lod_generation();

            const f32 half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            ImGui::BeginDisabled(generated <= 1);
            if (UI::subtle_button("Fit screen sizes", ImVec2(half, 0.f))) {
                fit_screen_sizes(lod, mesh->lods, mesh->bounds);
                live_changed = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Set every level's screen size once from its measured error and 'Pixel error'");
            ImGui::SameLine();
            if (UI::subtle_button("Remove LODs", ImVec2(-FLT_MIN, 0.f))) {
                clear_lods(*mesh);
                set_preview_lod(-1);
                m_dirty = true;
                m_wire_lod = -2;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Delete every generated level, only LOD 0 stays");
            ImGui::EndDisabled();
            ImGui::EndDisabled();
        }
        UI::end_card();

        ImGui::BeginDisabled(busy);

        // ======== generation (needs regeneration) ===================================================================
        if (UI::begin_card("LOD Generation", UI::card_options{ .description = "Changes here apply when you regenerate", .collapsible = true })) {

            static const lod_settings defaults{};

            i32 count = static_cast<i32>(lod.lod_count);
            const i32 default_count = static_cast<i32>(defaults.lod_count);
            if (UI::property_slider("Levels", count, 1, static_cast<i32>(MAX_LODS), "%d", "Number of levels, LOD 0 (the imported mesh) included",
                &default_count)) {
                lod.lod_count = static_cast<u32>(count);
                regen_changed = true;
            }

            static constexpr const char* NORMAL_MODES[] = { "Keep source", "Flat", "Auto smooth", "Smooth" };
            i32 mode = static_cast<i32>((lod.flags & LOD_FLAG_NORMALS_MASK) >> LOD_FLAG_NORMALS_SHIFT);
            const i32 default_mode = static_cast<i32>((defaults.flags & LOD_FLAG_NORMALS_MASK) >> LOD_FLAG_NORMALS_SHIFT);
            if (UI::property_combo("Normals", mode, NORMAL_MODES,
                "Normals of LOD 1+. Keep source reuses LOD 0's normals (can smear on coarse levels), Flat = one per face, "
                "Auto smooth = hard edges above the angle, Smooth = smooth everywhere", &default_mode)) {
                lod.flags = (lod.flags & ~static_cast<u32>(LOD_FLAG_NORMALS_MASK))
                    | ((static_cast<u32>(mode) << LOD_FLAG_NORMALS_SHIFT) & LOD_FLAG_NORMALS_MASK);
                regen_changed = true;
            }
            if (mode == static_cast<i32>(lod_normals_mode::auto_smooth))
                regen_changed |= UI::property_drag("Smooth angle", lod.smooth_angle, 0.5f, 0.0f, 180.0f, "%.0f deg",
                    "Edges between faces bent more than this stay hard", &defaults.smooth_angle);

            auto flag_toggle = [&](const char* label, const u32 bit, const bool inverted, const char* desc) {
                bool value = ((lod.flags & bit) != 0) != inverted;
                const bool default_value = ((defaults.flags & bit) != 0) != inverted;
                if (!UI::property_toggle(label, value, desc, &default_value))
                    return;
                const bool set_bit = value != inverted;
                lod.flags = set_bit ? (lod.flags | bit) : (lod.flags & ~bit);
                regen_changed = true;
            };

            flag_toggle("Lock borders", LOD_FLAG_LOCK_BORDERS, false,
                "Keep open mesh borders in place. Prevents cracks between separately simplified parts, "
                "but limits how far meshes made of many open plates can be reduced");
            flag_toggle("Prune small parts", LOD_FLAG_PRUNE, false,
                "Allow removing small disconnected pieces entirely (still bounded by the level's error limit)");
            flag_toggle("Keep small parts", LOD_FLAG_ALLOW_SUBMESH_REMOVAL, true,
                "Small submeshes are simplified relative to their own size and never removed completely. "
                "Off: coarse levels may drop parts that are tiny compared to the whole mesh");
        }
        UI::end_card();

        // ======== switching (applies immediately) ===================================================================
        if (UI::begin_card("LOD Switching", UI::card_options{ .description = "When each level is used. Applies immediately", .collapsible = true })) {

            static const lod_settings defaults{};

            bool auto_sizes = (lod.flags & LOD_FLAG_AUTO_SCREEN_SIZE) != 0;
            const bool default_auto = (defaults.flags & LOD_FLAG_AUTO_SCREEN_SIZE) != 0;
            if (UI::property_toggle("Auto screen sizes", auto_sizes,
                "Pick each level's switch size from its measured error, so the change stays below 'Pixel error' pixels (at 1080p). "
                "Off: set the screen sizes by hand in the table", &default_auto)) {
                lod.flags = auto_sizes ? (lod.flags | LOD_FLAG_AUTO_SCREEN_SIZE) : (lod.flags & ~LOD_FLAG_AUTO_SCREEN_SIZE);
                if (auto_sizes)
                    fit_screen_sizes(lod, mesh->lods, mesh->bounds);
                live_changed = true;
            }

            if (auto_sizes && UI::property_drag("Pixel error", lod.pixel_error, 0.02f, 0.1f, 16.0f, "%.2f px",
                "How many pixels a level switch may change on screen", &defaults.pixel_error)) {
                fit_screen_sizes(lod, mesh->lods, mesh->bounds);
                live_changed = true;
            }

            i32 bias = static_cast<i32>(lod.shadow_lod_bias);
            const i32 default_bias = static_cast<i32>(defaults.shadow_lod_bias);
            if (UI::property_slider("Shadow LOD bias", bias, 0, 3, "%d",
                "Shadow maps use this many levels coarser than the main view would at the same size", &default_bias)) {
                lod.shadow_lod_bias = static_cast<u32>(bias);
                live_changed = true;
            }

            live_changed |= UI::property_drag("Cull below", lod.cull_screen_size, 0.0005f, 0.0f, 0.2f,
                lod.cull_screen_size > 0.0f ? "%.4f" : "never",
                "Don't draw the mesh at all when it is smaller than this screen size (0 = always draw)", &defaults.cull_screen_size);
        }
        UI::end_card();

        // ======== simplifier weights (advanced) =====================================================================
        if (UI::begin_card("Simplifier Weights", UI::card_options{ .description = "Higher = keep shading / texture seams more faithfully",
            .collapsible = true, .default_open = false })) {

            static const lod_settings defaults{};
            regen_changed |= UI::property_drag("Normals", lod.normal_weight, 0.01f, 0.0f, 4.0f, "%.2f", nullptr, &defaults.normal_weight);
            regen_changed |= UI::property_drag("UVs", lod.uv_weight, 0.01f, 0.0f, 4.0f, "%.2f", nullptr, &defaults.uv_weight);
        }
        UI::end_card();

        ImGui::EndDisabled();   // busy

        if (regen_changed) {
            m_lod_needs_regen = true;
            m_dirty = true;
        }
        if (live_changed) {
            mesh->revision++;                   // renderer re-uploads the switch sizes / bias
            m_dirty = true;
        }
    }


    // geometry detail: discrete LODs or virtual geometry ---------------------------------------------------------------

    // The switch between the two detail systems. A mesh uses exactly one: the inspector shows only that one's cards
    void mesh_editor_window::draw_detail_mode_section() {

        auto registry = GLT::asset::registry::get_ref();
        auto* mesh = registry ? registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle) : nullptr;
        if (!mesh)
            return;

        using namespace GLT::asset::mesh;
        const bool virtual_mode = mesh->detail() == detail_mode::virtual_geometry;

        if (UI::begin_card("Geometry Detail", UI::card_options{ .description = "How the mesh gets cheaper with distance. A mesh uses one of the two",
            .summary = virtual_mode ? "Virtual Geometry" : "Discrete LODs", .collapsible = true })) {

            static constexpr const char* MODES[] = { "Discrete LODs", "Virtual Geometry" };
            i32 index = virtual_mode ? 1 : 0;
            if (UI::property_segmented("Mode", index, MODES,
                "Discrete LODs: the whole mesh switches between a few simplified levels. "
                "Virtual Geometry: the mesh is split into clusters and the detail is picked per cluster and per frame. "
                "Switching to Discrete LODs removes the clusters (they are rebuilt when you switch back)"))
                apply_detail_mode(index == 1 ? detail_mode::virtual_geometry : detail_mode::discrete_lods);

            ImGui::Dummy(ImVec2(0.f, 2.f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", mesh->detail() == detail_mode::virtual_geometry
                ? "Drawn as clusters: close parts at full detail, far parts coarse, hidden clusters not at all. For dense meshes "
                  "seen from near and far. Shadows and translucent materials use the fallback levels."
                : "Drawn as one of a few whole-mesh levels, picked by the mesh's size on screen. For small and medium meshes, "
                  "and anything seen at one distance.");
            ImGui::PopStyleColor();
        }
        UI::end_card();
    }


    // Switches the mesh between the two systems and brings the editor in line: builds what the new mode needs, drops the
    // preview state that belonged to the old one
    void mesh_editor_window::apply_detail_mode(const GLT::asset::mesh::detail_mode mode) {

        auto registry = GLT::asset::registry::get_ref();
        auto* mesh = registry ? registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle) : nullptr;
        if (!mesh || !GLT::asset::mesh::set_detail_mode(*mesh, mode))       // discrete LODs: removes the cluster hierarchy
            return;

        m_dirty = true;
        m_clusters_need_rebuild = false;
        m_wire_lod = -2;
        m_preview_lod = -1;

        if (mode == GLT::asset::mesh::detail_mode::virtual_geometry) {

            start_cluster_build();

            // The discrete levels are the fallback now. A mesh that has none yet gets them with its current LOD settings
            if (mesh->lod_count() <= 1 && mesh->lod.lod_count > 1)
                start_lod_generation();
        } else {

            m_last_cluster_build_ms = 0.0;
            m_show_cluster_bounds = false;

            // The virtual geometry debug views would only show a grey mesh now
            if (m_preview_scene) {
                const u32 current = m_preview_scene->get_render_mode();
                for (const auto& render_mode : m_preview_scene->supported_modes())
                    if (render_mode.id == current && render_mode.has_category()
                        && std::strcmp(render_mode.category, VIRTUAL_GEOMETRY_MODE_CATEGORY) == 0)
                        set_preview_render_mode(default_preview_render_mode());
            }
        }
        submit_preview_instance();

        LOG(info, "mesh_editor: [{}] geometry detail -> {}", m_mesh_name,
            mode == GLT::asset::mesh::detail_mode::virtual_geometry ? "virtual geometry" : "discrete LODs");
    }


    // virtual geometry -------------------------------------------------------------------------------------------------

    // True if the renderer draws the preview instance per cluster: the mesh is in virtual geometry mode, its clusters
    // exist and no fallback level is forced (the renderer setting "Virtual geometry" can still switch the path off)
    bool mesh_editor_window::preview_uses_virtual_geometry(const GLT::asset::mesh::mesh_asset& mesh) const {

        return m_preview_lod < 0 && mesh.uses_virtual_geometry();
    }


    // The preview camera as the cluster selection sees it, with the renderer's default error of one pixel
    GLT::asset::mesh::cluster_view mesh_editor_window::preview_cluster_view(const GLT::asset::mesh::mesh_asset& mesh) const {

        const GLT::world::camera_snapshot cam = build_camera_snapshot();

        GLT::asset::mesh::cluster_view view{};
        view.model = glm::mat4(1.0f);
        view.camera_position = cam.position;
        view.projection = 1.0f / std::tan(glm::radians(cam.fov) * 0.5f);
        view.near_plane = cam.near_plane;
        view.viewport_height = static_cast<f32>(std::max(m_last_preview_size.y, 1u));
        view.pixel_error = 1.0f;
        view.error_scale = mesh.virtual_geometry.error_scale;
        return view;
    }


    // What the preview camera selects. Same rule as the GPU pass, without its frustum / backface culling
    GLT::asset::mesh::cluster_cut mesh_editor_window::preview_cluster_cut(const GLT::asset::mesh::mesh_asset& mesh) const {

        return GLT::asset::mesh::measure_cut(mesh.clusters, preview_cluster_view(mesh));
    }


    void mesh_editor_window::start_cluster_build() {

        if (m_cluster_job.valid())
            return;

        auto registry = GLT::asset::registry::get_ref();
        auto* mesh = registry ? registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle) : nullptr;
        if (!mesh)
            return;

        GLT::asset::mesh::sanitize(mesh->virtual_geometry);

        // Copies: the job runs on a worker thread while the asset stays live (renderer reads it, the editor may close)
        m_cluster_job = std::async(std::launch::async,
            [vertices = mesh->vertices, indices = mesh->indices, submeshes = mesh->submeshes, settings = mesh->virtual_geometry]() {
                return GLT::asset::mesh::build_clusters(vertices, indices, submeshes, settings);
            });
    }


    void mesh_editor_window::poll_cluster_build() {

        if (!m_cluster_job.valid() || m_cluster_job.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;

        GLT::asset::mesh::cluster_build_output result = m_cluster_job.get();
        m_cluster_job = {};

        auto registry = GLT::asset::registry::get_ref();
        auto* mesh = registry ? registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle) : nullptr;
        if (!mesh)
            return;

        // The mode was switched back to discrete LODs while the job ran: such a mesh carries no clusters
        if (mesh->detail() != GLT::asset::mesh::detail_mode::virtual_geometry)
            return;

        // The job worked on a copy of the geometry. Only accept a result that fits the mesh as it is now
        if (!GLT::asset::mesh::validate_clusters(result.clusters, result.indices, result.submeshes, mesh->vertices.size(),
            mesh->submeshes.size())) {

            LOG(warn, "mesh_editor: [{}] cluster build produced no usable hierarchy - the mesh keeps using its fallback levels", m_mesh_name);
            m_clusters_need_rebuild = false;
            return;
        }

        mesh->clusters = std::move(result.clusters);
        mesh->cluster_indices = std::move(result.indices);
        mesh->cluster_submeshes = std::move(result.submeshes);
        mesh->revision++;                       // renderers re-upload (shared_resource_manager::refresh_mesh)

        m_last_cluster_build_ms = result.build_ms;
        m_clusters_need_rebuild = false;
        m_dirty = true;
        m_wire_lod = -2;

        LOG(info, "mesh_editor: [{}] {} clusters in {} levels in {:.1f} ms (coarsest selection {} of {} triangles)", m_mesh_name,
            mesh->clusters.size(), result.level_count, result.build_ms, result.coarsest_triangles, result.source_triangles);
    }


    // Shown instead of the LOD cards while the mesh is in virtual geometry mode
    void mesh_editor_window::draw_virtual_geometry_section() {

        auto registry = GLT::asset::registry::get_ref();
        auto* mesh = registry ? registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle) : nullptr;
        if (!mesh)
            return;

        using namespace GLT::asset::mesh;
        cluster_settings& settings = mesh->virtual_geometry;
        static const cluster_settings defaults{};
        static const lod_settings lod_defaults{};

        const bool busy = m_cluster_job.valid();
        const bool built = mesh->has_clusters();
        bool live_changed = false;              // renderer re-uploads, no rebuild
        bool rebuild_changed = false;           // cluster build inputs: rebuild needed

        // ======== hierarchy: status, statistics, rebuild ============================================================
        const std::string summary = busy ? std::string("building...")
            : (built ? std::format("{} clusters", format_count(mesh->clusters.size())) : std::string("not built"));
        if (UI::begin_card("Virtual Geometry", UI::card_options{ .summary = summary.c_str(), .collapsible = true })) {

            // ---- status ----
            if (busy)
                ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "Building the cluster hierarchy...");
            else if (!built)
                warning_text("No clusters yet - the mesh is drawn with its fallback levels until they are built.");
            else {

                u32 source_clusters = 0, source_triangles = 0, coarsest_triangles = 0, levels = 0;
                for (const cluster& c : mesh->clusters) {
                    levels = std::max(levels, c.depth + 1u);
                    if (c.depth == 0) {
                        source_clusters++;
                        source_triangles += c.index_count / 3;
                    }
                    if (c.parent_error >= CLUSTER_ERROR_NONE)
                        coarsest_triangles += c.index_count / 3;
                }
                const u64 bytes = mesh->clusters.size() * sizeof(cluster) + mesh->cluster_indices.size() * sizeof(u32)
                    + mesh->cluster_submeshes.size() * sizeof(cluster_submesh);

                UI::property_value("Clusters", std::format("{} ({} at full detail)", format_count(mesh->clusters.size()),
                    format_count(source_clusters)), "All hierarchy levels / the clusters holding the source triangles");
                UI::property_value("Levels", std::to_string(levels), "Depth of the hierarchy: each level has about half the triangles of the one below");
                UI::property_value("Triangles", std::format("{} .. {}", format_count(coarsest_triangles), format_count(source_triangles)),
                    "Fewest (far away) and most (close up) triangles the mesh can be drawn with");
                UI::property_value("Memory", GLT::util::format_bytes(bytes), "Cluster records + their index data, CPU side. The GPU holds the same");
                if (m_last_cluster_build_ms > 0.0)
                    UI::property_value("Built in", std::format("{:.0f} ms", m_last_cluster_build_ms));

                const cluster_cut cut = preview_cluster_cut(*mesh);
                UI::property_value("Preview draws", std::format("{} clusters, {} tris", format_count(cut.clusters), format_count(cut.triangles)),
                    m_preview_lod >= 0
                        ? "What the preview camera would select at 1 pixel error. The preview is locked to a fallback level right now"
                        : "What the preview camera selects at 1 pixel error (before frustum and backface culling)");
            }

            // ---- actions ----
            ImGui::Dummy(ImVec2(0.f, 4.f));
            if (m_clusters_need_rebuild && built)
                warning_text("Build settings changed - rebuild to apply them.");

            ImGui::BeginDisabled(busy);
            if (UI::accent_button(busy ? "Building..." : (built ? "Rebuild clusters" : "Build clusters"), ImVec2(-FLT_MIN, 0.f)))
                start_cluster_build();
            ImGui::EndDisabled();
        }
        UI::end_card();

        // ======== detail (applies immediately) ======================================================================
        if (UI::begin_card("Cluster Detail", UI::card_options{ .description = "How much detail is drawn. Applies immediately", .collapsible = true })) {

            if (UI::property_drag("Detail scale", settings.error_scale, 0.01f, 0.25f, 4.0f, "%.2f x",
                "Scales the error of every cluster of this mesh. Above 1 switches to coarser clusters later (more triangles), "
                "below 1 earlier. The renderer setting 'Pixel error' does the same for all meshes", &defaults.error_scale))
                live_changed = true;

            live_changed |= UI::property_drag("Cull below", mesh->lod.cull_screen_size, 0.0005f, 0.0f, 0.2f,
                mesh->lod.cull_screen_size > 0.0f ? "%.4f" : "never",
                "Don't draw the mesh at all when it is smaller than this screen size (0 = always draw)", &lod_defaults.cull_screen_size);
        }
        UI::end_card();

        // ======== build settings (need a rebuild) ===================================================================
        ImGui::BeginDisabled(busy);
        if (UI::begin_card("Cluster Build", UI::card_options{ .description = "Changes here apply when you rebuild", .collapsible = true,
            .default_open = false })) {

            i32 cluster_size = static_cast<i32>(settings.cluster_triangles);
            const i32 default_size = static_cast<i32>(defaults.cluster_triangles);
            if (UI::property_slider("Cluster size", cluster_size, static_cast<i32>(MIN_CLUSTER_TRIANGLES), static_cast<i32>(MAX_CLUSTER_TRIANGLES),
                "%d tris", "Most triangles per cluster. Smaller clusters follow the needed detail more closely but mean more "
                "draw commands; larger ones the opposite", &default_size)) {
                settings.cluster_triangles = static_cast<u32>(cluster_size);
                rebuild_changed = true;
            }

            bool lock_seams = (settings.flags & CLUSTER_FLAG_LOCK_SEAMS) != 0;
            const bool default_lock = (defaults.flags & CLUSTER_FLAG_LOCK_SEAMS) != 0;
            if (UI::property_toggle("Lock material seams", lock_seams,
                "Keep the vertices where two submeshes (materials) meet in place, so no cracks can open between them. "
                "Costs triangles along those seams at a distance", &default_lock)) {
                settings.flags = lock_seams ? (settings.flags | CLUSTER_FLAG_LOCK_SEAMS) : (settings.flags & ~static_cast<u32>(CLUSTER_FLAG_LOCK_SEAMS));
                rebuild_changed = true;
            }

            rebuild_changed |= UI::property_drag("Normal weight", settings.normal_weight, 0.01f, 0.0f, 4.0f, "%.2f",
                "How strongly the simplifier preserves shading", &defaults.normal_weight);
            rebuild_changed |= UI::property_drag("UV weight", settings.uv_weight, 0.01f, 0.0f, 4.0f, "%.2f",
                "How strongly the simplifier preserves texture coordinates", &defaults.uv_weight);
        }
        UI::end_card();
        ImGui::EndDisabled();   // busy

        // ======== fallback: the discrete levels, read-only in this mode ==============================================
        const u32 fallback_levels = mesh->lod_count();
        const bool fallback_busy = m_lod_job.valid();
        const std::string fallback_summary = fallback_busy ? std::string("generating...")
            : (fallback_levels > 1 ? std::format("{} levels", fallback_levels) : std::string("full detail only"));
        if (UI::begin_card("Fallback Levels", UI::card_options{
            .description = "Discrete levels drawn where clusters are not: shadows, translucent materials, virtual geometry switched off",
            .summary = fallback_summary.c_str(), .collapsible = true, .default_open = false })) {

            if (fallback_busy)
                ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "Generating the fallback levels...");
            else if (fallback_levels > 1)
                UI::property_value("Levels", std::format("{}, {} .. {} tris", fallback_levels,
                    format_count(mesh->lod_triangle_count(fallback_levels - 1)), format_count(mesh->lod_triangle_count(0))),
                    "Click a level in the outliner to look at it. To change how they are made, switch the mode to Discrete LODs, "
                    "edit, and switch back");
            else
                ImGui::TextWrapped("None: the fallback is the full-detail mesh, which is expensive for dense meshes.");

            ImGui::BeginDisabled(fallback_busy);
            if (UI::subtle_button(fallback_levels > 1 ? "Regenerate fallback levels" : "Generate fallback levels", ImVec2(-FLT_MIN, 0.f))) {
                if (mesh->lod.lod_count <= 1)
                    mesh->lod.lod_count = lod_defaults.lod_count;
                start_lod_generation();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Simplify the mesh into whole-mesh levels with its LOD settings");
            ImGui::EndDisabled();
        }
        UI::end_card();

        if (rebuild_changed) {
            sanitize(settings);
            m_clusters_need_rebuild = true;
            m_dirty = true;
        }
        if (live_changed) {
            sanitize(settings);
            sanitize(mesh->lod);
            mesh->revision++;                   // renderer picks up the detail scale / the cull size
            m_dirty = true;
        }
    }


    // Debug views of the preview: the renderer's render modes of the category "Virtual Geometry" (the same ones every
    // viewport offers), plus overlays the editor draws from the CPU twin of the selection
    void mesh_editor_window::draw_virtual_geometry_debug_section() {

        if (!m_preview_scene)
            return;

        if (UI::begin_card("Debug Views", UI::card_options{ .description = "Only affects this preview, not the asset", .collapsible = true })) {

            std::vector<const GLT::render::render_mode_info*> views;
            for (const auto& render_mode : m_preview_scene->supported_modes())
                if (render_mode.has_category() && std::strcmp(render_mode.category, VIRTUAL_GEOMETRY_MODE_CATEGORY) == 0)
                    views.push_back(&render_mode);

            if (views.empty()) {
                ImGui::TextDisabled("This renderer has no virtual geometry views");
            } else {

                std::vector<const char*> names;
                names.reserve(views.size() + 1);
                names.push_back("Off");
                i32 index = 0;
                const u32 current = m_preview_scene->get_render_mode();
                for (size_t i = 0; i < views.size(); ++i) {
                    names.push_back(views[i]->display_name);
                    if (views[i]->id == current)
                        index = static_cast<i32>(i) + 1;
                }

                static const i32 off = 0;
                if (UI::property_combo("View", index, std::span<const char* const>(names.data(), names.size()),
                    "Colour the preview by cluster, hierarchy level, triangle or screen error instead of shading it. "
                    "The same views are in the render mode menu of every viewport", &off))
                    set_preview_render_mode(index > 0 && static_cast<size_t>(index) <= views.size()
                        ? views[static_cast<size_t>(index) - 1]->id : default_preview_render_mode());

                if (index > 0 && static_cast<size_t>(index) <= views.size() && views[static_cast<size_t>(index) - 1]->has_tooltip()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::TextWrapped("%s", views[static_cast<size_t>(index) - 1]->tooltip);
                    ImGui::PopStyleColor();
                    ImGui::Dummy(ImVec2(0.f, 2.f));
                }
            }

            static const bool hidden = false;
            UI::property_toggle("Cluster bounds", m_show_cluster_bounds,
                "Bounding sphere of every cluster the preview camera selects, in the cluster's colour of the 'Geometry Clusters' view", &hidden);
            UI::property_toggle("Wireframe", m_show_wireframe,
                "Edges of the triangles the preview camera selects (same as the Wireframe button of the preview)", &hidden);
        }
        UI::end_card();
    }


    // Detail pill + popup at the cursor (top-left overlay of the preview): what the preview shows, and locking it to
    // one discrete level
    void mesh_editor_window::draw_preview_lod_overlay(const ImVec2& /*canvas_min*/, const ImVec2& /*canvas_max*/) {

        auto registry = GLT::asset::registry::get_ref();
        auto* mesh = registry ? registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle) : nullptr;
        if (!mesh || mesh->lod_count() == 0)
            return;

        const bool virtual_mode = mesh->detail() == GLT::asset::mesh::detail_mode::virtual_geometry;
        const u32 lod_count = mesh->lod_count();
        const u32 auto_lod = auto_lod_for(*mesh, preview_screen_size(*mesh));
        const u32 shown = m_preview_lod >= 0 ? std::min<u32>(static_cast<u32>(m_preview_lod), lod_count - 1) : auto_lod;

        const std::string label = virtual_mode
            ? (m_preview_lod < 0 ? std::string("Virtual geometry") : std::format("Fallback LOD {}", shown))
            : (m_preview_lod < 0 ? std::format("LOD auto ({})", auto_lod) : std::format("LOD {}", shown));
        if (ImGui::Button(label.c_str()))
            ImGui::OpenPopup("##preview_lod_popup");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", virtual_mode
                ? "Show the clusters the camera selects, or lock the preview to one fallback level"
                : "Lock the preview to one level, or let its size on screen pick it");

        const ImVec2 btn_min = ImGui::GetItemRectMin();
        const ImVec2 btn_max = ImGui::GetItemRectMax();
        ImGui::SetNextWindowPos(ImVec2(btn_min.x, btn_max.y + 4.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopup("##preview_lod_popup")) {

            if (ImGui::Selectable(virtual_mode ? "Virtual geometry (clusters)" : "Automatic (screen size)", m_preview_lod < 0))
                set_preview_lod(-1);
            ImGui::Separator();
            for (u32 level = 0; level < lod_count; ++level) {
                const std::string item = virtual_mode ? std::format("Fallback LOD {}", level) : std::format("LOD {}", level);
                if (ImGui::Selectable(item.c_str(), m_preview_lod == static_cast<i32>(level)))
                    set_preview_lod(static_cast<i32>(level));
                ImGui::SameLine(virtual_mode ? 150.0f : 110.0f);
                ImGui::TextDisabled("%s tris", format_count(mesh->lod_triangle_count(level)).c_str());
            }
            ImGui::EndPopup();
        }
    }


    void mesh_editor_window::submit_wireframe_debug_draws(const GLT::asset::mesh::mesh_asset& mesh) {

        auto* debug = m_preview_scene ? m_preview_scene->get_debug_draw() : nullptr;
        if (!debug)
            return;

        // Lift the lines slightly off the surface (along the vertex normal) so they win the depth test
        const f32 lift = 0.002f * std::max(GLT::asset::mesh::lod_sphere_radius(mesh.bounds), 1e-4f);
        auto lifted = [&](u32 v) { const auto& vx = mesh.vertex_at(v); return vx.position + vx.normal * lift; };

        // Unique edges of a triangle list, into m_wire_lines. Returns false once the edge budget is used up
        std::unordered_set<u64> seen;
        u32 edges = 0;
        auto add_triangles = [&](auto&& index_at, const u32 first, const u32 count) {

            for (u32 t = 0; t + 2 < count; t += 3) {

                const u32 tri[3] = { index_at(first + t), index_at(first + t + 1), index_at(first + t + 2) };
                for (u32 e = 0; e < 3; ++e) {

                    const u32 a = tri[e];
                    const u32 b = tri[(e + 1) % 3];
                    if (a >= mesh.total_vertex_count() || b >= mesh.total_vertex_count())
                        continue;
                    const u64 key = (static_cast<u64>(std::min(a, b)) << 32) | std::max(a, b);
                    if (!seen.insert(key).second)
                        continue;

                    m_wire_lines.push_back(lifted(a));
                    m_wire_lines.push_back(lifted(b));
                    if (++edges >= MAX_WIREFRAME_EDGES) {
                        m_wire_truncated = true;
                        return false;
                    }
                }
            }
            return true;
        };

        if (preview_uses_virtual_geometry(mesh)) {

            // Virtual geometry: the triangles of the clusters the preview camera selects (what the renderer draws, before
            // its frustum / backface culling). The selection changes with the camera; rebuild the lines only when it does
            GLT::asset::mesh::collect_cut(mesh.clusters, preview_cluster_view(mesh), m_cut_scratch);
            u64 signature = 0xcbf29ce484222325ull;
            for (const u32 index : m_cut_scratch)
                signature = (signature ^ index) * 0x100000001b3ull;

            if (m_wire_lod != WIRE_LEVEL_CLUSTERS || mesh.revision != m_wire_revision || signature != m_wire_cut_signature) {

                m_wire_lod = WIRE_LEVEL_CLUSTERS;
                m_wire_revision = mesh.revision;
                m_wire_cut_signature = signature;
                m_wire_lines.clear();
                m_wire_truncated = false;

                auto cluster_index_at = [&](u32 i) { return i < mesh.cluster_indices.size() ? mesh.cluster_indices[i] : ~0u; };
                for (const u32 index : m_cut_scratch) {
                    const GLT::asset::mesh::cluster& c = mesh.clusters[index];
                    if (!add_triangles(cluster_index_at, c.first_index, c.index_count))
                        break;
                }
            }
        } else {

            const u32 lod_count = mesh.lod_count();
            const i32 level = m_preview_lod >= 0 ? std::min<i32>(m_preview_lod, static_cast<i32>(lod_count) - 1)
                                                 : static_cast<i32>(auto_lod_for(mesh, preview_screen_size(mesh)));

            // Unique edges of the shown level, cached until the level or the geometry changes
            if (level != m_wire_lod || mesh.revision != m_wire_revision) {

                m_wire_lod = level;
                m_wire_revision = mesh.revision;
                m_wire_lines.clear();
                m_wire_truncated = false;

                auto level_index_at = [&](u32 i) { return mesh.index_at(i); };
                for (u32 sm = 0; sm < mesh.submeshes.size(); ++sm) {
                    const GLT::asset::mesh::lod_range range = mesh.range(static_cast<u32>(level), sm);
                    if (!add_triangles(level_index_at, range.first_index, range.index_count))
                        break;
                }
            }
        }

        const glm::vec4 color{ 0.35f, 0.85f, 1.0f, 0.55f };
        for (size_t i = 0; i + 1 < m_wire_lines.size(); i += 2)
            debug->line(m_wire_lines[i], m_wire_lines[i + 1], color, 0.0f, true);
    }


    // Bounding spheres (the ones the renderer culls with) of the clusters the preview camera selects, each in its
    // cluster's colour of the "Geometry Clusters" view. Limited to the nearest MAX_CLUSTER_BOUNDS
    void mesh_editor_window::submit_cluster_bounds_debug_draws(const GLT::asset::mesh::mesh_asset& mesh) {

        auto* debug = m_preview_scene ? m_preview_scene->get_debug_draw() : nullptr;
        if (!debug)
            return;

        const GLT::asset::mesh::cluster_view view = preview_cluster_view(mesh);
        GLT::asset::mesh::collect_cut(mesh.clusters, view, m_cut_scratch);

        if (m_cut_scratch.size() > MAX_CLUSTER_BOUNDS) {

            auto distance_of = [&](const u32 index) {
                const glm::vec4& sphere = mesh.clusters[index].bounds;
                return glm::distance(glm::vec3(sphere), view.camera_position) - sphere.w;
            };
            std::partial_sort(m_cut_scratch.begin(), m_cut_scratch.begin() + MAX_CLUSTER_BOUNDS, m_cut_scratch.end(),
                [&](const u32 a, const u32 b) { return distance_of(a) < distance_of(b); });
            m_cut_scratch.resize(MAX_CLUSTER_BOUNDS);
            m_cluster_bounds_truncated = true;
        }

        for (const u32 index : m_cut_scratch) {
            const GLT::asset::mesh::cluster& c = mesh.clusters[index];
            debug->sphere(glm::vec3(c.bounds), c.bounds.w, glm::vec4(GLT::asset::mesh::cluster_debug_color(index), 0.85f), 0.0f, true,
                CLUSTER_BOUNDS_SEGMENTS);
        }
    }

}
