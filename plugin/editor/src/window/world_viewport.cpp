#include "util/pch.h"
#include "world_viewport.h"

#include <algorithm>
#include <cctype>

#include <imgui.h>
#include <imgui_internal.h>

#include <config/imgui_config.h>
#include <plugin_system/plugin_manager.h>
#include <platform/i_window.h>
#include <layer/layer_stack.h>
#include <asset/i_asset_registry.h>
#include <render/image.h>
#include <render/i_renderer.h>
#include <render/i_debug_draw.h>
#include <world/i_world.h>
#include <world/i_world_scene.h>
#include <world/controller.h>
#include <world/entity_preset.h>
#include <game/game_layer.h>
#include <game/game_session.h>
#include <render/i_scene_renderer.h>
#include <application.h>

#include "util/ui/pannel_collection.h"
#include "resource_manager/icon_manager.h"
#include "window/content_browser.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    namespace {

        // Outliner layout metrics
        constexpr f32 VIS_COL_WIDTH = 22.f;   // width of the visibility-icon column (left of every row)
        constexpr f32 ROW_ICON_SIZE = 14.f;   // square size of visibility + component icons
        constexpr f32 TYPE_ICON_GAP = 3.f;    // space between adjacent component icons

        // Zebra stripe + hover
        constexpr ImU32 ZEBRA_COLOR = IM_COL32(255, 255, 255, 8);
        constexpr f32   HOVER_ALPHA = 0.35f;

        // Viewport debug highlight for selection
        const glm::vec4 PRIMARY_HIGHLIGHT = { 1.f, 0.5f, 0.1f, 1.f };
        const glm::vec4 SECONDARY_HIGHLIGHT = { 1.f, 0.7f, 0.3f, 0.55f };
    }

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    namespace {

        // ---- query parsing ------------------------------------------------

        struct search_term {
            enum class kind { name, component };
            kind        type = kind::name;
            std::string text;              // already lowercased
            bool        negate = false;
        };

        struct search_query {
            bool                     match_all = false;
            std::vector<search_term> terms;

            [[nodiscard]] bool empty() const noexcept { return terms.empty(); }
        };

        inline std::string trim_copy(std::string s) {
            auto isws = [](unsigned char c) { return std::isspace(c) != 0; };
            auto first = std::find_if_not(s.begin(), s.end(), isws);
            auto last = std::find_if_not(s.rbegin(), s.rend(), isws).base();
            if (first >= last) return {};
            return std::string(first, last);
        }

        inline std::string to_lower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        search_query parse_search(const std::string& input) {

            search_query q;

            std::string s = trim_copy(input);
            if (s.empty())
                return q;

            // '&' prefix -> AND mode (must be followed by whitespace to avoid clashing with '&' in names)
            if (s[0] == '&' && (s.size() == 1 || std::isspace(static_cast<unsigned char>(s[1])))) {

                q.match_all = true;
                s = trim_copy(s.substr(1));
                if (s.empty())
                    return q;
            }

            size_t pos = 0;
            while (pos < s.size()) {

                while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])))
                    ++pos;
                if (pos >= s.size())
                    break;

                size_t start = pos;
                while (pos < s.size() && !std::isspace(static_cast<unsigned char>(s[pos])))
                    ++pos;

                std::string tok = s.substr(start, pos - start);
                if (tok.empty())
                    continue;

                search_term t;
                size_t i = 0;
                if (i < tok.size() && tok[i] == '!') {
                    t.negate = true;
                    ++i;
                }
                if (i < tok.size() && tok[i] == '>') {
                    t.type = search_term::kind::component;
                    t.text = tok.substr(i + 1);
                } else {
                    t.type = search_term::kind::name;
                    t.text = tok.substr(i);
                }

                if (t.text.empty())
                    continue;

                t.text = to_lower(std::move(t.text));
                q.terms.push_back(std::move(t));
            }

            return q;
        }

        bool match_term(GLT::world::entity_id id, const search_term& t,
                        GLT::world::i_world* world, GLT::world::i_world_inspector* inspector) {

            bool hit = false;

            if (t.type == search_term::kind::component) {

                if (inspector) {
                    const auto comps = inspector->components_on(id);
                    for (const auto* c : comps) {
                        if (!c || c->name.empty())
                            continue;
                        if (to_lower(std::string(c->name)).find(t.text) != std::string::npos) {
                            hit = true;
                            break;
                        }
                    }
                }
            } else {

                std::string name;
                if (world) {
                    auto n = world->entity_name(id);
                    name = n.empty() ? ("Entity " + std::to_string(id.index)) : std::string(n);
                }
                hit = to_lower(std::move(name)).find(t.text) != std::string::npos;
            }

            return t.negate ? !hit : hit;
        }

        bool entity_matches_query(GLT::world::entity_id id, const search_query& q,
                                  GLT::world::i_world* world, GLT::world::i_world_inspector* inspector) {

            if (q.empty())
                return true;

            if (q.match_all) {
                for (const auto& t : q.terms)
                    if (!match_term(id, t, world, inspector))
                        return false;
                return true;
            }

            for (const auto& t : q.terms)
                if (match_term(id, t, world, inspector))
                    return true;
            return false;
        }

        // Walks the full hierarchy; appends every entity that matches. Order = DFS pre-order.
        void collect_filtered_matches_rec(GLT::world::entity_id id, const search_query& q,
                                          GLT::world::i_world* world, GLT::world::i_world_inspector* inspector,
                                          std::vector<GLT::world::entity_id>& out) {

            if (entity_matches_query(id, q, world, inspector))
                out.push_back(id);

            for (auto c : world->children_of(id))
                collect_filtered_matches_rec(c, q, world, inspector, out);
        }

        // Truncates `text` so that `text + "..."` fits within max_width. Returns "" if even the ellipsis won't fit.
        std::string ellipsize_to_fit(const std::string& text, float max_width) {

            if (text.empty() || max_width <= 0.f)
                return {};

            if (ImGui::CalcTextSize(text.c_str()).x <= max_width)
                return text;

            const std::string ell = "...";
            const float ell_w = ImGui::CalcTextSize(ell.c_str()).x;
            if (ell_w > max_width)
                return {};

            std::string out = text;
            while (!out.empty()) {
                // Never cut in the middle of a UTF-8 continuation byte
                do { out.pop_back(); }
                while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80);

                if (ImGui::CalcTextSize((out + ell).c_str()).x <= max_width)
                    return out + ell;
            }
            return ell;
        }

    } // namespace

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    world_viewport_window::world_viewport_window() {

        // offscreen scene renderer
        GLT::render::offscreen_target_desc offscreen{};
        GLT::render::scene_renderer_desc desc{
            .target = offscreen,
            .clear_color = glm::vec4(0.09f, 0.09f, 0.09f, 1.f),
        };
        m_scene = GLT::application::get().create_scene_renderer(desc);
        auto* session = GLT::application::get().get_game_layer_ptr()->get_primary_session();
        session->add_renderer(m_scene);

        make_window_name("World Viewport");
        m_renderer = GLT::render::renderer::get_ref();
        m_content_browsers.push_back(GLT::editor::content_browser_window());

        m_save_sub_handle = GLT::event_bus::subscribe<GLT::save_event>(std::bind_front(&world_viewport_window::on_save_event, this), 10);
        m_save_as_sub_handle = GLT::event_bus::subscribe<save_as_request_event>(std::bind_front(&world_viewport_window::on_save_as_request_event, this));
    }


    world_viewport_window::~world_viewport_window() {

        GLT::event_bus::unsubscribe(m_save_as_sub_handle);
        GLT::event_bus::unsubscribe(m_save_sub_handle);
        m_content_browsers.clear();
        GLT::application::get().destroy_scene_renderer(m_scene);
        m_renderer.reset();
    }

    // CLASS PUBLIC ====================================================================================================

    void world_viewport_window::window(const f32 delta_time) {

        if (!m_show_window) {
            if (m_scene)
                m_scene->set_active(false);
            return;
        }

        apply_pending_dock();

        ImGui::SetNextWindowSizeConstraints(ImVec2(600, 400), ImVec2(std::numeric_limits<f32>::max(), std::numeric_limits<f32>::max()));
        ImGui::Begin(m_window_id.c_str(), &m_show_window);
        {
            render_inner_dockspace();
        }
        ImGui::End();

        render_viewport();
        render_details();
        render_outliner();

        if (m_scene)
            m_scene->set_active(m_viewport_image_drawn);

        for (auto& browsers : m_content_browsers)
            if (!browsers.should_close())
                browsers.window(delta_time);

        render_save_as_popup();
    }


    void world_viewport_window::update(const f32 /*delta_time*/) {

        if (!m_world)
            m_world = GLT::application::get().get_game_layer_ptr()->get_primary_session()->get_world();

        if (m_scene) {

            const auto extent = glm::uvec2(static_cast<u32>(std::max(1.f, m_viewport_size.x)),
                static_cast<u32>(std::max(1.f, m_viewport_size.y)));

            if (m_scene->get_extent() != extent)
                m_scene->resize(extent);
        }
        update_selection_debug_draw();

        for (auto& ev : m_save_as_event_buffer) {

            pending_save_request pending{};
            pending.req = ev.get();

            std::snprintf(pending.filename, sizeof(pending.filename), "%s", pending.req.default_name.c_str());

            pending.directory = PROJECT_CONTENT_DIR;
            if (pending.directory.empty())
                pending.directory = pending.req.default_dir;

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

            const ImVec2 image_min = ImGui::GetCursorScreenPos();
            const ImVec2 image_size = ImGui::GetContentRegionAvail();
            m_viewport_size = image_size;

            if (m_scene && m_scene->get_output_image()) {
                ImGui::Image(m_scene->get_output_image(), m_viewport_size);
                m_viewport_image_drawn = ImGui::IsItemVisible();
            } else
                ImGui::TextColored(ImVec4(1.f, 0.5f, 0.4f, 1.f), "Scene renderer not available");

            const bool image_hovered = ImGui::IsItemHovered();

            const bool mode_popup_open = render_mode_selector_overlay(image_min);
            const bool settings_popup_open = render_settings_overlay();
            const bool overlay_active = mode_popup_open || settings_popup_open;
            const bool right_down = ImGui::IsMouseDown(ImGuiMouseButton_Right);

            render_gizmo_toolbar(image_min, image_size);
            render_gizmo(image_min, image_size);

            auto* game_layer = GLT::application::get().get_game_layer_ptr();
            auto* session = game_layer ? game_layer->get_primary_session() : nullptr;
            auto* ctrl = session ? session->get_controller() : nullptr;

            if (!m_cursor_captured && image_hovered && !overlay_active && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                set_cursor_captured(true);
                if (ctrl)
                    ctrl->set_input_enabled(true);
            }

            if (m_cursor_captured && !right_down) {
                set_cursor_captured(false);
                if (ctrl)
                    ctrl->set_input_enabled(false);
            }
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
    }


    bool world_viewport_window::render_mode_selector_overlay(const ImVec2& image_min) {

        if (!m_renderer || !m_scene)
            return false;

        constexpr f32 INSET = 8.f;
        ImGui::SetCursorScreenPos(ImVec2(image_min.x + INSET, image_min.y + INSET));

        const auto modes = m_scene->supported_modes();
        const u32  current = m_scene->get_render_mode();

        const char* current_label = "Render mode";
        for (const auto& m : modes)
            if (m.id == current) {
                current_label = m.display_name;
                break;
            }

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.f, 5.f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.10f, 0.10f, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.16f, 0.16f, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.20f, 0.20f, 0.90f));

        if (ImGui::Button(current_label))
            ImGui::OpenPopup("##render_mode_popup");

        const ImVec2 btn_min = ImGui::GetItemRectMin();
        const ImVec2 btn_max = ImGui::GetItemRectMax();

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 6.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 2.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.f, 2.f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.08f, 0.08f, 0.08f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.20f, 0.20f, 0.70f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.30f, 0.30f, 0.30f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.35f, 0.35f, 0.35f, 0.95f));

        ImGui::SetNextWindowPos(ImVec2(btn_min.x, btn_max.y + 4.f), ImGuiCond_Appearing);

        bool popup_open = false;
        if (ImGui::BeginPopup("##render_mode_popup",
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {

            popup_open = true;

            const char* last_category = nullptr;
            for (const auto& m : modes) {

                if (m.has_category() && (last_category == nullptr || std::strcmp(last_category, m.category) != 0)) {
                    last_category = m.category;
                    ImGui::SeparatorText(m.category);
                }

                ImGui::PushID(static_cast<int>(m.id));

                const bool selected = (m.id == current);
                if (ImGui::Selectable(m.display_name, selected)) {
                    m_scene->set_render_mode(m.id);
                    ImGui::CloseCurrentPopup();
                }

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

    // ---- details --------------------------------------------------------------------------------------------------

    void world_viewport_window::render_details() {

        if (!ImGui::Begin("Details", nullptr, ImGuiWindowFlags_NoScrollbar)) {
            ImGui::End();
            return;
        }

        if (!m_world || !m_inspector) {
            ImGui::TextDisabled("Inspector unavailable");
            ImGui::End();
            return;
        }

        const auto primary = primary_selection();
        const size_t sel_count = m_selected_entities.size();

        if (!primary.is_valid() || !m_world->alive(primary)) {
            ImGui::TextDisabled("No entity selected");
            ImGui::End();
            return;
        }

        if (sel_count > 1) {
            ImGui::Text("Multiple entities selected");
            ImGui::TextDisabled("%zu entities", sel_count);
            ImGui::Separator();
            ImGui::TextDisabled("Select a single entity to edit its components.");
            ImGui::Spacing();
            ImGui::TextDisabled("Ctrl-click to toggle; Shift-click for range.");
            ImGui::End();
            return;
        }

        const auto id = primary;
        const auto& components = m_inspector->components_on(id);
        const auto cursor_pos = ImGui::GetCursorPosX();
        const float avail = ImGui::GetContentRegionAvail().x;
        {
            const auto name = m_world->entity_name(id);
            ImGui::Text("Entity:");
            if (!name.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%.*s", static_cast<int>(name.size()), name.data());
            }

            u32 comp_count = 0;
            for (const auto* c : components) { (void)c; ++comp_count; }

            const char* add_label = "+ Add Component";
            const float btn_w = ImGui::CalcTextSize(add_label).x + ImGui::GetStyle().FramePadding.x * 2.f;
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%u component%s", comp_count, comp_count == 1 ? "" : "s");

            ImGui::SameLine();
            ImGui::SetCursorPosX(cursor_pos + avail - btn_w);

            if (ImGui::Button(add_label))
                ImGui::OpenPopup("##add_comp");
        }

        const ImVec2 add_btn_max = ImGui::GetItemRectMax();
        render_add_component_popup(id, add_btn_max);

        ImGui::Separator();

        ImGui::BeginChild("##comp_list", ImVec2(0, 0));
        for (const auto* comp : components) {

            ImGui::PushID(static_cast<int>(comp->hash));

            if (!comp->has || !comp->has(id)) {
                ImGui::PopID();
                continue;
            }

            const bool open = ImGui::CollapsingHeader(comp->name.data(),
                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
            if (UI::transparent_button("..."))
                ImGui::OpenPopup("##comp_ctx");

            const ImVec2 ctx_btn_max = ImGui::GetItemRectMax();
            ImGui::SetNextWindowPos(ImVec2(ctx_btn_max.x, ctx_btn_max.y + 3.f), ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));

            bool removed = false;
            if (ImGui::BeginPopup("##comp_ctx")) {

                if (ImGui::MenuItem("Remove", nullptr, false, !comp->can_remove || comp->can_remove(id))) {
                    m_inspector->remove_component(id, comp->hash);
                    removed = true;
                }

                if (ImGui::MenuItem("Copy"))
                    m_clipboard_component = comp->hash;

                ImGui::EndPopup();
            }

            if (!removed && open) {
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
        ImGui::EndChild();

        ImGui::End();
    }


    void world_viewport_window::render_add_component_popup(GLT::world::entity_id id, const ImVec2& btn_max) {

        ImGui::SetNextWindowPos(ImVec2(btn_max.x, btn_max.y + 3.f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const float vp_bottom = vp->WorkPos.y + vp->WorkSize.y;
        const float vp_left = vp->WorkPos.x;
        const float space_below = vp_bottom - (btn_max.y + 3.f);
        const float space_left = btn_max.x - vp_left;
        const float max_h = std::max(1.f, std::min(400.f, space_below));
        const float max_w = std::max(1.f, std::min(520.f, space_left));

        ImGui::SetNextWindowSizeConstraints(ImVec2(160.f, 0.f), ImVec2(max_w, max_h));
        if (!ImGui::BeginPopup("##add_comp", ImGuiWindowFlags_AlwaysAutoResize))
            return;

        static char filter[64] = "";
        if (ImGui::IsWindowAppearing())
            filter[0] = '\0';

        ImGui::SetNextItemWidth(-1.f);
        ImGui::InputTextWithHint("##filter", "Search...", filter, sizeof(filter));
        ImGui::Separator();

        ImGui::BeginChild("##comp_scroll", ImVec2(0.f, 0.f), ImGuiChildFlags_AutoResizeY);

        const std::string_view needle = filter;
        auto matches = [needle](const GLT::world::component_descriptor& d) {
            return needle.empty() || d.name.find(needle) != std::string_view::npos;
        };

        bool any = false;
        for (const auto& d : m_inspector->descriptors()) {

            if (!matches(d) || d.has(id))
                continue;

            any = true;
            ImGui::PushID(static_cast<int>(d.hash));
            if (ImGui::Selectable(d.name.data())) {
                m_inspector->add_component(id, d.hash);
                ImGui::CloseCurrentPopup();
            }
            if (!d.category.empty() && ImGui::IsItemHovered())
                ImGui::SetTooltip("%.*s", static_cast<int>(d.category.size()), d.category.data());
            ImGui::PopID();
        }

        if (!any)
            ImGui::TextDisabled(needle.empty() ? "All components already added" : "No matching components");

        if (m_clipboard_component != 0) {
            const GLT::world::component_descriptor* pasted = nullptr;
            for (const auto& e : m_inspector->descriptors())
                if (e.hash == m_clipboard_component) { pasted = &e; break; }

            if (pasted && !pasted->has(id)) {
                ImGui::Separator();
                if (ImGui::Selectable(std::string("Paste: " + std::string(pasted->name)).c_str())) {
                    m_inspector->add_component(id, m_clipboard_component);
                    ImGui::CloseCurrentPopup();
                }
            }
        }

        ImGui::EndChild();
        ImGui::EndPopup();
    }

    // ---- outliner (main) ------------------------------------------------------------------------------------------

    void world_viewport_window::render_outliner() {

        if (!ImGui::Begin("Outliner")) {
            ImGui::End();
            return;
        }

        if (!m_world) {
            ImGui::TextDisabled("No world plugin loaded");
            ImGui::End();
            return;
        }

        if (!m_inspector)
            m_inspector = m_world->as<GLT::world::i_world_inspector>();

        prune_dead_selection();

        // ---- toolbar ----
        if (ImGui::Button("+ Entity"))
            ImGui::OpenPopup("+ Entity Preset Popup");

        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.f);
        ImGui::InputTextWithHint("##outliner_filter", "Filter...",
                                 m_outliner_filter, sizeof(m_outliner_filter));

        // ---- search-bar syntax help ----
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.f);
            ImGui::TextUnformatted("Search syntax");
            ImGui::Separator();
            ImGui::Text("cube                         name contains \"cube\"");
            ImGui::Text(">light                        has a light component");
            ImGui::Text(">audio >mesh        has audio OR mesh");
            ImGui::Text("& >audio >mesh    has audio AND mesh");
            ImGui::Text("!>light                       has NO light component");
            ImGui::Text("& hero >camera     name contains hero AND has camera");
            ImGui::Spacing();
            ImGui::TextDisabled("Component names match by substring (case-insensitive)");
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }

        // ---- preset popup ----
        if (ImGui::BeginPopup("+ Entity Preset Popup")) {

            std::string last_category{};
            for (const auto& preset : GLT::world::preset_registry::get().all()) {

                if (preset.category != last_category) {
                    if (!last_category.empty())
                        ImGui::Separator();
                    if (!preset.category.empty())
                        ImGui::TextDisabled("%s", preset.category.c_str());
                    last_category = preset.category;
                }

                ImGui::PushID(preset.name.c_str());

                const auto icon = GLT::editor::icon_manager::string_to_icon(preset.icon.c_str());
                if (icon.has_value()) {
                    const auto icon_data = icon_manager::get((*icon));
                    ImGui::Image(icon_data.tex_ref, ImVec2(16.f, 16.f), icon_data.uv0, icon_data.uv1);
                    ImGui::SameLine();
                }

                if (ImGui::Selectable(preset.name.c_str())) {
                    const auto id = m_world->spawn_preset(preset.name);
                    if (id.is_valid()) {
                        m_selected_entities = { id };
                        m_selection_anchor = id;
                    }
                }

                if (!preset.tooltip.empty() && ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", preset.tooltip.c_str());

                ImGui::PopID();
            }

            ImGui::EndPopup();
        }

        ImGui::SameLine();
        if (!m_selected_entities.empty())
            ImGui::TextDisabled("| %zu selected", m_selected_entities.size());
        else
            ImGui::TextDisabled("| %s", m_inspector ? "inspector ready" : "no inspector");

        ImGui::Separator();

        // ---- parse query ----
        const search_query query = parse_search(m_outliner_filter);
        const bool filter_mode = !query.empty();

        // ---- build visible order ----
        std::vector<GLT::world::entity_id> visible_order;
        visible_order.reserve(64);

        for (auto root : m_world->root_entities()) {
            if (filter_mode)
                collect_filtered_matches_rec(root, query, m_world, m_inspector, visible_order);
            else
                collect_visible_tree_order(root, visible_order);
        }

        if (filter_mode && visible_order.empty()) {
            ImGui::TextDisabled("No entities match");
            ImGui::End();
            return;
        }

        // ---- draw rows ----
        int row_index = 0;
        if (!filter_mode)
            for (auto root : m_world->root_entities())
                render_outliner_row(root, 0, row_index, visible_order, /*flat=*/false);
        else
            for (auto id : visible_order)
                render_outliner_row(id, 0, row_index, visible_order, /*flat=*/true);

        // ---- blank-click deselect ----
        if (!filter_mode
            && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)
            && !ImGui::IsAnyItemHovered()
            && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            clear_selection();
        }

        // ---- Delete key ----
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
            && ImGui::IsKeyPressed(ImGuiKey_Delete)
            && !m_selected_entities.empty()) {

            for (auto id : m_selected_entities)
                if (m_world->alive(id))
                    m_world->despawn(id);

            clear_selection();
        }

        ImGui::End();
    }

    // ---- outliner (per-row) ---------------------------------------------------------------------------------------

    void world_viewport_window::render_outliner_row(
        GLT::world::entity_id id,
        int depth,
        int& row_index,
        const std::vector<GLT::world::entity_id>& visible_order,
        bool flat) {

        if (!m_world->alive(id))
            return;

        ImGui::PushID(static_cast<int>(id.index));

        // ---- zebra bookkeeping (increment BEFORE recursion so children see the next stripe) ----
        const bool striped = (row_index % 2) != 0;
        ++row_index;

        const std::string name = entity_display_name(id);
        const bool has_kids = m_world->has_children(id);
        const bool is_sel = is_selected(id);
        const bool visible = true; // m_world->is_visible(id);

        const float text_h = ImGui::GetTextLineHeight();
        const float row_h = ImGui::GetTextLineHeightWithSpacing();
        const ImVec2 row_start = ImGui::GetCursorScreenPos();
        const float avail_w = ImGui::GetContentRegionAvail().x;
        const ImVec2 row_end = { row_start.x + avail_w, row_start.y + row_h };

        // ---- background ----
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool hovered = ImGui::IsMouseHoveringRect(row_start, row_end);

        if (is_sel)
            dl->AddRectFilled(row_start, row_end, ImGui::GetColorU32(ImGuiCol_Header));
        else if (hovered)
            dl->AddRectFilled(row_start, row_end, ImGui::GetColorU32(ImGuiCol_HeaderHovered, HOVER_ALPHA));
        else if (striped)
            dl->AddRectFilled(row_start, row_end, ZEBRA_COLOR);

        // ---- visibility toggle ----
        {
            const auto vis_icon_type = visible ? icon_manager::icon::show : icon_manager::icon::hide;
            const auto vis_data = icon_manager::get(vis_icon_type);

            ImGui::SetCursorScreenPos({ row_start.x, row_start.y });
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.10f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.20f));

            if (ImGui::ImageButton("##vis", vis_data.tex_ref, ImVec2(ROW_ICON_SIZE, ROW_ICON_SIZE), vis_data.uv0, vis_data.uv1))
                ; // m_world->set_visible(id, !visible);

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip(visible ? "Hide entity" : "Show entity");

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
        }

        // ---- tree arrow ----
        bool open = true;
        bool clicked = false;
        float content_x = 0.f;

        if (flat) {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf
                | ImGuiTreeNodeFlags_NoTreePushOnOpen
                | ImGuiTreeNodeFlags_SpanAvailWidth;

            ImGui::SetCursorScreenPos({ row_start.x + VIS_COL_WIDTH, row_start.y });
            ImGui::TreeNodeEx("##node", flags, "");
            clicked = ImGui::IsItemClicked();
            content_x = ImGui::GetItemRectMin().x + ImGui::GetTreeNodeToLabelSpacing();
        } else {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                | ImGuiTreeNodeFlags_NoTreePushOnOpen
                | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (!has_kids)
                flags |= ImGuiTreeNodeFlags_Leaf;

            const float indent_spacing = ImGui::GetStyle().IndentSpacing;
            ImGui::SetCursorScreenPos({ row_start.x + VIS_COL_WIDTH + depth * indent_spacing, row_start.y });
            open = ImGui::TreeNodeEx("##node", flags, "");
            clicked = ImGui::IsItemClicked();
            content_x = ImGui::GetItemRectMin().x + ImGui::GetTreeNodeToLabelSpacing();
        }

        // ---- component icons (right-aligned) ----
        const auto comp_icons = entity_component_icons(id);
        const f32 icons_w = comp_icons.empty() ? 0.f
            : (static_cast<f32>(comp_icons.size()) * ROW_ICON_SIZE
            + static_cast<f32>(comp_icons.size() - 1) * TYPE_ICON_GAP
            + TYPE_ICON_GAP * 2.f);

        // ---- name (ellipsized if colliding with icons) ----
        const f32 name_max_w = (row_end.x - content_x) - icons_w - 4.f;
        const std::string displayed_name = ellipsize_to_fit(name, name_max_w);

        ImGui::SetCursorScreenPos({ content_x, row_start.y });
        if (!visible)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(displayed_name.c_str());
        if (!visible)
            ImGui::PopStyleColor();
        clicked |= ImGui::IsItemClicked();

        // ---- hover tooltip (name region) ----
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            render_entity_tooltip(id);

        // ---- draw component icons ----
        if (!comp_icons.empty()) {
            const f32 icon_y = row_start.y + (text_h - ROW_ICON_SIZE) * 0.5f;
            f32 icon_x = row_end.x
                - static_cast<f32>(comp_icons.size()) * ROW_ICON_SIZE
                - static_cast<f32>(comp_icons.size() - 1) * TYPE_ICON_GAP
                - 4.f;

            for (auto ic : comp_icons) {
                ImGui::SetCursorScreenPos({ icon_x, icon_y });
                const auto data = icon_manager::get(ic);
                ImGui::Image(data.tex_ref, ImVec2(ROW_ICON_SIZE, ROW_ICON_SIZE), data.uv0, data.uv1);
                clicked |= ImGui::IsItemClicked();
                icon_x += ROW_ICON_SIZE + TYPE_ICON_GAP;
            }
        }

        if (clicked)
            handle_outliner_click(id, visible_order);

        render_entity_context_menu(id, visible);

        ImGui::SetCursorScreenPos({ row_start.x, row_end.y });
        ImGui::Dummy(ImVec2());

        if (!flat && open && has_kids) {
            for (auto child : m_world->children_of(id))
                render_outliner_row(child, depth + 1, row_index, visible_order, /*flat=*/false);
        }

        ImGui::PopID();
    }

    // ---- outliner (context menu) ----------------------------------------------------------------------------------

    void world_viewport_window::render_entity_context_menu(GLT::world::entity_id id, bool visible) {

        if (!ImGui::BeginPopupContextItem("##row_ctx"))
            return;

        // Right-click selects (unless already part of a multi-selection)
        if (!is_selected(id)) {
            m_selected_entities = { id };
            m_selection_anchor = id;
        }

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
            m_selected_entities = { copy };
            m_selection_anchor = copy;
        }

        if (ImGui::MenuItem(visible ? "Hide" : "Show"))
            ; // m_world->set_visible(id, !visible);

        if (ImGui::MenuItem("Delete", "Del")) {
            const auto victims = m_selected_entities;
            clear_selection();
            for (auto v : victims)
                if (m_world->alive(v))
                    m_world->despawn(v);
        }

        if (m_inspector && ImGui::BeginMenu("Add Component")) {

            std::map<std::string_view, std::vector<const GLT::world::component_descriptor*>> by_cat;
            for (const auto& d : m_inspector->descriptors())
                by_cat[d.category].push_back(&d);

            for (auto& [cat, list] : by_cat) {
                if (ImGui::BeginMenu(std::string(cat).c_str())) {
                    for (const auto* d : list) {
                        if (d->has(id))
                            continue;
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

    // ---- outliner (tooltip) ---------------------------------------------------------------------------------------

    void world_viewport_window::render_entity_tooltip(GLT::world::entity_id id) {

        ImGui::BeginTooltip();

        const std::string name = entity_display_name(id);
        ImGui::TextUnformatted(name.c_str());

        if (!m_inspector) {
            ImGui::TextDisabled("(no inspector)");
            ImGui::EndTooltip();
            return;
        }

        const auto comps = m_inspector->components_on(id);
        if (comps.empty()) {
            ImGui::TextDisabled("(no components)");
        } else {
            ImGui::Separator();
            for (const auto* c : comps) {
                if (!c || c->name.empty())
                    continue;

                ImGui::Bullet();
                ImGui::TextUnformatted(c->name.data(), c->name.data() + c->name.size());

                if (c->summary) {
                    auto s = c->summary(id);
                    if (!s.empty()) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("— %s", s.c_str());
                    }
                }
            }
        }

        ImGui::EndTooltip();
    }

    // ---- selection helpers ----------------------------------------------------------------------------------------

    void world_viewport_window::handle_outliner_click(GLT::world::entity_id id,
        const std::vector<GLT::world::entity_id>& visible_order) {

        const bool ctrl = ImGui::GetIO().KeyCtrl;
        const bool shift = ImGui::GetIO().KeyShift;

        if (shift && m_selection_anchor.is_valid()) {

            const auto it_anchor = std::find(visible_order.begin(), visible_order.end(), m_selection_anchor);
            const auto it_click = std::find(visible_order.begin(), visible_order.end(), id);

            if (it_anchor != visible_order.end() && it_click != visible_order.end()) {

                const size_t a = static_cast<size_t>(std::distance(visible_order.begin(), it_anchor));
                const size_t b = static_cast<size_t>(std::distance(visible_order.begin(), it_click));
                const size_t lo = std::min(a, b);
                const size_t hi = std::max(a, b);

                m_selected_entities.clear();
                m_selected_entities.reserve(hi - lo + 1);
                for (size_t i = lo; i <= hi; ++i)
                    m_selected_entities.push_back(visible_order[i]);
                return;
            }
        }

        if (ctrl) {
            const auto it = std::find(m_selected_entities.begin(), m_selected_entities.end(), id);
            if (it != m_selected_entities.end())
                m_selected_entities.erase(it);
            else {
                m_selected_entities.push_back(id);
                m_selection_anchor = id;
            }
            return;
        }

        m_selected_entities.clear();
        m_selected_entities.push_back(id);
        m_selection_anchor = id;
    }


    void world_viewport_window::clear_selection() {

        m_selected_entities.clear();
        m_selection_anchor = GLT::world::INVALID_ENTITY;
    }


    bool world_viewport_window::is_selected(GLT::world::entity_id id) const {

        return std::find(m_selected_entities.begin(), m_selected_entities.end(), id) != m_selected_entities.end();
    }


    GLT::world::entity_id world_viewport_window::primary_selection() const {

        if (m_selection_anchor.is_valid() && is_selected(m_selection_anchor))
            return m_selection_anchor;

        if (!m_selected_entities.empty())
            return m_selected_entities.front();

        return GLT::world::INVALID_ENTITY;
    }


    void world_viewport_window::prune_dead_selection() {

        if (!m_world)
            return;

        std::erase_if(m_selected_entities, [this](GLT::world::entity_id id) { return !m_world->alive(id); });

        if (m_selection_anchor.is_valid() && !m_world->alive(m_selection_anchor)) {
            m_selection_anchor = m_selected_entities.empty() ? GLT::world::INVALID_ENTITY : m_selected_entities.front();
        }
    }

    // ---- outliner (hierarchy helpers) -----------------------------------------------------------------------------

    void world_viewport_window::collect_visible_tree_order(GLT::world::entity_id id, std::vector<GLT::world::entity_id>& out) const {

        out.push_back(id);

        if (!is_tree_node_open(id))
            return;

        for (auto c : m_world->children_of(id))
            collect_visible_tree_order(c, out);
    }


    bool world_viewport_window::is_tree_node_open(GLT::world::entity_id id) const {

        if (!m_world || !m_world->has_children(id))
            return false;

        ImGui::PushID(static_cast<int>(id.index));
        const ImGuiID storage_id = ImGui::GetID("##node");
        ImGui::PopID();

        return ImGui::TreeNodeGetOpen(storage_id);
    }


    std::string world_viewport_window::entity_display_name(GLT::world::entity_id id) const {

        const auto name = m_world->entity_name(id);
        if (name.empty())
            return "Entity " + std::to_string(id.index);
        return std::string(name);
    }


    std::vector<icon_manager::icon_type> world_viewport_window::entity_component_icons(GLT::world::entity_id id) const {

        std::vector<icon_manager::icon_type> icons;

        if (!m_inspector)
            return icons;

        const auto comps = m_inspector->components_on(id);
        auto has = [&comps](std::string_view n) {
            for (const auto* c : comps)
                if (c && !c->name.empty() && c->name == n)
                    return true;
            return false;
        };

        // Order = visual order (left to right) of the right-aligned icon block
        if (has("mesh"))
            icons.push_back(icon_manager::icon::mesh_asset);
        if (has("light"))
            icons.push_back(icon_manager::icon::sun);
        if (has("camera"))
            icons.push_back(icon_manager::icon::camera);
        if (has("audio_source"))
            icons.push_back(icon_manager::icon::audio);

        return icons;
    }

    // ---- saving ---------------------------------------------------------------------------------------------------

    void world_viewport_window::on_save_event(const GLT::save_event& /*event*/) {

        GLT::application::get().get_project_ref().serialize_projects_data(GLT::serializer::option::save);

        auto* session = GLT::application::get().get_game_layer_ptr()->get_primary_session();
        VALIDATE(session, return, "", "No primary session");

        auto* world = session->get_world();
        VALIDATE(world, return, "", "Failed to get world");

        if (auto result = world->save(); result) {
            GLT::event_bus::post(GLT::notification_event("Saved", "World saved successfully", GLT::logger::severity::info));
            return;
        } else
            GLT::event_bus::post(GLT::notification_event("Saved",
                std::format("Failed to save World [{}]", GLT::util::enum_to_string(result.error())), GLT::logger::severity::warn));

        const auto& world_asset = world->asset();
        const bool has_asset = static_cast<bool>(world_asset);
        const auto world_name = has_asset
            ? GLT::asset::registry::get_ref()->info(world_asset.get()).name
            : std::string{ "untitled_world" };

        GLT::save_as_request_event::request req{
            .title = "Save World As",
            .default_name = world_name,
            .default_dir = PROJECT_CONTENT_DIR / "world",
            .extension = std::string(GLT::asset::type_to_extension(GLT::asset::core_types::world)),
            .on_resolved = [world](const std::filesystem::path& chosen) {

                if (chosen.empty())
                    return;

                if (auto r = world->save_as(chosen); !r) {
                    LOG(error, "save_as failed: error {}", static_cast<int>(r.error()));
                    return;
                }

                std::error_code error{};
                auto& project = GLT::application::get().get_project_ref();
                if (project.start_world.empty() || (!GLT::vfs::exists(PROJECT_CONTENT_DIR / project.start_world, error) && !error))
                    project.start_world = GLT::project::to_content_relative(chosen);

                if (project.editor_start_world.empty() || (!GLT::vfs::exists(PROJECT_CONTENT_DIR / project.editor_start_world, error) && !error))
                    project.editor_start_world = GLT::project::to_content_relative(chosen);
            },
        };

        GLT::event_bus::post(GLT::save_as_request_event(std::move(req)));
    }


    void world_viewport_window::on_save_as_request_event(const save_as_request_event& event) { m_save_as_event_buffer.push_back(event); }


    void world_viewport_window::open_next_save_request() {

        if (m_save_as_open || m_save_as_queue.empty())
            return;

        m_save_as_open = true;
        ImGui::OpenPopup("##save_as_modal");
    }


    void world_viewport_window::render_save_as_popup() {

        open_next_save_request();

        if (!m_save_as_open)
            return;

        auto& pending = m_save_as_queue.front();

        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(560, 0), ImVec2(FLT_MAX, FLT_MAX));

        bool keep_open = true;
        if (ImGui::BeginPopupModal("##save_as_modal", &keep_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {

            ImGui::TextUnformatted(pending.req.title.c_str());
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextUnformatted("Directory");
            ImGui::SameLine(100.f);
            GLT::UI::draw_directory_picker("##save_as_dir_picker", pending.directory, PROJECT_CONTENT_DIR);

            ImGui::TextUnformatted("Filename");
            ImGui::SameLine(100.f);
            const std::string ext_hint = "." + pending.req.extension;
            const f32 ext_w = ImGui::CalcTextSize(ext_hint.c_str()).x + 8.f;
            ImGui::SetNextItemWidth(-(ext_w + ImGui::GetStyle().ItemSpacing.x));
            ImGui::InputText("##name", pending.filename, sizeof(pending.filename));
            ImGui::SameLine();
            ImGui::TextDisabled("%s", ext_hint.c_str());

            ImGui::Spacing();

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

    // ---- render settings ------------------------------------------------------------------------------------------

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

                if (changed)
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

    // ---- gizmo ----------------------------------------------------------------------------------------------------

    void world_viewport_window::render_gizmo(const ImVec2& image_min, const ImVec2& image_size) {

        const auto id = primary_selection();
        if (!id.is_valid() || !m_world || !m_world->alive(id))
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

        const glm::mat4& view = cam.view;

        const f32 aspect = image_size.x / image_size.y;
        glm::mat4 projection = glm::perspective(glm::radians(cam.fov), aspect, cam.near_plane, cam.far_plane);

        const glm::vec3 position = m_world->get_local_position(id);
        const glm::quat rotation = m_world->get_local_rotation_q(id);
        const glm::vec3 scale = m_world->get_local_scale(id);

        glm::mat4 model = glm::translate(glm::mat4(1.f), position)
            * glm::mat4_cast(rotation)
            * glm::scale(glm::mat4(1.f), scale);

        float* snap_ptr = nullptr;
        if (m_gizmo_snap) {
            switch (m_gizmo_operation) {
                case ImGuizmo::TRANSLATE:
                    snap_ptr = m_gizmo_snap_translate;
                    break;
                case ImGuizmo::ROTATE:
                    snap_ptr = &m_gizmo_snap_rotate;
                    break;
                case ImGuizmo::SCALE:
                    snap_ptr = &m_gizmo_snap_scale;
                    break;
                default: break;
            }
        }

        ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), m_gizmo_operation, m_gizmo_mode,
            glm::value_ptr(model), nullptr, snap_ptr);

        if (!ImGuizmo::IsUsing())
            return;

        const glm::vec3 new_pos = glm::vec3(model[3]);
        const glm::quat new_rot = glm::quat_cast(glm::mat3(model));
        const glm::vec3 new_scale = glm::vec3(
            glm::length(glm::vec3(model[0])),
            glm::length(glm::vec3(model[1])),
            glm::length(glm::vec3(model[2])));

        const glm::vec3 world_delta = new_pos - position;
        if (glm::length(world_delta) > 1e-6f) {
            const glm::vec3 local_delta = glm::inverse(rotation) * world_delta;
            m_world->translate_local(id, local_delta);
        }

        if (glm::abs(glm::dot(rotation, new_rot)) < 0.9999f)
            m_world->set_local_rotation_q(id, new_rot);

        if (glm::distance(scale, new_scale) > 1e-6f)
            m_world->set_local_scale(id, new_scale);
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

        if (m_gizmo_snap) {

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
        icon_manager::icon_type icon) {

        const ImVec4& c_weak = ImVec4(0.10f, 0.10f, 0.10f, 0.55f);
        const ImVec4& c_default = GLT::imgui_config::get_action_color00_default_ref();
        const ImVec4& c_hover = GLT::imgui_config::get_action_color00_hover_ref();
        const ImVec4& c_active = GLT::imgui_config::get_action_color00_active_ref();

        const ImVec4& bg = active ? c_default : c_weak;
        const ImVec4& hov = active ? c_hover   : c_default;
        const ImVec4& act = active ? c_active  : c_hover;

        ImGui::PushStyleColor(ImGuiCol_Button, bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, act);

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

        auto* debug = m_scene ? m_scene->get_debug_draw() : nullptr;
        if (!debug || !m_world)
            return;

        constexpr f32 lifetime = 0.1f;
        const auto primary = primary_selection();

        for (auto id : m_selected_entities) {
            if (!m_world->alive(id))
                continue;
            const glm::vec4& color = (id == primary) ? PRIMARY_HIGHLIGHT : SECONDARY_HIGHLIGHT;
            debug->highlight_entity(id, color, lifetime, true);
        }
    }

    // ---- misc -----------------------------------------------------------------------------------------------------

    void world_viewport_window::set_cursor_captured(const bool captured) {

        m_cursor_captured = captured;

        auto* window = GLT::platform::window::get_ptr();
        if (!window)
            return;

        window->set_cursor_mode(GLT::application::get().get_main_window(), captured
            ? GLT::platform::cursor_mode::cursor_captured
            : GLT::platform::cursor_mode::cursor_normal);
    }

}
