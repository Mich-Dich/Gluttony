
#include "util/pch.h"
#include "world_viewport.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <plugin_system/plugin_manager.h>
#include <plugin_system/i_renderer_plugin.h>
#include <plugin_system/i_window_plugin.h>
#include <render/image.h>

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
    }


    world_viewport_window::~world_viewport_window() {

        m_content_browsers.clear();
        m_renderer.reset();
    }

    // CLASS PUBLIC ====================================================================================================

    void world_viewport_window::window(const f32 delta_time) {

        if (!m_show_window)
            return;

        apply_pending_dock();

        // A regular, dockable window. Its body hosts a nested dockspace for
        // the Viewport / Details / Tools sub-panels. The outer editor_layer
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

        for (auto& browsers : m_content_browsers)
            if (!browsers.should_close())
                browsers.window(delta_time);
    }


    void world_viewport_window::update(const f32 /*delta_time*/) {

        m_renderer->set_render_size({m_viewport_size.x, m_viewport_size.y});
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

    // ImGui::DragFloat3("Scale", &transform.scale.x, 0.05f, 0.001f, 1000.f);

    void world_viewport_window::render_viewport() {

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ImageRounding, 0.0f);
        if (ImGui::Begin("Viewport")) {

            m_viewport_size = ImGui::GetContentRegionAvail();
            ImGui::Image(m_renderer->get_rendered_image(), m_viewport_size);
            const bool hovered = ImGui::IsItemHovered();
            const bool right_down = ImGui::IsMouseDown(ImGuiMouseButton_Right);

            // Enter: press RMB while the image itself is the top-most hovered item
            if (!m_cursor_captured && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {

                set_cursor_captured(true);
                GLT::editor::context::get().set_viewport_interacted(true);
            }

            // release RMB. MUST NOT depend on hover — once captured, ImGui reports the virtual cursor at the window center
            if (m_cursor_captured && !right_down) {

                set_cursor_captured(false);
                GLT::editor::context::get().set_viewport_interacted(false);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
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
        for (const auto* d : m_inspector->components_on(id)) {

            ImGui::PushID(static_cast<int>(d->hash));

            // One collapsing header per component. Default-open.
            const bool open = ImGui::CollapsingHeader(d->name.data(),
                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

            // "..." button on the header row.
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
            if (ImGui::SmallButton("..."))
                ImGui::OpenPopup("##comp_ctx");

            if (ImGui::BeginPopup("##comp_ctx")) {

                if (ImGui::MenuItem("Remove", nullptr, false,
                        !d->can_remove || d->can_remove(id))) {
                    m_inspector->remove_component(id, d->hash);
                }

                if (ImGui::MenuItem("Copy")) {
                    // Stash the descriptor hash; paste applies on the target.
                    m_clipboard_component = d->hash;
                }

                ImGui::EndPopup();
            }

            if (open) {
                ImGui::Indent(8.f);
                if (d->draw)
                    d->draw(id);
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

            // Optional filter box — the popup gets long fast.
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

}
