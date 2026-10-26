#include "util/pch.h"
#include "mesh_editor.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <cstdio>

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

#include "util/ui/pannel_collection.h"
#include "util/ui/asset_picker.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    constexpr f32                                   MIN_PREVIEW_SIDE = 64.0f;

    constexpr f32                                   MIN_CAM_DISTANCE = 0.05f;

    constexpr f32                                   MAX_CAM_DISTANCE = 1500.0f;

    constexpr u32                                   DEFAULT_PREVIEW_SIZE = 512;

    constexpr const char*                           DRAG_PAYLOAD_ID = "CONTENT_BROWSER_ITEM";

    // Panel layout defaults for the three-column body
    constexpr f32                                   SPLITTER_WIDTH = 6.0f;

    constexpr f32                                   MIN_OUTLINER_WIDTH = 150.0f;

    constexpr f32                                   MIN_INSPECTOR_WIDTH = 200.0f;

    constexpr f32                                   BODY_MIN_WIDTH = 400.0f;

    constexpr f32                                   BODY_MIN_HEIGHT = 200.0f;

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

        // Draw a 2px visual bar centered in the splitter zone
        const ImU32 col = (hovered || active)
            ? IM_COL32(200, 200, 200, 220)
            : IM_COL32(100, 100, 100, 100);

        const f32 cx = cursor.x + SPLITTER_WIDTH * 0.5f;
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(cx - 1.0f, cursor.y + 4.0f),
            ImVec2(cx + 1.0f, cursor.y + height - 4.0f),
            col);
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

        if (ImGui::Begin(m_window_id.c_str(), &m_show_window)) {

            handle_drag_drop();
            draw_toolbar();
            ImGui::Separator();
            draw_body();
        }
        ImGui::End();
    }


    void mesh_editor_window::update(const f32 delta_time) {

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

    void mesh_editor_window::draw_toolbar() {

        ImGui::BeginDisabled(!m_has_mesh);

        if (ImGui::Button("Reload", ImVec2(80.0f, 0.0f)))
            open(m_mesh_path);

        ImGui::SameLine();
        if (ImGui::Button("Save", ImVec2(80.0f, 0.0f)))
            save();

        ImGui::EndDisabled();

        if (m_dirty) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 200, 90, 255));
            ImGui::TextUnformatted("Unsaved changes");
            ImGui::PopStyleColor();
        }

        ImGui::SameLine(0.0f, 24.0f);

        ImGui::Checkbox("Grid", &m_show_grid);
        ImGui::SameLine();
        ImGui::Checkbox("Origin", &m_show_origin);
        ImGui::SameLine();
        ImGui::Checkbox("Colliders", &m_show_colliders_in_preview);
        draw_collider_gizmo_toolbar();

        if (m_preview_scene) {

            ImGui::SameLine(0.0f, 24.0f);

            const auto modes = m_preview_scene->supported_modes();
            const u32 current_id = m_preview_scene->get_render_mode();

            i32 current_index = -1;
            for (u32 i = 0; i < modes.size(); ++i)
                if (modes[i].id == current_id) { current_index = i; break; }

            const std::string preview_label = (current_index >= 0)
                ? std::string(modes[current_index].key) : std::string("(unknown)");

            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::BeginCombo("##render_mode", preview_label.c_str())) {
                for (u32 i = 0; i < modes.size(); ++i) {
                    const bool selected = (modes[i].id == current_id);
                    if (ImGui::Selectable(std::string(modes[i].key).c_str(), selected)) {
                        m_current_mode_id = modes[i].id;
                        m_preview_scene->set_render_mode(modes[i].id);
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
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
        ImGui::BeginChild("##outliner", ImVec2(m_outliner_width, total_h), true);
        draw_outliner_panel();
        ImGui::EndChild();

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
        ImGui::BeginChild("##inspector", ImVec2(0.0f, total_h), true);
        draw_inspector_panel();
        ImGui::EndChild();
    }


    // outliner --------------------------------------------------------------------------------------------------------

    void mesh_editor_window::draw_outliner_panel() {

        if (m_load_failed) {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 90, 90, 255));
            ImGui::TextUnformatted("Failed to load mesh.");
            ImGui::PopStyleColor();
            return;
        }

        if (!m_has_mesh) {
            ImGui::TextDisabled("No mesh loaded.");
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
            const bool mesh_selected = (m_inspector_target == inspector_target::mesh);
            if (ImGui::Selectable(m_mesh_name.c_str(), mesh_selected, ImGuiSelectableFlags_SpanAvailWidth))
                select_mesh();
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (m_mesh_path.empty())
            ImGui::TextUnformatted("  (unsaved)");
        else
            ImGui::TextWrapped("  %s", m_mesh_path.generic_string().c_str());
        ImGui::PopStyleColor();

        ImGui::Separator();

        i32 new_hovered_sub = -1;

        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 12.0f);

        // ---- submeshes ----
        if (ImGui::TreeNodeEx("Submeshes", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {

            if (mesh->submeshes.empty()) {
                ImGui::TextDisabled("(none)");
            }

            for (u32 i = 0; i < mesh->submesh_count(); ++i) {

                ImGui::PushID(static_cast<int>(i));

                const bool selected =
                    (m_inspector_target == inspector_target::submesh && m_selected_submesh == static_cast<i32>(i));

                char label[32];
                std::snprintf(label, sizeof(label), "%u", i);

                if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_SpanAvailWidth)) {
                    if (selected) select_mesh();
                    else          select_submesh(static_cast<i32>(i));
                }

                if (ImGui::IsItemHovered())
                    new_hovered_sub = static_cast<i32>(i);

                ImGui::SameLine();
                ImGui::TextDisabled("[%u..%u]", mesh->submeshes[i].first_index,
                    mesh->submeshes[i].first_index + mesh->submeshes[i].index_count);

                ImGui::PopID();
            }

            ImGui::TreePop();
        }

        // ---- colliders ----
        m_hovered_collider = -1;

        const bool colliders_open =
            ImGui::TreeNodeEx("Colliders", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);

        if (colliders_open) {

            if (ImGui::Button("+ Add collider", ImVec2(-1.0f, 0.0f)))
                ImGui::OpenPopup("add_collider_popup");

            draw_add_collider_popup();

            if (m_colliders.empty()) {
                ImGui::TextDisabled("(none)");
            } else {
                for (i32 i = 0; i < static_cast<i32>(m_colliders.size()); ++i)
                    draw_collider_row(i);
            }

            ImGui::TreePop();
        }

        ImGui::PopStyleVar();

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

        const bool selected =
            (m_inspector_target == inspector_target::collider && m_selected_collider == index);

        char label[192];
        std::snprintf(label, sizeof(label), "%s%s  [%s]",
            item.visible ? "" : "(hidden) ",
            item.name.c_str(),
            collider_shape_kind_name(item.kind));

        if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_SpanAvailWidth)) {
            if (selected) select_mesh();
            else          select_collider(index);
        }

        if (ImGui::IsItemHovered())
            m_hovered_collider = index;

        if (ImGui::BeginPopupContextItem()) {

            if (ImGui::MenuItem(item.visible ? "Hide" : "Show"))
                item.visible = !item.visible;

            ImGui::Separator();

            if (ImGui::MenuItem("Delete"))
                m_pending_delete_collider = index;

            ImGui::EndPopup();
        }

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
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 90, 90, 255));
            ImGui::TextUnformatted("Failed to load mesh.");
            ImGui::PopStyleColor();
            return;
        }

        if (!m_has_mesh) {
            ImGui::TextDisabled("No mesh loaded.");
            return;
        }

        if (ImGui::BeginChild("##inspector_scroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_None)) {

            switch (m_inspector_target) {
                case inspector_target::mesh:        draw_inspector_mesh();       break;
                case inspector_target::submesh:     draw_inspector_submesh();    break;
                case inspector_target::collider:    draw_inspector_collider();   break;
            }
        }
        ImGui::EndChild();
    }


    void mesh_editor_window::draw_inspector_mesh() {

        ImGui::TextUnformatted("Mesh");
        ImGui::Separator();

        draw_mesh_summary_section();
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

        ImGui::Text("Submesh %d", m_selected_submesh);
        ImGui::Separator();

        if (GLT::UI::begin_table("submesh_info_table", false, ImVec2(0, 0), 0.0f, true, 0.42f)) {

            GLT::UI::table_row("index range",
                std::to_string(sm.first_index) + ".." + std::to_string(sm.first_index + sm.index_count));

            if (m_selected_submesh < static_cast<i32>(m_submesh_bounds_cache.size())) {

                const auto& b = m_submesh_bounds_cache[static_cast<size_t>(m_selected_submesh)];
                const glm::vec3 sz = b.max - b.min;

                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.3f, %.3f, %.3f", sz.x, sz.y, sz.z);
                GLT::UI::table_row("size", std::string_view{ buf });
            }

            GLT::UI::end_table();
        }

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::TextUnformatted("Material");

        const auto old_handle = sm.material;
        if (GLT::UI::asset_picker_widget({
                .label = "##submesh_mat",
                .filter = GLT::asset::core_types::material,
                .allow_clear = true,
                .placeholder = "(none)"
            }, sm.material, ImGui::GetContentRegionAvail().x)) {

            if (old_handle != INVALID_HANDLE && old_handle != sm.material)
                registry->release(old_handle);
            if (sm.material != INVALID_HANDLE)
                registry->add_dependency(m_asset_handle, sm.material);

            m_dirty = true;
        }

        if (m_dirty) {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 200, 90, 255));
            ImGui::TextWrapped("Material changes are preview-only.");
            ImGui::PopStyleColor();
        }
    }


    void mesh_editor_window::draw_inspector_collider() {

        if (m_selected_collider < 0 || m_selected_collider >= static_cast<i32>(m_colliders.size())) {
            select_mesh();
            return;
        }

        auto& item = m_colliders[static_cast<size_t>(m_selected_collider)];

        ImGui::TextUnformatted("Collider");
        ImGui::Separator();

        // ---- identity ----
        {
            char name_buf[128];
            std::snprintf(name_buf, sizeof(name_buf), "%s", item.name.c_str());
            if (ImGui::InputText("Name", name_buf, sizeof(name_buf)))
                item.name = name_buf;
        }

        ImGui::TextDisabled("Type: %s", collider_shape_kind_name(item.kind));

        ImGui::Checkbox("Visible", &item.visible);
        ImGui::SameLine();
        ImGui::ColorEdit4("##collider_color", &item.color.x,
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);

        ImGui::Spacing();
        ImGui::Separator();

        // ---- transform ----
        ImGui::TextUnformatted("Transform");
        bool changed = false;

        changed |= ImGui::DragFloat3("Position", &item.position.x, 0.01f);

        glm::vec3 euler = glm::degrees(glm::eulerAngles(item.rotation));
        if (ImGui::DragFloat3("Rotation", &euler.x, 0.5f)) {
            item.rotation = glm::quat(glm::radians(euler));
            changed = true;
        }

        ImGui::Spacing(); ImGui::Separator();
        ImGui::TextUnformatted("Shape");

        switch (item.kind) {
            case collider_shape_kind::box:
                changed |= ImGui::DragFloat3("Half Extents", &item.box_half_extents.x, 0.01f, 0.001f, 10000.0f);
                break;
            case collider_shape_kind::sphere:
                changed |= ImGui::DragFloat("Radius", &item.sphere_radius, 0.01f, 0.001f, 10000.0f);
                break;
            case collider_shape_kind::capsule:
                changed |= ImGui::DragFloat("Radius",      &item.capsule_radius,      0.01f, 0.001f, 10000.0f);
                changed |= ImGui::DragFloat("Half Height", &item.capsule_half_height, 0.01f, 0.0f,   10000.0f);
                break;
            case collider_shape_kind::cylinder:
                changed |= ImGui::DragFloat("Radius",      &item.cylinder_radius,      0.01f, 0.001f, 10000.0f);
                changed |= ImGui::DragFloat("Half Height", &item.cylinder_half_height, 0.01f, 0.001f, 10000.0f);
                break;
            case collider_shape_kind::convex_hull:
                ImGui::TextDisabled("Auto-generated from mesh vertices.");
                ImGui::TextColored(ImVec4(1.f, 0.7f, 0.3f, 1.f),
                    "Not yet supported by the runtime format - will be skipped on save.");
                break;
            case collider_shape_kind::triangle_mesh:
                ImGui::TextDisabled("Auto-generated from mesh triangle soup.");
                break;
        }

        ImGui::Spacing(); ImGui::Separator();
        ImGui::TextUnformatted("Material");

        changed |= ImGui::SliderFloat("Friction",    &item.material.friction,    0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Restitution", &item.material.restitution, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::DragFloat ("Density",      &item.material.density,     10.0f, 0.0f, 30000.0f, "%.0f kg/m³");

        if (changed)
            m_dirty = true;

        ImGui::Spacing(); ImGui::Separator();

        // ---- actions ----
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(140, 40, 40, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(180, 50, 50, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(200, 60, 60, 255));

        if (ImGui::Button("Delete collider", ImVec2(-1.0f, 0.0f)))
            m_pending_delete_collider = m_selected_collider;

        ImGui::PopStyleColor(3);

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::TextDisabled("Collider editing is not yet saved with the asset.");
    }


    // mesh-only inspector sections -------------------------------------------------------------------------------------

    void mesh_editor_window::draw_mesh_summary_section() {

        if (!ImGui::CollapsingHeader("Summary", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return;

        auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle);
        if (!mesh)
            return;

        if (!GLT::UI::begin_table("mesh_summary_table", false, ImVec2(0, 0), 0.0f, true, 0.42f))
            return;

        GLT::UI::table_row("vertices",  std::to_string(mesh->vertices.size()));
        GLT::UI::table_row("indices",   std::to_string(mesh->indices.size()));
        GLT::UI::table_row("triangles", std::to_string(mesh->indices.size() / 3));
        GLT::UI::table_row("submeshes", std::to_string(mesh->submesh_count()));
        GLT::UI::table_row("memory",    GLT::util::format_bytes(mesh->memory_usage()));

        GLT::UI::end_table();
    }


    void mesh_editor_window::draw_mesh_bounds_section() {

        if (!ImGui::CollapsingHeader("Bounds"))
            return;

        auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return;

        auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle);
        if (!mesh)
            return;

        glm::vec3 mn, mx;
        compute_mesh_bounds(*mesh, mn, mx);
        const glm::vec3 size = mx - mn;

        if (!GLT::UI::begin_table("mesh_bounds_table", false, ImVec2(0, 0), 0.0f, true, 0.42f))
            return;

        char buf[128];

        std::snprintf(buf, sizeof(buf), "%.3f, %.3f, %.3f", mn.x, mn.y, mn.z);
        GLT::UI::table_row("min", std::string_view{ buf });

        std::snprintf(buf, sizeof(buf), "%.3f, %.3f, %.3f", mx.x, mx.y, mx.z);
        GLT::UI::table_row("max", std::string_view{ buf });

        std::snprintf(buf, sizeof(buf), "%.3f, %.3f, %.3f", size.x, size.y, size.z);
        GLT::UI::table_row("size", std::string_view{ buf });

        GLT::UI::end_table();
    }


    void mesh_editor_window::draw_preview_settings_section() {

        if (!ImGui::CollapsingHeader("Preview"))
            return;

        if (ImGui::ColorEdit4("Background", &m_background_color.x,
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
            // m_preview_scene->set_clear_color(m_background_color);
        }

        ImGui::Checkbox("Auto-rotate", &m_auto_rotate);
        if (m_auto_rotate) {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderFloat("##auto_rotate_speed", &m_auto_rotate_speed, 0.01f, 100.f, "%.2f %%");
        }
    }


    void mesh_editor_window::draw_camera_section() {

        if (!ImGui::CollapsingHeader("Camera"))
            return;

        if (ImGui::Button("Reset to fit", ImVec2(-1.0f, 0.0f)))
            reset_camera_to_fit();

        ImGui::SliderFloat("Distance", &m_preview_camera.distance, MIN_CAM_DISTANCE, MAX_CAM_DISTANCE, "%.2f");
        ImGui::SliderFloat("FOV",      &m_preview_camera.fov_deg, 15.0f, 110.0f, "%.0f deg");
        ImGui::DragFloat3("Target",    &m_preview_camera.target.x, 0.01f);
        ImGui::DragFloat("Yaw",        &m_preview_camera.yaw_rad,   0.01f);
        ImGui::DragFloat("Pitch",      &m_preview_camera.pitch_rad, 0.01f);
    }


    void mesh_editor_window::draw_lighting_section() {

        if (!ImGui::CollapsingHeader("Lighting"))
            return;

        ImGui::SliderFloat("Key",  &m_preview_lighting.key_intensity,  0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Fill", &m_preview_lighting.fill_intensity, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Rim",  &m_preview_lighting.rim_intensity,  0.0f, 1.0f, "%.2f");

        ImGui::Checkbox("Headlight (key follows camera)", &m_preview_lighting.headlight);
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
            ImGui::TextDisabled("Open a mesh asset to preview it.");
            return;
        }

        if (!m_preview_scene) {
            ImGui::TextDisabled("Preview target unavailable.");
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
            draw->AddRect(canvas_min, canvas_max, IM_COL32(60, 60, 70, 255));
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

        // ---------- overlay buttons (lower-right, gizmo style) ----------
        //
        // Uses the same color ladder as world_viewport's gizmo_toggle_button
        // so the two surfaces read as the same UI family.
        {
            const ImVec4& c_weak   = ImVec4(0.10f, 0.10f, 0.10f, 0.55f);
            const ImVec4& c_hover  = GLT::imgui_config::get_action_color00_default_ref();
            const ImVec4& c_active = GLT::imgui_config::get_action_color00_hover_ref();

            ImGui::PushStyleColor(ImGuiCol_Button,        c_weak);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c_hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  c_active);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(8.0f, 4.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(4.0f, 4.0f));

            // Measure the total width of the four buttons so we can right-align
            const char* labels[] = { "Fit", "Front", "Top", "Side" };
            const f32   frame_pad_x = ImGui::GetStyle().FramePadding.x * 2.0f;
            const f32   item_gap    = ImGui::GetStyle().ItemSpacing.x;
            f32         total_w     = item_gap * 3.0f;
            for (const char* l : labels)
                total_w += ImGui::CalcTextSize(l).x + frame_pad_x;

            const f32 x = canvas_max.x - total_w - 10.0f;
            const f32 y = canvas_max.y - ImGui::GetFrameHeight() - 10.0f;
            ImGui::SetCursorScreenPos(ImVec2(x, y));

            if (ImGui::Button("Fit"))   reset_camera_to_fit();
            ImGui::SameLine();
            if (ImGui::Button("Front")) { m_preview_camera.yaw_rad = 0.0f; m_preview_camera.pitch_rad = 0.0f; }
            ImGui::SameLine();
            if (ImGui::Button("Top"))   { m_preview_camera.yaw_rad = 0.0f; m_preview_camera.pitch_rad = 1.4f; }
            ImGui::SameLine();
            if (ImGui::Button("Side"))  { m_preview_camera.yaw_rad = glm::half_pi<f32>(); m_preview_camera.pitch_rad = 0.0f; }

            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(3);
        }

        // ---------- hint text (lower-left) ----------
        ImGui::SetCursorScreenPos(ImVec2(canvas_min.x + 10.0f, canvas_max.y - ImGui::GetFrameHeight() - 10.0f));
        ImGui::TextDisabled("LMB orbit   MMB pan   wheel zoom");
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

        GLT::asset::mesh::instance inst{};
        inst.mesh = m_asset_handle;
        inst.transform = glm::mat4(1.0f);
        m_preview_scene->submit_scene(std::span<const GLT::asset::mesh::instance>(&inst, 1));

        submit_preview_lights();

        const auto modes = m_preview_scene->supported_modes();
        if (!modes.empty()) {
            m_current_mode_id = modes.front().id;
            m_preview_scene->set_render_mode(m_current_mode_id);
        }
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


    void mesh_editor_window::draw_collider_gizmo_toolbar() {

        if (m_inspector_target != inspector_target::collider || m_selected_collider < 0)
            return;

        auto toggle = [](const char* label, bool active) {
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.50f, 0.80f, 1.0f));
            const bool clicked = ImGui::Button(label);
            if (active) ImGui::PopStyleColor();
            return clicked;
        };

        ImGui::SameLine(0.0f, 24.0f);
        ImGui::TextUnformatted("Gizmo:");
        ImGui::SameLine();
        if (toggle("T", m_collider_gizmo_operation == ImGuizmo::TRANSLATE))
            m_collider_gizmo_operation = ImGuizmo::TRANSLATE;
        ImGui::SameLine();
        if (toggle("R", m_collider_gizmo_operation == ImGuizmo::ROTATE))
            m_collider_gizmo_operation = ImGuizmo::ROTATE;
        ImGui::SameLine();
        if (toggle("S", m_collider_gizmo_operation == ImGuizmo::SCALE))
            m_collider_gizmo_operation = ImGuizmo::SCALE;

        ImGui::SameLine(0.0f, 16.0f);
        ImGui::TextUnformatted("Space:");
        ImGui::SameLine();
        if (toggle("World", m_collider_gizmo_mode == ImGuizmo::WORLD))
            m_collider_gizmo_mode = ImGuizmo::WORLD;
        ImGui::SameLine();
        if (toggle("Local", m_collider_gizmo_mode == ImGuizmo::LOCAL))
            m_collider_gizmo_mode = ImGuizmo::LOCAL;
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

}
