
#include "util/pch.h"
#include "world_viewport.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <config/imgui_config.h>
#include <plugin_system/plugin_manager.h>
#include <platform/i_window.h>
#include <asset/i_asset_registry.h>
#include <render/image.h>
#include <render/i_renderer.h>
#include <render/i_debug_draw.h>
#include <world/i_world.h>
#include <world/i_world_scene.h>
#include <world/world_layer.h>

#include "util/ui/pannel_collection.h"
#include "resource_manager/icon_manager.h"
#include "window/content_browser.h"
#include "util/context.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

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

    world_viewport_window::world_viewport_window() {
        
        make_window_name("World Viewport");
        m_renderer = GLT::plugin_manager::get_plugin_ref<GLT::render::i_renderer_plugin>(GLT::plugin_manager::interface::renderer);
        m_content_browsers.push_back(GLT::editor::content_browser_window());       // create first content browser

        m_save_sub_handle = GLT::event_bus::subscribe<GLT::save_event>(std::bind_front(&world_viewport_window::on_save_event, this), 10);
        m_save_as_sub_handle = GLT::event_bus::subscribe<save_as_request_event>(std::bind_front(&world_viewport_window::on_save_as_request_event, this));
    }


    world_viewport_window::~world_viewport_window() {

        GLT::event_bus::unsubscribe(m_save_as_sub_handle);
        GLT::event_bus::unsubscribe(m_save_sub_handle);
        m_content_browsers.clear();
        m_renderer.reset();
    }

    // CLASS PUBLIC ====================================================================================================

    void world_viewport_window::window(const f32 delta_time) {

        if (!m_show_window) {

            if (m_renderer)
                m_renderer->set_world_render_enabled(false);
            return;
        }

        apply_pending_dock();

        // A regular, dockable window. Its body hosts a nested dockspace for
        // the Viewport / Details / Tools sub-panels. The outer world_viewport_window
        // docks this window by its full ImGui ID (m_window_id).
        ImGui::SetNextWindowSizeConstraints(ImVec2(600, 400), ImVec2(std::numeric_limits<f32>::max(), std::numeric_limits<f32>::max()));
        ImGui::Begin(m_window_id.c_str(), &m_show_window);
        {
            render_inner_dockspace();
        }
        ImGui::End();

        // Sub-panels are top-level windows. DockBuilder assigns them to the
        // inner dockspace's nodes the first time the inner layout is built.
        render_viewport();
        render_details();
        render_outliner();

        if (m_renderer)
            m_renderer->set_world_render_enabled(m_viewport_image_drawn);

        for (auto& browsers : m_content_browsers)
            if (!browsers.should_close())
                browsers.window(delta_time);

        render_save_as_popup();                 // after every other window
    }


    void world_viewport_window::update(const f32 /*delta_time*/) {

        m_renderer->set_render_size({m_viewport_size.x, m_viewport_size.y});
        update_selection_debug_draw();

        // Move any events that arrived during the previous frame into the queue.
        for (auto& ev : m_save_as_event_buffer) {

            pending_save_request pending{};
            pending.req = ev.get();

            std::snprintf(pending.filename, sizeof(pending.filename), "%s", pending.req.default_name.c_str());

            // Picker and registry both speak project-relative - convert here so the modal opens with a path the picker can highlight in its tree
            pending.directory = PROJECT_CONTENT_DIR;
            if (pending.directory.empty())
                pending.directory = pending.req.default_dir;        // fallback: picker still accepts absolute

            m_save_as_queue.push_back(std::move(pending));
        }
        m_save_as_event_buffer.clear();
    }


    bool world_viewport_window::serialize(const std::filesystem::path& /*project_file*/, const GLT::serializer::option /*option*/) { return false; }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void world_viewport_window::render_inner_dockspace() {

        ImGuiID inner_id = ImGui::GetID("##world_viewport_dockspace");
        const ImVec2  inner_size = ImGui::GetContentRegionAvail();

        const bool no_layout_yet = (ImGui::DockBuilderGetNode(inner_id) == nullptr);
        if (m_reset_layout || no_layout_yet) {

            m_reset_layout = false;
            build_default_layout(inner_id, inner_size);
        }

        ImGui::DockSpace(inner_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    }


    void world_viewport_window::build_default_layout(ImGuiID dockspace_id, const ImVec2& size) {

        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, size);

        ImGuiID dock_main = dockspace_id;
        ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.25f, nullptr, &dock_main);
        ImGuiID dock_right_b = ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.65f, nullptr, &dock_right);
        ImGuiID dock_bottom = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.30f, nullptr, &dock_main);

        // Names must match exactly what the sub-windows pass to ImGui::Begin().
        ImGui::DockBuilderDockWindow("Viewport", dock_main);
        ImGui::DockBuilderDockWindow("Outliner", dock_right);
        ImGui::DockBuilderDockWindow("Details",  dock_right_b);

        if (m_content_browsers.size() > 0)
            ImGui::DockBuilderDockWindow(m_content_browsers[0].get_window_id().c_str(), dock_bottom);

        ImGui::DockBuilderFinish(dockspace_id);
    }


    void world_viewport_window::render_viewport() {

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ImageRounding, 0.0f);

        m_viewport_image_drawn = false;
        if (ImGui::Begin("Viewport")) {

            m_viewport_size = ImGui::GetContentRegionAvail();
            ImGui::Image(m_renderer->get_rendered_image(), m_viewport_size);
            m_viewport_image_drawn = ImGui::IsItemVisible();

            const bool image_hovered = ImGui::IsItemHovered();
            const ImVec2 image_min = ImGui::GetItemRectMin();
            const ImVec2 image_size = ImGui::GetItemRectSize();

            // These two overlays rely on GetItemRectMin() still being the image,
            // so they MUST run before anything else draws.
            const bool mode_popup_open = render_mode_selector_overlay();
            const bool settings_popup_open = render_settings_overlay();
            const bool overlay_active = mode_popup_open || settings_popup_open;

            // Gizmo is self-contained: it takes the rect explicitly, so it can run
            // after the overlays without disturbing their layout state.
            render_gizmo_toolbar(image_min, image_size);
            render_gizmo(image_min, image_size);

            const bool right_down = ImGui::IsMouseDown(ImGuiMouseButton_Right);

            if (!m_cursor_captured && image_hovered && !overlay_active && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                set_cursor_captured(true);
                GLT::editor::context::get().set_viewport_interacted(true);
            }

            if (m_cursor_captured && !right_down) {
                set_cursor_captured(false);
                GLT::editor::context::get().set_viewport_interacted(false);
            }
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
    }


    bool world_viewport_window::render_mode_selector_overlay() {

        if (!m_renderer)
            return false;

        // position the button in the top-left of the viewport image
        const ImVec2 image_min = ImGui::GetItemRectMin();
        constexpr f32 INSET = 8.f;
        ImGui::SetCursorScreenPos(ImVec2(image_min.x + INSET, image_min.y + INSET));

        // resolve the current mode's label
        const auto modes = m_renderer->supported_modes();
        const u32  current = m_renderer->get_render_mode();

        const char* current_label = "Render mode";
        for (const auto& m : modes)
            if (m.id == current) {
                current_label = m.display_name;
                break;
            }

        // the button 
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.f, 5.f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.10f, 0.10f, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.16f, 0.16f, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.20f, 0.20f, 0.90f));

        if (ImGui::Button(current_label))
            ImGui::OpenPopup("##render_mode_popup");

        // Capture the button's screen-space rect before popping styles
        const ImVec2 btn_min = ImGui::GetItemRectMin();
        const ImVec2 btn_max = ImGui::GetItemRectMax();

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);

        // the popup
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 6.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 2.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.f, 2.f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.08f, 0.08f, 0.08f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.20f, 0.20f, 0.70f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.30f, 0.30f, 0.30f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.35f, 0.35f, 0.35f, 0.95f));

        // Top-left of the popup = bottom-left of the button, 4px below. ImGuiCond_Appearing fires on the frame the popup opens
        // after that the popup keeps its position until it closes
        ImGui::SetNextWindowPos(ImVec2(btn_min.x, btn_max.y + 4.f), ImGuiCond_Appearing);

        bool popup_open = false;
        if (ImGui::BeginPopup("##render_mode_popup",
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {

            popup_open = true;

            const char* last_category = nullptr;
            for (const auto& m : modes) {

                // Category separator when it changes.
                if (m.has_category() && (last_category == nullptr || std::strcmp(last_category, m.category) != 0)) {

                    last_category = m.category;
                    ImGui::SeparatorText(m.category);
                }

                ImGui::PushID(static_cast<int>(m.id));

                const bool selected = (m.id == current);
                if (ImGui::Selectable(m.display_name, selected)) {
                    m_renderer->set_render_mode(m.id);
                    ImGui::CloseCurrentPopup();     // standard dropdown UX
                }

                // Immediate tooltip on hover
                if (m.has_tooltip() && ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", m.tooltip);

                ImGui::PopID();
            }

            ImGui::EndPopup();
        }

        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(3);

        return popup_open;
    }


    void world_viewport_window::render_details() {

        if (!ImGui::Begin("Details")) {
            ImGui::End();
            return;
        }

        if (!m_world || !m_inspector) {
            ImGui::TextDisabled("Inspector unavailable");
            ImGui::End();
            return;
        }

        const auto id = m_selected_entity;
        if (!id.is_valid() || !m_world->alive(id)) {
            ImGui::TextDisabled("No entity selected");
            ImGui::End();
            return;
        }

        // ---- header: name + entity id ----
        {
            const auto name = m_world->entity_name(id);
            ImGui::Text("Entity [%u]", id.index);
            if (!name.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("— %.*s", static_cast<int>(name.size()), name.data());
            }
        }
        ImGui::Separator();

        // ---- component list ----
        for (const auto* comp : m_inspector->components_on(id)) {

            ImGui::PushID(static_cast<int>(comp->hash));

            // One collapsing header per component. Default-open.
            const bool open = ImGui::CollapsingHeader(comp->name.data(),
                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

            // "..." button on the header row.
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
            if (ImGui::SmallButton("..."))
                ImGui::OpenPopup("##comp_ctx");

            if (ImGui::BeginPopup("##comp_ctx")) {

                if (ImGui::MenuItem("Remove", nullptr, false, !comp->can_remove || comp->can_remove(id)))
                    m_inspector->remove_component(id, comp->hash);

                if (ImGui::MenuItem("Copy"))
                    // Stash the descriptor hash; paste applies on the target.
                    m_clipboard_component = comp->hash;

                ImGui::EndPopup();
            }

            if (open) {

                ImGui::Indent(8.f);
                if (comp->draw)
                    comp->draw(id);
                else
                    ImGui::TextDisabled("(runtime-only component)");
                ImGui::Unindent(8.f);
                ImGui::Spacing();
            }

            ImGui::PopID();
        }

        // ---- Add Component (bottom of panel) ----
        ImGui::Separator();
        if (ImGui::Button("Add Component", ImVec2(-1, 0)))
            ImGui::OpenPopup("##add_comp");

        if (ImGui::BeginPopup("##add_comp")) {

            // Optional filter box - the popup gets long fast.
            static char filter[64] = "";
            ImGui::InputTextWithHint("##filter", "Search...", filter, sizeof(filter));
            ImGui::Separator();

            const std::string_view needle = filter;
            auto matches = [needle](const GLT::world::component_descriptor& d) {
                return needle.empty() || d.name.find(needle) != std::string_view::npos;
            };

            for (const auto& d : m_inspector->descriptors()) {

                if (!matches(d))
                    continue;

                if (d.has(id))
                    continue;                       // already present

                if (ImGui::Selectable(d.name.data())) {
                    m_inspector->add_component(id, d.hash);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered() && !d.category.empty()) {
                    ImGui::BeginTooltip();
                    ImGui::TextDisabled("%.*s",
                        static_cast<int>(d.category.size()), d.category.data());
                    ImGui::EndTooltip();
                }
            }

            // Paste row, if the clipboard holds a component.
            if (m_clipboard_component != 0) {
                ImGui::Separator();
                const auto* d = m_inspector->descriptors().empty()
                    ? nullptr
                    : [&]() -> const GLT::world::component_descriptor* {
                        for (const auto& e : m_inspector->descriptors())
                            if (e.hash == m_clipboard_component)
                                return &e;
                        return nullptr;
                    }();

                if (d && !d->has(id))
                    if (ImGui::Selectable(std::string("Paste: " + std::string(d->name)).c_str())) {
                        m_inspector->add_component(id, m_clipboard_component);
                        ImGui::CloseCurrentPopup();
                    }
            }

            ImGui::EndPopup();
        }

        ImGui::End();
    }


    void world_viewport_window::render_outliner() {

        if (!ImGui::Begin("Outliner")) {
            ImGui::End();
            return;
        }

        if (!m_world)
            m_world = GLT::world::manager::get_ref();

        if (!m_world) {
            ImGui::TextDisabled("No world plugin loaded");
            ImGui::End();
            return;
        }

        // Bind the inspector once. If the world plugin doesn't offer one, add/remove/edit menus still work but show no component UI.
        if (!m_inspector)
            m_inspector = m_world->as<GLT::world::i_world_inspector>();

        // ---- toolbar ----
        if (ImGui::Button("+ Entity"))
            ImGui::OpenPopup("add_entity_root");

        if (ImGui::BeginPopup("add_entity_root")) {
            if (ImGui::MenuItem("Empty Entity")) {
                const auto id = m_world->spawn();
                m_selected_entity = id;
            }
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        ImGui::TextDisabled("| %s", m_inspector ? "inspector ready" : "no inspector");

        ImGui::Separator();

        // ---- tree ----
        for (auto root : m_world->root_entities())
            render_outliner_node(root);

        // ---- blank-click deselect ----
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)
            && !ImGui::IsAnyItemHovered()
            && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            m_selected_entity = GLT::world::INVALID_ENTITY;

        ImGui::End();
    }


    void world_viewport_window::render_outliner_node(GLT::world::entity_id id) {

        ImGui::PushID(static_cast<int>(id.index));

        const std::string_view name = m_world->entity_name(id);
        char label[160];
        std::snprintf(label, sizeof(label), "%s",
            name.empty()
                ? ("Entity " + std::to_string(id.index)).c_str()
                : std::string(name.substr(0, 128)).c_str());

        const bool has_kids = m_world->has_children(id);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (!has_kids)
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (m_selected_entity == id)
            flags |= ImGuiTreeNodeFlags_Selected;

        const bool open = ImGui::TreeNodeEx("##node", flags, "%s", label);

        // ---- selection ----
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            m_selected_entity = id;

        // ---- drag & drop (reparent) ----
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("GLT_ENTITY", &id, sizeof(id));
            ImGui::TextUnformatted(label);
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("GLT_ENTITY")) {
                const auto dragged = *static_cast<const GLT::world::entity_id*>(p->Data);
                if (dragged != id) m_world->set_parent(dragged, id);
            }
            ImGui::EndDragDropTarget();
        }

        // ---- context menu ----
        if (ImGui::BeginPopupContextItem("##ctx")) {

            m_selected_entity = id;                            // right-click selects

            if (ImGui::MenuItem("Add Child Entity")) {
                const auto child = m_world->spawn();
                m_world->set_parent(child, id);
            }

            if (ImGui::MenuItem("Duplicate")) {
                const auto copy = m_world->spawn();
                if (m_inspector)
                    m_inspector->copy_components(id, copy);
                if (const auto p = m_world->parent_of(id); p.is_valid())
                    m_world->set_parent(copy, p);
            }

            if (ImGui::MenuItem("Delete", "Del")) {
                // If the current selection is this entity, drop it.
                if (m_selected_entity == id)
                    m_selected_entity = GLT::world::INVALID_ENTITY;
                m_world->despawn(id);
                ImGui::EndPopup();
                if (open && has_kids)
                    ImGui::TreePop();
                ImGui::PopID();
                return;                                        // don't touch `id` after despawn
            }

            // ---- Add Component submenu ----
            if (m_inspector && ImGui::BeginMenu("Add Component")) {

                // Group descriptors by category.
                std::map<std::string_view, std::vector<const GLT::world::component_descriptor*>> by_cat;
                for (const auto& d : m_inspector->descriptors())
                    by_cat[d.category].push_back(&d);

                for (auto& [cat, list] : by_cat) {
                    if (ImGui::BeginMenu(std::string(cat).c_str())) {
                        for (const auto* d : list) {
                            if (d->has(id))
                                continue;           // already present
                            if (ImGui::MenuItem(d->name.data()))
                                m_inspector->add_component(id, d->hash);
                        }
                        ImGui::EndMenu();
                    }
                }

                if (m_inspector->descriptors().empty())
                    ImGui::TextDisabled("no components registered");

                ImGui::EndMenu();
            }

            ImGui::EndPopup();
        }

        // ---- recurse ----
        if (open && has_kids) {
            for (auto c : m_world->children_of(id))
                render_outliner_node(c);
            ImGui::TreePop();
        }

        ImGui::PopID();
    }


    void world_viewport_window::set_cursor_captured(const bool captured) {

        m_cursor_captured = captured;

        auto window = GLT::platform::get_window_ref();
        if (!window)
            return;

        window->set_cursor_mode(captured
            ? GLT::platform::cursor_mode::cursor_disabled   // hidden + relative motion, frozen visually
            : GLT::platform::cursor_mode::cursor_normal);
    }


    void world_viewport_window::on_save_event(const GLT::save_event& event) {

        // save project data -------------------------------------------------------------------------------------------
        GLT::application::get().get_project_ref().serialize_projects_data(GLT::serializer::option::save);


        // save world --------------------------------------------------------------------------------------------------
        auto* world_layer = GLT::application::get().get_layer_stack_ref().get<GLT::world::world_layer>();
        VALIDATE(world_layer, return, "", "Failed to get world layer");

        auto world = world_layer->get_world();
        VALIDATE(world, return, "", "Failed to get world");

        const bool has_handle = world->world_handle() != GLT::asset::handle{};
        const bool force_as = event.is_forced_save_as();
        if (has_handle && !force_as) {

            auto result = world->save_world();
            if (result)
                GLT::event_bus::post(GLT::notification_event("Saved", "World saved successfully", GLT::logger::severity::info));
            else {

                const auto description = std::format("Failed to save World [{}]", GLT::util::enum_to_string(result.error()));
                GLT::event_bus::post(GLT::notification_event("Saved", description, GLT::logger::severity::warn));
            }
            return;
        }

        // need a location from the user
        const auto world_name = GLT::asset::registry::get_ref()->info(world->world_handle()).name;
        GLT::save_as_request_event::request req{
            .title = "Save World As",
            .default_name = has_handle ? world_name : "untitled_world",
            .default_dir = PROJECT_CONTENT_DIR / "world",
            .extension = std::string(GLT::asset::type_to_extension(GLT::asset::core_types::world)),
            .on_resolved = [world](const std::filesystem::path& chosen) {
    
                if (chosen.empty())
                    return;                                     // cancelled

                if (auto result = world->save_world_as(chosen); !result) {

                    LOG(error, "save_world_as failed: error {}", static_cast<int>(result.error()));
                    return;
                }

                // if the project settings dont have a world defined -> save the newly created world
                std::error_code error{};
                auto& project = GLT::application::get().get_project_ref();
                if (project.start_world.empty()
                    || (!GLT::vfs::exists(PROJECT_CONTENT_DIR / project.start_world, error) && !error))
                    project.start_world = GLT::project::to_content_relative(chosen);

                if (project.editor_start_world.empty()
                    || (!GLT::vfs::exists(PROJECT_CONTENT_DIR / project.editor_start_world, error) && !error))
                    project.editor_start_world = GLT::project::to_content_relative(chosen);
            },
        };

        GLT::event_bus::post(GLT::save_as_request_event(std::move(req)));
    }


    void world_viewport_window::on_save_as_request_event(const save_as_request_event& event) { m_save_as_event_buffer.push_back(event); }


    void world_viewport_window::open_next_save_request() {

        if (m_save_as_open)
            return;                         // one at a time

        if (m_save_as_queue.empty())
            return;

        m_save_as_open = true;
        ImGui::OpenPopup("##save_as_modal");
    }


    void world_viewport_window::render_save_as_popup() {

        open_next_save_request();

        if (!m_save_as_open)
            return;

        auto& pending = m_save_as_queue.front();

        // Always centered in the main viewport - no ImGuiCond_Appearing.
        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(560, 0), ImVec2(FLT_MAX, FLT_MAX));

        bool keep_open = true;
        if (ImGui::BeginPopupModal("##save_as_modal", &keep_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {

            ImGui::TextUnformatted(pending.req.title.c_str());
            ImGui::Separator();
            ImGui::Spacing();

            // ---- directory ----
            ImGui::TextUnformatted("Directory");
            ImGui::SameLine(100.f);
            GLT::UI::draw_directory_picker("##save_as_dir_picker", pending.directory, PROJECT_CONTENT_DIR);

            // ---- filename ----
            ImGui::TextUnformatted("Filename");
            ImGui::SameLine(100.f);
            const std::string ext_hint = "." + pending.req.extension;
            const f32 ext_w = ImGui::CalcTextSize(ext_hint.c_str()).x + 8.f;
            ImGui::SetNextItemWidth(-(ext_w + ImGui::GetStyle().ItemSpacing.x));
            ImGui::InputText("##name", pending.filename, sizeof(pending.filename));
            ImGui::SameLine();
            ImGui::TextDisabled("%s", ext_hint.c_str());

            ImGui::Spacing();

            // ---- preview + validation ----
            std::filesystem::path full = pending.directory / pending.filename;
            if (full.extension() != std::filesystem::path(ext_hint))
                full += ext_hint;

            ImGui::TextDisabled("Will write to:");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", full.generic_string().c_str());

            pending.error_message.clear();
            if (pending.directory.empty())
                pending.error_message = "Directory is required.";
            else if (pending.filename[0] == '\0')
                pending.error_message = "Filename is required.";

            if (!pending.error_message.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.f, 0.5f, 0.4f, 1.f), "%s", pending.error_message.c_str());
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // ---- buttons ----
            const bool can_confirm = pending.error_message.empty();

            ImGui::BeginDisabled(!can_confirm);
            if (ImGui::Button("Save", ImVec2(120, 0))) {
                resolve_save_as_request(true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                resolve_save_as_request(false);
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                resolve_save_as_request(false);
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        if (!keep_open && m_save_as_open)
            resolve_save_as_request(false);
    }


    void world_viewport_window::resolve_save_as_request(bool confirmed) {

        if (m_save_as_queue.empty()) {
            m_save_as_open = false;
            return;
        }

        pending_save_request pending = std::move(m_save_as_queue.front());
        m_save_as_queue.pop_front();
        m_save_as_open = false;

        std::filesystem::path resolved{};

        if (confirmed) {

            resolved = pending.directory / pending.filename;
            const std::string ext_hint = "." + pending.req.extension;
            if (resolved.extension() != ext_hint)
                resolved += ext_hint;
        }

        auto callback = std::move(pending.req.on_resolved);
        if (callback)
            callback(resolved);
    }


    bool world_viewport_window::render_settings_overlay() {

        if (!m_renderer)
            return false;

        const auto* type_data = m_renderer->settings_descriptor();
        void* data = m_renderer->settings_data();
        if (!type_data || !data)
            return false;

        ImGui::SameLine(0.0f, 6.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.f, 5.f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.10f, 0.10f, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.16f, 0.16f, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.20f, 0.20f, 0.90f));

        if (ImGui::Button("Settings"))
            ImGui::OpenPopup("##render_settings_popup");

        const ImVec2 btn_min = ImGui::GetItemRectMin();
        const ImVec2 btn_max = ImGui::GetItemRectMax();

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);

        // Same popup styling as the mode selector
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 8.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.08f, 0.08f, 0.08f, 0.92f));

        ImGui::SetNextWindowPos(ImVec2(btn_min.x, btn_max.y + 4.f), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(400.f, 0.f));

        bool open = false;
        if (ImGui::BeginPopup("##render_settings_popup", ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {

            open = true;

            if (ImGui::BeginTable("##renderer_settings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {

                UI::begin_table("Renderer_settings", false);

                std::string_view last_cat{};
                bool changed = false;
                GLT::reflect::for_each_member(*type_data, data,
                    [&](const GLT::reflect::member_descriptor& member_descriptor, void* ptr) -> bool {

                        if (member_descriptor.flags & (u32)GLT::reflect::member_flags::skip)
                            return true;

                        const std::string_view cat = member_descriptor.category ? member_descriptor.category : "";

                        // Category header row when it changes
                        if (!cat.empty() && cat != last_cat) {
                            last_cat = cat;
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_Header));
                            ImGui::TextUnformatted(cat.data());
                            ImGui::TableSetColumnIndex(1);
                        }

                        if (draw_reflected_setting(member_descriptor, ptr))
                            changed = true;

                        return true;
                    });

                UI::end_table();

                if (changed)            // One notification per frame, not per field
                    m_renderer->on_settings_changed();
            }

            ImGui::EndPopup();
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);

        return open;
    }


    bool world_viewport_window::draw_reflected_setting(const GLT::reflect::member_descriptor& md, void* ptr) {

        const std::string_view label = (md.display_name && md.display_name[0])
            ? std::string_view(md.display_name)
            : std::string_view(md.name);

        const bool has_range = (md.flags & (u32)GLT::reflect::member_flags::has_range) != 0;
        const f32  lo = static_cast<f32>(md.range_min);
        const f32  hi = static_cast<f32>(md.range_max);

        switch (md.kind) {

            case GLT::reflect::type_kind::boolean: {
                auto& value = *static_cast<bool*>(ptr);
                return GLT::UI::table_row(label, value);
            }

            case GLT::reflect::type_kind::u32: {
                auto& value = *static_cast<u32*>(ptr);
                return has_range ? GLT::UI::table_row<u32>(label, value, 0.2f, lo, hi) : GLT::UI::table_row<u32>(label, value);
            }
            case GLT::reflect::type_kind::i32: {
                auto& value = *static_cast<i32*>(ptr);
                return has_range ? GLT::UI::table_row<i32>(label, value, 0.2f, lo, hi) : GLT::UI::table_row<i32>(label, value);
            }
            case GLT::reflect::type_kind::f32: {
                auto& value = *static_cast<f32*>(ptr);
                return has_range ? GLT::UI::table_row<f32>(label, value, 0.1f, lo, hi) : GLT::UI::table_row<f32>(label, value);
            }
            case GLT::reflect::type_kind::f64: {
                auto& value = *static_cast<f64*>(ptr);
                return has_range ? GLT::UI::table_row<f64>(label, value, 0.1f, lo, hi) : GLT::UI::table_row<f64>(label, value);
            }

            default: {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label.data());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("(unsupported)");
                return false;
            }
        }
    }


    void world_viewport_window::render_gizmo(const ImVec2& image_min, const ImVec2& image_size) {

        if (!m_world || !m_selected_entity.is_valid())
            return;
        if (!m_world->alive(m_selected_entity))
            return;

        auto* scene = m_world->as<GLT::world::i_world_scene>();
        if (!scene)
            return;

        GLT::world::camera_snapshot cam{};
        if (!scene->get_camera_view(cam))
            return;

        if (image_size.x <= 1.f || image_size.y <= 1.f)
            return;

        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(image_min.x, image_min.y, image_size.x, image_size.y);

        // --- view / projection -------------------------------------------------
        // camera_snapshot::view is already world -> view (inverse of camera world).
        const glm::mat4& view = cam.view;

        const f32 aspect = image_size.x / image_size.y;
        glm::mat4 projection = glm::perspective( glm::radians(cam.fov), aspect, cam.near_plane, cam.far_plane);

        // --- model matrix from the entity's local transform --------------------
        const glm::vec3 position = m_world->get_local_position(m_selected_entity);
        const glm::quat rotation = m_world->get_local_rotation_q(m_selected_entity);
        const glm::vec3 scale = m_world->get_local_scale(m_selected_entity);

        glm::mat4 model = glm::translate(glm::mat4(1.f), position)
                        * glm::mat4_cast(rotation)
                        * glm::scale(glm::mat4(1.f), scale);

        // --- snap --------------------------------------------------------------
        float* snap_ptr = nullptr;
        if (m_gizmo_snap) {
            switch (m_gizmo_operation) {
                case ImGuizmo::TRANSLATE: snap_ptr = m_gizmo_snap_translate; break;
                case ImGuizmo::ROTATE:    snap_ptr = &m_gizmo_snap_rotate; break;
                case ImGuizmo::SCALE:     snap_ptr = &m_gizmo_snap_scale; break;
                default: break;
            }
        }

        // --- manipulate --------------------------------------------------------
        ImGuizmo::Manipulate(
            glm::value_ptr(view),
            glm::value_ptr(projection),
            m_gizmo_operation,
            m_gizmo_mode,
            glm::value_ptr(model),
            nullptr,
            snap_ptr);

        // --- write back only while the user is actually dragging ---------------
        if (!ImGuizmo::IsUsing())
            return;

        const glm::vec3 new_pos = glm::vec3(model[3]);
        const glm::quat new_rot = glm::quat_cast(glm::mat3(model));
        const glm::vec3 new_scale = glm::vec3(
            glm::length(glm::vec3(model[0])),
            glm::length(glm::vec3(model[1])),
            glm::length(glm::vec3(model[2])));

        // Position: translate_local expects a *local* delta.
        const glm::vec3 world_delta = new_pos - position;
        if (glm::length(world_delta) > 1e-6f) {
            const glm::vec3 local_delta = glm::inverse(rotation) * world_delta;
            m_world->translate_local(m_selected_entity, local_delta);
        }

        // Rotation: absolute set.
        if (glm::abs(glm::dot(rotation, new_rot)) < 0.9999f)
            m_world->set_local_rotation_q(m_selected_entity, new_rot);

        // Scale: absolute set.
        if (glm::distance(scale, new_scale) > 1e-6f)
            m_world->set_local_scale(m_selected_entity, new_scale);
    }


    void world_viewport_window::render_gizmo_toolbar(const ImVec2& image_min, const ImVec2& image_size) {

        using namespace GLT::imgui_config;

        constexpr f32 INSET = 8.f;
        constexpr f32 CARD_GAP = 3.f;
        constexpr f32 ROW_GAP = 3.f;
        const f32 right_x = image_min.x + image_size.x - INSET;
        const f32 top_y = image_min.y + INSET;
        const ImVec4 card_bg(.1f, .1f, .1f, 0.45f);

        constexpr ImGuiWindowFlags parent_flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoFocusOnAppearing;

        constexpr ImGuiChildFlags card_flags =
            ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY |
            ImGuiChildFlags_AlwaysUseWindowPadding;

        auto push_card_style = [&]() {
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 5.f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.f, 4.f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, card_bg);
        };
        auto pop_card_style = [&]() {
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(3);
        };

        ImGui::SetNextWindowPos(ImVec2(right_x, top_y), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(CARD_GAP, CARD_GAP));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.f));

        ImVec2 row1_pos{}, row1_size{};

        if (ImGui::Begin("##gizmo_row1", nullptr, parent_flags)) {

            push_card_style();
            if (ImGui::BeginChild("##gizmo_ops", ImVec2(0, 0), card_flags)) {
                if (gizmo_toggle_button("T", m_gizmo_operation == ImGuizmo::TRANSLATE, ImVec2(15.f, 15.f), true, icon_manager::icon::transfrom_translation))
                    m_gizmo_operation = ImGuizmo::TRANSLATE;

                ImGui::SameLine();
                if (gizmo_toggle_button("R", m_gizmo_operation == ImGuizmo::ROTATE, ImVec2(15.f, 15.f), true, icon_manager::icon::transfrom_rotation))
                    m_gizmo_operation = ImGuizmo::ROTATE;

                ImGui::SameLine();
                if (gizmo_toggle_button("S", m_gizmo_operation == ImGuizmo::SCALE, ImVec2(15.f, 15.f), true, icon_manager::icon::transfrom_scale))
                    m_gizmo_operation = ImGuizmo::SCALE;
            }
            ImGui::EndChild();
            pop_card_style();


            ImGui::SameLine();
            push_card_style();
            if (ImGui::BeginChild("##gizmo_space", ImVec2(0, 0), card_flags)) {
                if (gizmo_toggle_button("Global", m_gizmo_mode == ImGuizmo::WORLD))
                    m_gizmo_mode = ImGuizmo::WORLD;

                ImGui::SameLine();
                if (gizmo_toggle_button("Local", m_gizmo_mode == ImGuizmo::LOCAL))
                    m_gizmo_mode = ImGuizmo::LOCAL;
            }
            ImGui::EndChild();
            pop_card_style();


            ImGui::SameLine();
            push_card_style();
            if (ImGui::BeginChild("##gizmo_snap_toggle", ImVec2(0, 0), card_flags)) {
                if (gizmo_toggle_button("Snap", m_gizmo_snap))
                    m_gizmo_snap = !m_gizmo_snap;
            }
            ImGui::EndChild();
            pop_card_style();

            row1_pos = ImGui::GetWindowPos();
            row1_size = ImGui::GetWindowSize();
        }
        ImGui::End();

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(4);

        // ===== ROW 2 (snap settings) ============================================
        if (m_gizmo_snap) {

            // Anchor below row1, still right-aligned to the viewport's right edge.
            const ImVec2 anchor = ImVec2(right_x, row1_pos.y + row1_size.y + ROW_GAP);
            ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, ImVec2(1.0f, 0.0f));

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0.f, 0.f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,      ImVec2(CARD_GAP, CARD_GAP));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.f));

            if (ImGui::Begin("##gizmo_row2", nullptr, parent_flags)) {

                push_card_style();
                if (ImGui::BeginChild("##gizmo_snap_settings", ImVec2(0, 0), card_flags)) {

                    switch (m_gizmo_operation) {
                        case ImGuizmo::TRANSLATE:
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextUnformatted("Translate snap");
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(200.f);
                            ImGui::DragFloat3("##snap_t", &m_gizmo_snap_translate[0], .1f, 0.f, 100.f, "%.2f");
                            break;
                        case ImGuizmo::ROTATE:
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextUnformatted("Rotate snap");
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(120.f);
                            ImGui::DragFloat("##snap_r", &m_gizmo_snap_rotate, 1.f, 0.f, 100.f, "%.1f deg");
                            break;
                        case ImGuizmo::SCALE:
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextUnformatted("Scale snap");
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(120.f);
                            ImGui::DragFloat("##snap_s", &m_gizmo_snap_scale, .1f, 0.f, 100.f, "%.2f");
                            break;
                        default: break;
                    }
                }
                ImGui::EndChild();
                pop_card_style();
            }
            ImGui::End();

            ImGui::PopStyleColor();
            ImGui::PopStyleVar(4);
        }
    }


    bool world_viewport_window::gizmo_toggle_button(const char* label, bool active, const ImVec2& size, const bool use_icon, 
        icon_manager::icon icon) {


        const ImVec4& c_weak = ImVec4(0.10f, 0.10f, 0.10f, 0.55f);
        const ImVec4& c_default = GLT::imgui_config::get_action_color00_default_ref();
        const ImVec4& c_hover = GLT::imgui_config::get_action_color00_hover_ref();
        const ImVec4& c_active = GLT::imgui_config::get_action_color00_active_ref();

        // Same intensity ladder either way; just shifted one step up when active.
        const ImVec4& bg = active ? c_default : c_weak;
        const ImVec4& hov = active ? c_hover   : c_default;
        const ImVec4& act = active ? c_active  : c_hover;

        ImGui::PushStyleColor(ImGuiCol_Button, bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, act);

        // Match render_settings_overlay()'s button metrics.
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.f, 2.f));

        bool clicked{};
        if (use_icon) {

            const auto icon_data = icon_manager::get(icon);
            clicked = ImGui::ImageButton(label, icon_data.tex_ref, size, icon_data.uv0, icon_data.uv1);
        } else
            clicked = ImGui::Button(label, size);

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
        return clicked;
    }


    void world_viewport_window::update_selection_debug_draw() {

        // Renderer may not implement debug draw at all (e.g. null / headless).
        auto* debug = m_renderer ? m_renderer->as<GLT::render::i_debug_draw>() : nullptr;
        if (!debug)
            return;

        // Nothing selected -> nothing to refresh. Old watch entries expire on their own.
        if (!m_world || !m_selected_entity.is_valid() || !m_world->alive(m_selected_entity))
            return;

        constexpr f32 lifetime = 0.1f;
        const glm::vec4 color{ 1.f, 0.5f, 0.1f, 1.f };                                      // orange-ish selection tint
        debug->highlight_entity(m_selected_entity, color, lifetime, /*depth_test=*/true);
    }

}
