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

    constexpr f32                                   OUTLINER_PANEL_WIDTH = 240.0f;

    constexpr f32                                   INSPECTOR_PANEL_WIDTH = 340.0f;

    constexpr f32                                   PANEL_GAP = 4.0f;

    constexpr f32                                   MIN_PREVIEW_SIDE = 64.0f;

    constexpr f32                                   MIN_CAM_DISTANCE = 0.05f;

    constexpr f32                                   MAX_CAM_DISTANCE = 1500.0f;

    constexpr u32                                   DEFAULT_PREVIEW_SIZE = 512;

    constexpr const char*                           DRAG_PAYLOAD_ID = "CONTENT_BROWSER_ITEM";

    static constexpr const char*                    kRenderModeLabels[] = {
        "Lit", "Unlit", "Wireframe", "Wireframe Overlay", "Normals", "UVs"
    };

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    namespace {

        // ---- wireframe helpers -------------------------------------------------------------------------------------

        void draw_box_wireframe(GLT::render::i_debug_draw& debug, const glm::mat4& xform, const glm::vec3& he,
            const glm::vec4& color) {

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
            f32 radius, f32 half_height, const glm::vec4& color, u32 segments = 16) {

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
        VALIDATE(GLT::vfs::exists(PROJECT_CONTENT_DIR / path, error) && !error,
            m_load_failed = true; m_show_window = true; return,
            "", "file not found [{}]", (path.generic_string()))

        VALIDATE(!GLT::vfs::is_directory(PROJECT_CONTENT_DIR / path, error) && !error,
            m_load_failed = true; m_show_window = true; return,
            "", "path is a directory [{}]", (path.generic_string()))

        auto registry = GLT::asset::registry::get_ref();
        VALIDATE(registry, m_load_failed = true; m_show_window = true; return, "", "no asset registry")

        auto loaded = registry->load(path);
        VALIDATE(loaded, m_load_failed = true; m_show_window = true; return, "", "failed to load mesh [{}]", (path.generic_string()))

        if (!load_from_handle(*loaded)) {
            registry->unload(*loaded);
            m_load_failed = true;
        }

        ensure_preview();
        reset_camera_to_fit();

        m_show_window = true;
    }


    void mesh_editor_window::close_mesh() {

        destroy_preview();

        if (m_asset_handle != INVALID_HANDLE) {
            if (auto registry = GLT::asset::registry::get_ref())
                registry->unload(m_asset_handle);
            m_asset_handle = INVALID_HANDLE;
        }

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

        if (!m_has_mesh || m_asset_handle == INVALID_HANDLE)
            return false;

        auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return false;

        auto result = registry->save(m_asset_handle);
        if (!result) {
            LOG(error, "mesh_editor: save failed for [{}] (error {}). "
                       "Meshes currently return [no_handler] from the handler.",
                m_mesh_name, static_cast<int>(result.error()));
            return false;
        }

        m_dirty = false;
        return true;
    }

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

        if (avail.x < 100.0f || avail.y < 100.0f)
            return;

        const f32 body_width = avail.x;
        const f32 outliner_w = OUTLINER_PANEL_WIDTH;
        const f32 inspector_w = INSPECTOR_PANEL_WIDTH;

        f32 preview_w = body_width - outliner_w - inspector_w - PANEL_GAP * 2.0f;
        if (preview_w < MIN_PREVIEW_SIDE) {
            // Shrink the side panels proportionally if the window is too narrow
            const f32 scale = std::max(0.4f, (body_width - MIN_PREVIEW_SIDE - PANEL_GAP * 2.0f) / (outliner_w + inspector_w));
            const f32 o = outliner_w * scale;
            const f32 i = inspector_w * scale;
            preview_w = body_width - o - i - PANEL_GAP * 2.0f;
            ImGui::BeginChild("##outliner", ImVec2(o, 0.0f), true);
        } else {
            ImGui::BeginChild("##outliner", ImVec2(outliner_w, 0.0f), true);
        }
        draw_outliner_panel();
        ImGui::EndChild();

        ImGui::SameLine(0.0f, PANEL_GAP);

        ImGui::BeginChild("##preview_region", ImVec2(preview_w, 0.0f), false);
        draw_preview_panel();
        ImGui::EndChild();

        ImGui::SameLine(0.0f, PANEL_GAP);

        ImGui::BeginChild("##inspector", ImVec2(0.0f, 0.0f), true);
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
        i32 new_hovered_col = -1;

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
        m_hovered_collider = new_hovered_col;

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

        ImGui::DragFloat3("Position", &item.position.x, 0.01f);

        glm::vec3 euler = glm::degrees(glm::eulerAngles(item.rotation));
        if (ImGui::DragFloat3("Rotation", &euler.x, 0.5f))
            item.rotation = glm::quat(glm::radians(euler));

        ImGui::Spacing();
        ImGui::Separator();

        // ---- shape parameters ----
        ImGui::TextUnformatted("Shape");

        switch (item.kind) {

            case collider_shape_kind::box:
                ImGui::DragFloat3("Half Extents", &item.box_half_extents.x, 0.01f, 0.001f, 10000.0f);
                break;

            case collider_shape_kind::sphere:
                ImGui::DragFloat("Radius", &item.sphere_radius, 0.01f, 0.001f, 10000.0f);
                break;

            case collider_shape_kind::capsule:
                ImGui::DragFloat("Radius",      &item.capsule_radius,      0.01f, 0.001f, 10000.0f);
                ImGui::DragFloat("Half Height", &item.capsule_half_height, 0.01f, 0.0f,   10000.0f);
                break;

            case collider_shape_kind::cylinder:
                ImGui::DragFloat("Radius",      &item.cylinder_radius,      0.01f, 0.001f, 10000.0f);
                ImGui::DragFloat("Half Height", &item.cylinder_half_height, 0.01f, 0.001f, 10000.0f);
                break;

            case collider_shape_kind::convex_hull:
                ImGui::TextDisabled("Auto-generated from mesh vertices (not yet plumbed).");
                break;

            case collider_shape_kind::triangle_mesh:
                ImGui::TextDisabled("Auto-generated from mesh triangle soup (not yet plumbed).");
                break;
        }

        ImGui::Spacing();
        ImGui::Separator();

        // ---- material ----
        ImGui::TextUnformatted("Material");

        ImGui::SliderFloat("Friction",    &item.material.friction,    0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Restitution", &item.material.restitution, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("Density",       &item.material.density,     10.0f, 0.0f, 30000.0f, "%.0f kg/m³");

        ImGui::Spacing();
        ImGui::Separator();

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

        if (!UI::begin_collapsing_header_section("Summary"))
            return;

        auto registry = GLT::asset::registry::get_ref();
        if (!registry) {
            UI::end_collapsing_header_section();
            return;
        }

        auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle);
        if (!mesh) {
            UI::end_collapsing_header_section();
            return;
        }

        if (!GLT::UI::begin_table("mesh_summary_table", false, ImVec2(0, 0), 0.0f, true, 0.42f)) {
            UI::end_collapsing_header_section();
            return;
        }

        GLT::UI::table_row("vertices", std::to_string(mesh->vertices.size()));
        GLT::UI::table_row("indices", std::to_string(mesh->indices.size()));
        GLT::UI::table_row("triangles", std::to_string(mesh->indices.size() / 3));
        GLT::UI::table_row("submeshes", std::to_string(mesh->submesh_count()));
        GLT::UI::table_row("memory", GLT::util::format_bytes(mesh->memory_usage()));

        GLT::UI::end_table();
        UI::end_collapsing_header_section();
    }


    void mesh_editor_window::draw_mesh_bounds_section() {

        if (!UI::begin_collapsing_header_section("Bounds"))
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

        if (!GLT::UI::begin_table("mesh_bounds_table", false, ImVec2(0, 0), 0.0f, true, 0.42f)) {
            
            UI::end_collapsing_header_section();
            return;
        }

        char buf[128];

        std::snprintf(buf, sizeof(buf), "%.3f, %.3f, %.3f", mn.x, mn.y, mn.z);
        GLT::UI::table_row("min", std::string_view{ buf });

        std::snprintf(buf, sizeof(buf), "%.3f, %.3f, %.3f", mx.x, mx.y, mx.z);
        GLT::UI::table_row("max", std::string_view{ buf });

        std::snprintf(buf, sizeof(buf), "%.3f, %.3f, %.3f", size.x, size.y, size.z);
        GLT::UI::table_row("size", std::string_view{ buf });

        GLT::UI::end_table();
        UI::end_collapsing_header_section();
    }


    void mesh_editor_window::draw_preview_settings_section() {

        if (!UI::begin_collapsing_header_section("Preview"))
            return;

        if (ImGui::ColorEdit4("Background", &m_background_color.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
            // m_preview_scene->set_clear_color(m_background_color);
        }

        ImGui::Checkbox("Auto-rotate", &m_auto_rotate);
        if (m_auto_rotate) {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderFloat("##auto_rotate_speed", &m_auto_rotate_speed, 0.01f, 100.f, "%.2f %%");
        }

        UI::end_collapsing_header_section();
    }


    void mesh_editor_window::draw_camera_section() {

        if (!UI::begin_collapsing_header_section("Camera"))
            return;

        if (ImGui::Button("Reset to fit", ImVec2(-1.0f, 0.0f)))
            reset_camera_to_fit();

        ImGui::SliderFloat("Distance", &m_preview_camera.distance, MIN_CAM_DISTANCE, MAX_CAM_DISTANCE, "%.2f");
        ImGui::SliderFloat("FOV", &m_preview_camera.fov_deg, 15.0f, 110.0f, "%.0f deg");
        ImGui::DragFloat3("Target", &m_preview_camera.target.x, 0.01f);
        ImGui::DragFloat("Yaw", &m_preview_camera.yaw_rad,   0.01f);
        ImGui::DragFloat("Pitch", &m_preview_camera.pitch_rad, 0.01f);
        UI::end_collapsing_header_section();
    }


    void mesh_editor_window::draw_lighting_section() {

        if (!UI::begin_collapsing_header_section("Lighting"))
            return;

        ImGui::SliderFloat("Key", &m_preview_lighting.key_intensity,  0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Fill", &m_preview_lighting.fill_intensity, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Rim", &m_preview_lighting.rim_intensity,  0.0f, 1.0f, "%.2f");

        ImGui::Checkbox("Headlight (key follows camera)", &m_preview_lighting.headlight);
        UI::end_collapsing_header_section();
    }

    // collider state helpers -------------------------------------------------------------------------------------------

    void mesh_editor_window::add_collider(collider_shape_kind kind) {

        collider_editor_item item{};
        item.id = m_next_collider_id++;
        item.kind = kind;
        item.name = std::string(collider_shape_kind_name(kind)) + " " + std::to_string(item.id);

        // Seed geometry from mesh bounds where it makes sense
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
    }


    void mesh_editor_window::remove_collider(i32 index) {

        if (index < 0 || index >= static_cast<i32>(m_colliders.size()))
            return;

        m_colliders.erase(m_colliders.begin() + index);

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

        // ---------- camera input ----------
        const bool hovered = ImGui::IsItemHovered();
        const bool active_left = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
        const bool active_middle = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle);
        ImGuiIO& io = ImGui::GetIO();

        if (hovered && io.MouseWheel != 0.0f) {
            m_preview_camera.distance = GLT::math::clamp(
                m_preview_camera.distance * std::pow(0.9f, io.MouseWheel),
                MIN_CAM_DISTANCE, MAX_CAM_DISTANCE);
        }

        if (active_left) {
            m_preview_camera.yaw_rad   -= io.MouseDelta.x * 0.008f;
            m_preview_camera.pitch_rad = GLT::math::clamp(
                m_preview_camera.pitch_rad - io.MouseDelta.y * 0.008f, -1.45f, 1.45f);
        }

        if (active_middle) {
            const glm::vec3 fwd(
                std::cos(m_preview_camera.pitch_rad) * std::sin(m_preview_camera.yaw_rad),
                std::sin(m_preview_camera.pitch_rad),
                std::cos(m_preview_camera.pitch_rad) * std::cos(m_preview_camera.yaw_rad));
            const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
            const glm::vec3 up = glm::normalize(glm::cross(right, fwd));
            m_preview_camera.target += (-right * io.MouseDelta.x + up * io.MouseDelta.y)
                                     * (m_preview_camera.distance * 0.0025f);
        }

        // ---------- overlay buttons ----------
        ImGui::SetCursorScreenPos(ImVec2(canvas_min.x + 10.0f, canvas_max.y - 30.0f));
        ImGui::BeginGroup();
        if (ImGui::Button("Fit"))   reset_camera_to_fit();
        ImGui::SameLine();
        if (ImGui::Button("Front")) { m_preview_camera.yaw_rad = 0.0f; m_preview_camera.pitch_rad = 0.0f; }
        ImGui::SameLine();
        if (ImGui::Button("Top"))   { m_preview_camera.yaw_rad = 0.0f; m_preview_camera.pitch_rad = 1.4f; }
        ImGui::SameLine();
        if (ImGui::Button("Side"))  { m_preview_camera.yaw_rad = glm::half_pi<f32>(); m_preview_camera.pitch_rad = 0.0f; }
        ImGui::EndGroup();

        ImGui::SetCursorScreenPos(ImVec2(canvas_min.x + 10.0f, canvas_max.y - 54.0f));
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
        cam.position = position;
        cam.view = glm::lookAt(position, m_preview_camera.target, glm::vec3(0, 1, 0));
        cam.fov = m_preview_camera.fov_deg;
        cam.near_plane = std::max(0.01f, m_preview_camera.distance * 0.01f);
        cam.far_plane = m_preview_camera.distance * 10.0f + 1000.0f;
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
        m_preview_camera.distance = GLT::math::clamp(radius * 2.5f, MIN_CAM_DISTANCE, MAX_CAM_DISTANCE);
        m_preview_camera.yaw_rad = -0.42f;
        m_preview_camera.pitch_rad = -0.56f;
        m_preview_camera.fov_deg = 45.0f;
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

        // Keep inspector state consistent if the new mesh has fewer submeshes
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
            constexpr f32 GRID_EXT = GRID_HALF * GRID_STEP;
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
                    draw_capsule_wireframe(*debug, c.position, c.rotation, c.capsule_radius, c.capsule_half_height, color);
                    break;

                case collider_shape_kind::cylinder:
                    draw_cylinder_wireframe(*debug, c.position, c.rotation, c.cylinder_radius, c.cylinder_half_height, color);
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

}
