
// #include "util/pch.h"
// #include "mesh_editor.h"

// #include <imgui.h>
// #include <imgui_internal.h>

// #include <config/imgui_config.h>
// #include <asset/type.h>
// #include <asset/i_asset_registry.h>
// #include <plugin_system/plugin_manager.h>
// #include <render/i_renderer.h>
// #include <render/i_debug_draw.h>
// #include <util/util.h>

// #include "util/ui/pannel_collection.h"
// #include "util/ui/asset_picker.h"



// // FORWARD DECLARATIONS ================================================================================================

// namespace GLT::editor {

//     // CONSTANTS =======================================================================================================

//     constexpr f32                                   DETAILS_PANEL_WIDTH = 460.0f;

//     constexpr f32                                   MIN_PREVIEW_SIDE = 64.0f;

//     constexpr f32                                   MIN_CAM_DISTANCE = 0.05f;

//     constexpr f32                                   MAX_CAM_DISTANCE = 1500.0f;

//     constexpr u32                                   DEFAULT_PREVIEW_SIZE = 512;

//     constexpr const char*                           DRAG_PAYLOAD_ID = "CONTENT_BROWSER_ITEM";

//     static constexpr const char*                    kRenderModeLabels[] = {
//         "Lit", "Unlit", "Wireframe", "Wireframe Overlay", "Normals", "UVs"
//     };

//     // INTERNAL FUNCTION DECLARATION ===================================================================================

//     // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

//     // =================================================================================================================
//     // ctor / dtor
//     // =================================================================================================================

//     mesh_editor_window::mesh_editor_window() { open(std::filesystem::path{}); }


//     mesh_editor_window::mesh_editor_window(const std::filesystem::path& path) { open(path); }


//     mesh_editor_window::~mesh_editor_window() { close_mesh(); }

//     // =================================================================================================================
//     // public
//     // =================================================================================================================

//     void mesh_editor_window::open(const std::filesystem::path& path) {

//         close_mesh();

//         m_mesh_path = path;
//         m_mesh_name = path.empty() ? std::string("(no mesh)") : path.filename().replace_extension("").string();
//         m_load_failed = false;

//         cache_window_state();
//         make_window_name((std::string("MESH: ") + m_mesh_name).c_str());

//         if (path.empty()) {
//             m_show_window = true;
//             return;
//         }

//         std::error_code error{};
//         VALIDATE(GLT::vfs::exists(PROJECT_CONTENT_DIR / path, error) && !error,
//             m_load_failed = true; m_show_window = true; return,
//             "", "file not found [{}]", (path.generic_string()))

//         VALIDATE(!GLT::vfs::is_directory(PROJECT_CONTENT_DIR / path, error) && !error,
//             m_load_failed = true; m_show_window = true; return,
//             "", "path is a directory [{}]", (path.generic_string()))

//         auto registry = GLT::asset::registry::get_ref();
//         VALIDATE(registry, m_load_failed = true; m_show_window = true; return, "", "no asset registry")

//         auto loaded = registry->load(path);
//         VALIDATE(loaded, m_load_failed = true; m_show_window = true; return, "", "failed to load mesh [{}]", (path.generic_string()))

//         if (!load_from_handle(*loaded)) {
//             registry->unload(*loaded);
//             m_load_failed = true;
//         }

//         ensure_preview();
//         reset_camera_to_fit();

//         m_show_window = true;
//     }


//     void mesh_editor_window::close_mesh() {

//         destroy_preview();

//         if (m_asset_handle != INVALID_HANDLE) {
//             if (auto registry = GLT::asset::registry::get_ref())
//                 registry->unload(m_asset_handle);
//             m_asset_handle = INVALID_HANDLE;
//         }

//         m_has_mesh = false;
//         m_dirty = false;

//         m_submesh_bounds_cache.clear();

//         m_hovered_submesh = -1;
//         m_selected_submesh = -1;
//     }


//     void mesh_editor_window::window(const f32 /*delta_time*/) {

//         m_preview_image_drawn = false;
//         if (!m_show_window)
//             return;

//         apply_pending_dock();
//         ImGui::SetNextWindowSizeConstraints(ImVec2(880.0f, 520.0f), ImVec2(std::numeric_limits<f32>::max(),
//             std::numeric_limits<f32>::max()));

//         if (ImGui::Begin(m_window_id.c_str(), &m_show_window)) {

//             handle_drag_drop();
//             GLT::UI::custom_frame(DETAILS_PANEL_WIDTH, true,
//                 ImGui::GetColorU32(GLT::imgui_config::get_default_gray1_ref()),
//                 [this]() { draw_details_panel(); },
//                 [this]() { draw_preview_panel(); });
//         }
//         ImGui::End();
//     }


//     void mesh_editor_window::update(const f32 /*delta_time*/) {

//         if (m_preview_dirty)
//             push_preview_state();
        
//         auto* renderer = GLT::render::renderer::get_ptr();
//         if (!renderer)
//             return;

//         auto* preview = renderer->as<GLT::render::i_preview>();
//         if (!preview)
//             return;

//         preview->set_active(m_preview_id, m_preview_image_drawn);

//         // Re-submit every frame - lifetime == 0 means "this frame only", so the editor owns the cadence
//         submit_debug_draws();
//     }


//     bool mesh_editor_window::serialize(const std::filesystem::path& /*project_file*/, const GLT::serializer::option /*option*/) {
//         return false;
//     }


//     bool mesh_editor_window::save() {

//         if (!m_has_mesh || m_asset_handle == INVALID_HANDLE)
//             return false;

//         auto registry = GLT::asset::registry::get_ref();
//         if (!registry)
//             return false;

//         auto result = registry->save(m_asset_handle);
//         if (!result) {
//             LOG(error, "mesh_editor: save failed for [{}] (error {}). "
//                        "Meshes currently return [no_handler] from the handler.",
//                 m_mesh_name, static_cast<int>(result.error()));
//             return false;
//         }

//         m_dirty = false;
//         return true;
//     }

//     // =================================================================================================================
//     // private – panels
//     // =================================================================================================================

//     void mesh_editor_window::draw_details_panel() {

//         // header -----------------------------------------------------------------------------------------------------
//         ImGui::TextUnformatted(m_mesh_name.c_str());

//         ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
//         if (m_mesh_path.empty())
//             ImGui::TextUnformatted("(unsaved)");
//         else
//             ImGui::TextWrapped("%s", m_mesh_path.generic_string().c_str());
//         ImGui::PopStyleColor();

//         ImGui::Dummy(ImVec2(0.0f, 6.0f));

//         // actions ----------------------------------------------------------------------------------------------------
//         ImGui::BeginDisabled(!m_has_mesh);

//         if (ImGui::Button("Reload", ImVec2(100.0f, 0.0f)))
//             open(m_mesh_path);

//         ImGui::SameLine();
//         if (ImGui::Button("Save", ImVec2(100.0f, 0.0f)))
//             save();

//         ImGui::EndDisabled();

//         if (m_dirty) {
//             ImGui::SameLine();
//             ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 200, 90, 255));
//             ImGui::TextUnformatted("Unsaved changes");
//             ImGui::PopStyleColor();
//         }

//         // state ------------------------------------------------------------------------------------------------------
//         if (m_load_failed) {
//             ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 90, 90, 255));
//             ImGui::TextUnformatted("Failed to load mesh.");
//             ImGui::PopStyleColor();
//             return;
//         }

//         if (!m_has_mesh) {
//             ImGui::TextDisabled("No mesh loaded.");
//             return;
//         }

//         // scroll region ----------------------------------------------------------------------------------------------
//         if (ImGui::BeginChild("##mesh_details_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_None)) {

//             draw_summary_section();
//             draw_bounds_section();
//             draw_submesh_section();
//             draw_preview_settings_section();
//             draw_camera_section();
//         }
//         ImGui::EndChild();
//     }


//     void mesh_editor_window::draw_preview_panel() {

//         if (!m_has_mesh) {
//             ImGui::TextDisabled("Open a mesh asset to preview it.");
//             return;
//         }

//         auto renderer = GLT::render::renderer::get_ref();
//         if (!renderer) {
//             ImGui::TextDisabled("No renderer available.");
//             return;
//         }

//         auto* preview = renderer->as<GLT::render::i_preview>();
//         if (!preview) {
//             ImGui::TextDisabled("Active renderer does not implement i_preview.");
//             return;
//         }

//         if (m_preview_id == 0) {
//             ImGui::TextDisabled("Preview target unavailable.");
//             return;
//         }

//         // resize preview to the available area
//         const ImVec2 avail = ImGui::GetContentRegionAvail();
//         if (avail.x < MIN_PREVIEW_SIDE || avail.y < MIN_PREVIEW_SIDE)
//             return;

//         const glm::uvec2 new_size(static_cast<u32>(avail.x), static_cast<u32>(avail.y));
//         if (new_size != m_last_preview_size) {
//             preview->set_size(m_preview_id, new_size);
//             m_last_preview_size = new_size;
//         }

//         const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
//         const ImVec2 canvas_max(canvas_min.x + avail.x, canvas_min.y + avail.y);
//         void* tex = preview->get_render_target(m_preview_id);

//         ImGui::InvisibleButton("##mesh_preview_canvas", avail,
//             ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

//         if (tex) {

//             ImDrawList* draw = ImGui::GetWindowDrawList();
//             draw->AddImage(reinterpret_cast<ImTextureID>(tex), canvas_min, canvas_max, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
//             m_preview_image_drawn = ImGui::IsItemVisible();
//             draw->AddRect(canvas_min, canvas_max, IM_COL32(60, 60, 70, 255), 0.0f, 0, 1.0f);

//         } else {

//             ImGui::GetWindowDrawList()->AddRectFilled(canvas_min, canvas_max, IM_COL32(24, 24, 28, 255));
//             const char* msg = "Preview not yet rendered";
//             const ImVec2 sz = ImGui::CalcTextSize(msg);
//             ImGui::GetWindowDrawList()->AddText(
//                 ImVec2((canvas_min.x + canvas_max.x - sz.x) * 0.5f,
//                        (canvas_min.y + canvas_max.y - sz.y) * 0.5f),
//                 IM_COL32(150, 150, 160, 220), msg);
//         }

//         // ---- camera input ------------------------------------------------------------------------------------------
//         const bool hovered = ImGui::IsItemHovered();
//         const bool active_left = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
//         const bool active_middle = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle);

//         ImGuiIO& io = ImGui::GetIO();

//         if (hovered && io.MouseWheel != 0.0f) {

//             const f32 factor = std::pow(0.9f, io.MouseWheel);
//             m_preview_camera.distance = std::clamp(m_preview_camera.distance * factor, MIN_CAM_DISTANCE, MAX_CAM_DISTANCE);
//             m_preview_dirty = true;
//         }

//         if (active_left) {

//             m_preview_camera.yaw_rad += io.MouseDelta.x * 0.008f;
//             m_preview_camera.pitch_rad = std::clamp(m_preview_camera.pitch_rad + io.MouseDelta.y * 0.008f, -1.45f, 1.45f);
//             m_preview_dirty = true;
//         }

//         if (active_middle) {

//             // pan target in camera-right/up plane
//             const glm::vec3 fwd = glm::vec3(
//                 std::cos(m_preview_camera.pitch_rad) * std::sin(m_preview_camera.yaw_rad),
//                 std::sin(m_preview_camera.pitch_rad),
//                 std::cos(m_preview_camera.pitch_rad) * std::cos(m_preview_camera.yaw_rad));

//             const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
//             const glm::vec3 up = glm::normalize(glm::cross(right, fwd));

//             const f32 pan_speed = m_preview_camera.distance * 0.0025f;
//             m_preview_camera.target += (-right * io.MouseDelta.x + up * io.MouseDelta.y) * pan_speed;
//             m_preview_dirty = true;
//         }

//         // ---- overlay controls at the bottom of the preview ---------------------------------------------------------
//         ImGui::SetCursorScreenPos(ImVec2(canvas_min.x + 10.0f, canvas_max.y - 30.0f));
//         ImGui::BeginGroup();

//         if (ImGui::Button("Fit"))
//             reset_camera_to_fit();

//         ImGui::SameLine();
//         if (ImGui::Button("Front")) {
//             m_preview_camera.yaw_rad = 0.0f;
//             m_preview_camera.pitch_rad = 0.0f;
//             m_preview_dirty = true;
//         }
//         ImGui::SameLine();
//         if (ImGui::Button("Top")) {
//             m_preview_camera.yaw_rad = 0.0f;
//             m_preview_camera.pitch_rad = 1.4f;
//             m_preview_dirty = true;
//         }
//         ImGui::SameLine();
//         if (ImGui::Button("Side")) {
//             m_preview_camera.yaw_rad = glm::half_pi<f32>();
//             m_preview_camera.pitch_rad = 0.0f;
//             m_preview_dirty = true;
//         }

//         ImGui::EndGroup();

//         ImGui::SetCursorScreenPos(ImVec2(canvas_min.x + 10.0f, canvas_max.y - 54.0f));
//         ImGui::TextDisabled("LMB orbit   MMB pan   wheel zoom");
//     }

//     // =================================================================================================================
//     // private – sections
//     // =================================================================================================================

//     void mesh_editor_window::draw_summary_section() {

//         if (!ImGui::CollapsingHeader("Summary", ImGuiTreeNodeFlags_DefaultOpen))
//             return;

//         auto registry = GLT::asset::registry::get_ref();
//         if (!registry)
//             return;

//         auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle);
//         if (!mesh)
//             return;

//         if (!GLT::UI::begin_table("mesh_summary_table", false, ImVec2(0, 0), 0.0f, true, 0.42f))
//             return;

//         GLT::UI::table_row("vertices", std::to_string(mesh->vertices.size()));
//         GLT::UI::table_row("indices",  std::to_string(mesh->indices.size()));
//         GLT::UI::table_row("triangles", std::to_string(mesh->indices.size() / 3));
//         GLT::UI::table_row("submeshes", std::to_string(mesh->submesh_count()));
//         GLT::UI::table_row("memory",   GLT::util::format_bytes(mesh->memory_usage()));

//         GLT::UI::end_table();
//     }


//     void mesh_editor_window::draw_bounds_section() {

//         if (!ImGui::CollapsingHeader("Bounds"))
//             return;

//         auto registry = GLT::asset::registry::get_ref();
//         if (!registry)
//             return;

//         auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle);
//         if (!mesh)
//             return;

//         glm::vec3 mn, mx;
//         compute_mesh_bounds(*mesh, mn, mx);
//         const glm::vec3 size = mx - mn;

//         if (!GLT::UI::begin_table("mesh_bounds_table", false, ImVec2(0, 0), 0.0f, true, 0.42f))
//             return;

//         char buf[128];

//         std::snprintf(buf, sizeof(buf), "%.3f, %.3f, %.3f", mn.x, mn.y, mn.z);
//         GLT::UI::table_row("min", std::string_view{ buf });

//         std::snprintf(buf, sizeof(buf), "%.3f, %.3f, %.3f", mx.x, mx.y, mx.z);
//         GLT::UI::table_row("max", std::string_view{ buf });

//         std::snprintf(buf, sizeof(buf), "%.3f, %.3f, %.3f", size.x, size.y, size.z);
//         GLT::UI::table_row("size", std::string_view{ buf });

//         GLT::UI::end_table();
//     }


//     void mesh_editor_window::draw_submesh_section() {

//         if (!ImGui::CollapsingHeader("Submeshes", ImGuiTreeNodeFlags_DefaultOpen))
//             return;

//         auto registry = GLT::asset::registry::get_ref();
//         if (!registry)
//             return;

//         auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle);
//         if (!mesh)
//             return;

//         if (mesh->submeshes.empty()) {
//             ImGui::TextDisabled("This mesh has no submeshes.");
//             return;
//         }

//         // Hover any row -> highlight that submesh in the preview. Selected state is a pinned override.
//         i32 new_hover = -1;

//         constexpr ImGuiTableFlags kTableFlags =
//             ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;

//         if (ImGui::BeginTable("mesh_submesh_table", 3, kTableFlags)) {

//             ImGui::TableSetupColumn("##idx",    ImGuiTableColumnFlags_WidthFixed,   36.0f);
//             ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_WidthStretch, 1.2f);
//             ImGui::TableSetupColumn("Info",     ImGuiTableColumnFlags_WidthStretch, 1.6f);
//             ImGui::TableHeadersRow();

//             for (u32 i = 0; i < mesh->submesh_count(); ++i) {

//                 auto& sm = mesh->submeshes[i];

//                 ImGui::TableNextRow();
//                 ImGui::PushID(static_cast<int>(i));

//                 // ---- col 0: selectable label - spans the whole row for a clean hover target -----------------------
//                 ImGui::TableSetColumnIndex(0);

//                 const bool selected = (m_selected_submesh == static_cast<i32>(i));

//                 char label_buf[16];
//                 std::snprintf(label_buf, sizeof(label_buf), "%u", i);

//                 if (ImGui::Selectable(label_buf, selected,
//                         ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
//                         ImVec2(0, 0))) {

//                     m_selected_submesh = selected ? -1 : static_cast<i32>(i);
//                     update_highlight();
//                 }

//                 // AllowOverlap means the picker below still receives clicks; IsItemHovered() still reports the
//                 // whole-row region because the picker hasn't been submitted at this point in the frame.
//                 if (ImGui::IsItemHovered())
//                     new_hover = static_cast<i32>(i);

//                 // ---- col 1: material picker -----------------------------------------------------------------------
//                 ImGui::TableSetColumnIndex(1);

//                 const GLT::asset::handle old_handle = sm.material;
//                 if (GLT::UI::asset_picker_widget(
//                         { .label = "##submesh_mat", .filter = GLT::asset::core_types::material,
//                         .allow_clear = true, .placeholder = "(none)" },
//                         sm.material, ImGui::GetContentRegionAvail().x)) {

//                     // Preview-only - the mesh handler doesn't override serialize(), so this never hits disk.
//                     if (old_handle != INVALID_HANDLE && old_handle != sm.material)
//                         registry->release(old_handle);
//                     if (sm.material != INVALID_HANDLE)
//                         registry->add_dependency(m_asset_handle, sm.material);

//                     m_dirty = true;
//                 }

//                 // ---- col 2: index range + per-submesh AABB ----------------------------------------------------------
//                 ImGui::TableSetColumnIndex(2);

//                 if (i < m_submesh_bounds_cache.size()) {

//                     const auto& b = m_submesh_bounds_cache[i];
//                     const glm::vec3 size = b.max - b.min;

//                     ImGui::Text("idx %u..%u", sm.first_index, sm.first_index + sm.index_count);
//                     ImGui::TextDisabled("AABB %.2f, %.2f, %.2f", size.x, size.y, size.z);
//                 }

//                 ImGui::PopID();
//             }

//             ImGui::EndTable();
//         }

//         if (new_hover != m_hovered_submesh) {
//             m_hovered_submesh = new_hover;
//             update_highlight();
//         }

//         ImGui::Dummy(ImVec2(0.0f, 4.0f));
//         ImGui::TextDisabled("Hover a row to highlight it in the preview. Click to select.");
//         if (m_dirty) {
//             ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 200, 90, 255));
//             ImGui::TextWrapped("Material changes are preview-only - mesh serialization is not yet supported.");
//             ImGui::PopStyleColor();
//         }
//     }


//     void mesh_editor_window::draw_preview_settings_section() {

//         if (!ImGui::CollapsingHeader("Preview", ImGuiTreeNodeFlags_DefaultOpen))
//             return;

//         // ---- render mode -------------------------------------------------------------------------------------------
//         i32 mode = static_cast<i32>(m_preview_settings.render_mode);
//         ImGui::SetNextItemWidth(-1.0f);
//         if (ImGui::Combo("##render_mode", &mode, kRenderModeLabels, IM_ARRAYSIZE(kRenderModeLabels))) {
//             m_preview_settings.render_mode = static_cast<GLT::render::preview_render_mode>(mode);
//             m_preview_dirty = true;
//         }

//         // ---- background --------------------------------------------------------------------------------------------
//         if (ImGui::ColorEdit4("Background", &m_preview_settings.background.x,
//                 ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar))
//             m_preview_dirty = true;

//         // ---- overlays ----------------------------------------------------------------------------------------------
//         if (ImGui::Checkbox("Grid", &m_preview_settings.show_grid))
//             m_preview_dirty = true;

//         ImGui::SameLine();
//         if (ImGui::Checkbox("Origin", &m_preview_settings.show_origin))
//             m_preview_dirty = true;

//         // ---- auto-rotate -------------------------------------------------------------------------------------------
//         if (ImGui::Checkbox("Auto-rotate", &m_auto_rotate))
//             m_preview_dirty = true;

//         if (m_auto_rotate) {
//             ImGui::SetNextItemWidth(-1.0f);
//             if (ImGui::SliderFloat("##auto_rotate_speed", &m_auto_rotate_speed, 0.01f, 100.f, "%.2f %"))
//                 m_preview_dirty = true;
//         }
//     }


//     void mesh_editor_window::draw_camera_section() {

//         if (!ImGui::CollapsingHeader("Camera"))
//             return;

//         if (ImGui::Button("Reset to fit", ImVec2(-1.0f, 0.0f)))
//             reset_camera_to_fit();

//         // distance
//         if (ImGui::SliderFloat("Distance", &m_preview_camera.distance, MIN_CAM_DISTANCE, MAX_CAM_DISTANCE, "%.2f"))
//             m_preview_dirty = true;

//         // fov
//         if (ImGui::SliderFloat("FOV", &m_preview_camera.fov_deg, 15.0f, 110.0f, "%.0f deg"))
//             m_preview_dirty = true;

//         // target
//         if (ImGui::DragFloat3("Target", &m_preview_camera.target.x, 0.01f))
//             m_preview_dirty = true;

//         // angles
//         if (ImGui::DragFloat("Yaw",   &m_preview_camera.yaw_rad,   0.01f))
//             m_preview_dirty = true;
//         if (ImGui::DragFloat("Pitch", &m_preview_camera.pitch_rad, 0.01f))
//             m_preview_dirty = true;
//     }

//     // =================================================================================================================
//     // private – preview plumbing
//     // =================================================================================================================

//     void mesh_editor_window::ensure_preview() {

//         if (m_preview_id != 0 || !m_has_mesh)
//             return;

//         auto renderer = GLT::render::renderer::get_ref();
//         if (!renderer)
//             return;

//         auto* preview = renderer->as<GLT::render::i_preview>();
//         if (!preview)
//             return;

//         GLT::render::preview_desc desc;
//         desc.source = GLT::render::preview_source::mesh_asset;
//         desc.mesh = m_asset_handle;
//         desc.material = INVALID_HANDLE;             // use the mesh's own per-submesh materials

//         m_preview_id = preview->create(desc, glm::uvec2(DEFAULT_PREVIEW_SIZE, DEFAULT_PREVIEW_SIZE));
//         if (m_preview_id == 0)
//             return;

//         m_last_preview_size = { DEFAULT_PREVIEW_SIZE, DEFAULT_PREVIEW_SIZE };
//         push_preview_state();
//         update_highlight();
//     }


//     void mesh_editor_window::destroy_preview() {

//         if (m_preview_id == 0)
//             return;

//         if (auto renderer = GLT::render::renderer::get_ref()) {
//             if (auto* preview = renderer->as<GLT::render::i_preview>())
//                 preview->destroy(m_preview_id);
//         }

//         m_preview_id = 0;
//         m_last_preview_size = { 0, 0 };
//     }


//     void mesh_editor_window::push_preview_state() {

//         m_preview_dirty = false;

//         if (m_preview_id == 0)
//             return;

//         auto renderer = GLT::render::renderer::get_ref();
//         if (!renderer)
//             return;

//         auto* preview = renderer->as<GLT::render::i_preview>();
//         if (!preview)
//             return;

//         preview->set_settings(m_preview_id, m_preview_settings);
//         preview->set_camera(m_preview_id, m_preview_camera);
//         preview->set_auto_rotate(m_preview_id, m_auto_rotate, m_auto_rotate_speed / 1000);
//     }


//     void mesh_editor_window::update_highlight() {

//         if (m_preview_id == 0)
//             return;

//         auto renderer = GLT::render::renderer::get_ref();
//         if (!renderer)
//             return;

//         auto* preview = renderer->as<GLT::render::i_preview>();
//         if (!preview)
//             return;

//         std::array<u32, 2> ids{};
//         u32 count = 0;

//         if (m_hovered_submesh >= 0 && m_hovered_submesh != m_selected_submesh)
//             ids[count++] = static_cast<u32>(m_hovered_submesh);

//         if (m_selected_submesh >= 0)
//             ids[count++] = static_cast<u32>(m_selected_submesh);

//         preview->set_highlight_submeshes(m_preview_id, std::span<const u32>(ids.data(), count));
//     }


//     void mesh_editor_window::reset_camera_to_fit() {

//         auto registry = GLT::asset::registry::get_ref();
//         if (!registry)
//             return;

//         auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle);
//         if (!mesh)
//             return;

//         glm::vec3 mn, mx;
//         compute_mesh_bounds(*mesh, mn, mx);

//         m_preview_camera.target = 0.5f * (mn + mx);

//         const glm::vec3 extent = mx - mn;
//         const f32 radius = 0.5f * glm::length(extent);
//         m_preview_camera.distance = std::clamp(radius * 2.5f, MIN_CAM_DISTANCE, MAX_CAM_DISTANCE);
//         m_preview_camera.yaw_rad = 0.7f;
//         m_preview_camera.pitch_rad = 0.35f;
//         m_preview_camera.fov_deg = 45.0f;

//         m_preview_dirty = true;
//     }

//     // =================================================================================================================
//     // private – helpers
//     // =================================================================================================================

//     void mesh_editor_window::compute_mesh_bounds(const GLT::asset::mesh::mesh_asset& mesh,
//         glm::vec3& out_min, glm::vec3& out_max) const {

//         out_min = glm::vec3( std::numeric_limits<f32>::max());
//         out_max = glm::vec3(-std::numeric_limits<f32>::max());

//         if (mesh.vertices.empty())
//             return;

//         for (const auto& v : mesh.vertices) {
//             out_min = glm::min(out_min, v.position);
//             out_max = glm::max(out_max, v.position);
//         }
//     }


//     void mesh_editor_window::rebuild_submesh_bounds_cache(const GLT::asset::mesh::mesh_asset& mesh) {

//         m_submesh_bounds_cache.clear();
//         m_submesh_bounds_cache.resize(mesh.submeshes.size());

//         for (std::size_t i = 0; i < mesh.submeshes.size(); ++i) {

//             const auto& sm = mesh.submeshes[i];
//             auto& b = m_submesh_bounds_cache[i];

//             b.min = glm::vec3( std::numeric_limits<f32>::max());
//             b.max = glm::vec3(-std::numeric_limits<f32>::max());

//             bool any = false;
//             for (u32 k = 0; k < sm.index_count; ++k) {

//                 const u32 idx = mesh.indices[sm.first_index + k];
//                 if (idx >= mesh.vertices.size())
//                     continue;

//                 b.min = glm::min(b.min, mesh.vertices[idx].position);
//                 b.max = glm::max(b.max, mesh.vertices[idx].position);
//                 any = true;
//             }

//             if (!any) {
//                 b.min = glm::vec3(0.0f);
//                 b.max = glm::vec3(0.0f);
//             }
//         }
//     }


//     bool mesh_editor_window::load_from_handle(GLT::asset::handle h) {

//         auto registry = GLT::asset::registry::get_ref();
//         if (!registry)
//             return false;

//         auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(h);
//         if (!mesh)
//             return false;

//         m_asset_handle = h;
//         m_has_mesh = true;
//         m_load_failed = false;
//         m_dirty = false;

//         m_hovered_submesh = -1;
//         m_selected_submesh = -1;

//         rebuild_submesh_bounds_cache(*mesh);

//         return true;
//     }


//     void mesh_editor_window::handle_drag_drop() {

//         const ImVec2 min = ImGui::GetWindowPos();
//         const ImVec2 max = ImVec2(min.x + ImGui::GetWindowSize().x, min.y + ImGui::GetWindowSize().y);

//         if (!ImGui::BeginDragDropTargetCustom(ImRect(min, max), ImGui::GetID("##mesh_drop_target")))
//             return;

//         if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DRAG_PAYLOAD_ID)) {

//             const char* raw = static_cast<const char*>(payload->Data);
//             if (raw && raw[0] != '\0') {

//                 const std::filesystem::path dropped(raw);
//                 const std::string ext = GLT::util::to_lower(dropped.extension().string());
//                 // Accept any of the mesh-ish extensions the factory normally emits for a mesh asset.
//                 if (ext == ".glt_mesh" || ext == ".gltf" || ext == ".glb" || ext == ".obj")
//                     open(dropped);
//             }
//         }

//         ImGui::EndDragDropTarget();
//     }


//     void mesh_editor_window::submit_debug_draws() {

//         if (m_preview_id == 0 || !m_has_mesh || !m_preview_image_drawn)
//             return;

//         auto renderer = GLT::render::renderer::get_ref();
//         if (!renderer)
//             return;

//         auto* preview = renderer->as<GLT::render::i_preview>();
//         if (!preview)
//             return;

//         auto* debug = preview->get_debug_draw(m_preview_id);
//         if (!debug)
//             return;

//         auto registry = GLT::asset::registry::get_ref();
//         if (!registry)
//             return;

//         auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle);
//         if (!mesh)
//             return;

//         // ---- whole-mesh AABB - always visible ----
//         glm::vec3 mesh_min, mesh_max;
//         compute_mesh_bounds(*mesh, mesh_min, mesh_max);

//         // Empty meshes come back as (f32::max, -f32::max); skip them
//         if (mesh_min.x > mesh_max.x || mesh_min.y > mesh_max.y || mesh_min.z > mesh_max.z)
//             return;

//         // Preview renders the mesh at identity, so mesh-local == world - no transform needed
//         debug->aabb(mesh_min, mesh_max,
//             glm::vec4(0.70f, 0.75f, 0.85f, 0.70f), 0.f, false);

//         // ---- hovered-submesh AABB - only while the row is hovered ----
//         if (m_hovered_submesh >= 0 && static_cast<std::size_t>(m_hovered_submesh) < m_submesh_bounds_cache.size()) {

//             const auto& b = m_submesh_bounds_cache[m_hovered_submesh];

//             // Skip degenerate entries (submesh with no valid indices)
//             if (b.min.x <= b.max.x && b.min.y <= b.max.y && b.min.z <= b.max.z)
//                 debug->aabb(b.min, b.max, glm::vec4(1.0f, 0.85f, 0.20f, 1.0f), 0.f, false);
//         }
//     }

// }
