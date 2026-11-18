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
#include <config/project.h>
#include <settings/engine_settings.h>

#include "util/ui/panel_collection.h"
#include "resource_manager/icon_manager.h"
#include "window/content_browser.h"
#include "settings/editor_settings.h"
#include "editor_layer.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    namespace {

        // Outliner layout metrics
        constexpr f32 VIS_COL_WIDTH = 22.f;   // width of the visibility-icon column (left of every row)
        constexpr f32 ROW_ICON_SIZE = 14.f;   // square size of visibility + component icons
        constexpr f32 TYPE_ICON_GAP = 3.f;    // space between adjacent component icons

        // Drag and drop inside the outliner
        constexpr const char* OUTLINER_ENTITY_PAYLOAD = "GLT_OUTLINER_ENTITIES";    // the entities are in m_dragged_entities
        constexpr const char* OUTLINER_FOLDER_PAYLOAD = "GLT_OUTLINER_FOLDER";      // folder_id

        // Auto scroll while dragging near the top / bottom edge of the outliner (or past it)
        constexpr f32 DRAG_SCROLL_ZONE = 36.f;          // height of the edge band that starts scrolling
        constexpr f32 DRAG_SCROLL_MIN_SPEED = 120.f;    // px / s at the inner border of the band
        constexpr f32 DRAG_SCROLL_MAX_SPEED = 900.f;    // px / s at the edge of the list
        constexpr f32 DRAG_SCROLL_BEYOND_MAX = 2.5f;    // past the edge it keeps speeding up, up to this x the max speed
        constexpr f32 DRAG_SCROLL_SIDE_SLACK = 48.f;    // outliner drags still count this far left / right of the panel

        // <project>/metadata/<this>/<world asset id>.txt: folders and hidden entities of a world (window/outliner_layout.h)
        constexpr const char* OUTLINER_LAYOUT_DIR = "outliner";

        // Zebra stripe + hover
        constexpr ImU32 ZEBRA_COLOR = IM_COL32(255, 255, 255, 5);
        constexpr f32   HOVER_ALPHA = 0.8f;
        constexpr f32   ROW_ROUNDING = 4.f;

        editor_settings::gizmo_settings& gizmo_cfg() { return editor_settings::get().gizmo; }
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

        // True if the user did something this frame (click, release, drag, typing, Enter / Tab). World changes on frames
        // without input are not the user's doing and are not recorded as undo steps
        bool user_input_this_frame() {

            const ImGuiIO& io = ImGui::GetIO();
            if (ImGui::IsAnyItemActive() || ImGui::GetCurrentContext()->ActiveIdPreviousFrame != 0)
                return true;

            for (int button = 0; button < ImGuiMouseButton_COUNT; ++button)
                if (io.MouseClicked[button] || io.MouseReleased[button])
                    return true;

            return io.InputQueueCharacters.Size > 0
                || ImGui::IsKeyPressed(ImGuiKey_Enter, false)
                || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)
                || ImGui::IsKeyPressed(ImGuiKey_Tab, false);
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

        m_enable_undo_system = true;
        m_details_edit.on_applied = [this](const undo_system::world_state& state) { select_after_undo(state); };
        m_gizmo_edit.on_applied = [this](const undo_system::world_state& state) { select_after_undo(state); };
        m_renderer = GLT::render::renderer::get_ref();
        add_content_browser();

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
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        ImGui::Begin(m_window_id.c_str(), &m_show_window);
        ImGui::PopStyleVar();
        {
            track_focus();
            render_panel_bar();
            render_inner_dockspace();
        }
        ImGui::End();

        render_viewport();
        render_details();
        render_outliner();

        if (m_scene)
            m_scene->set_active(m_viewport_image_drawn);

        for (auto& browser : m_content_browsers)
            if (!browser->should_close())
                browser->window(delta_time);

        render_save_as_popup();
        render_open_world_popup();
    }


    void world_viewport_window::update(const f32 /*delta_time*/) {

        open_requested_world();                                 // before anything reads m_world this frame

        auto* game_layer = GLT::application::get().get_game_layer_ptr();
        auto* session = game_layer ? game_layer->get_primary_session() : nullptr;
        auto* world = session ? session->get_world() : nullptr;

        if (world != m_world) {

            // Another world got loaded: the history and the selection belong to the old one
            if (m_layout_unsaved)
                save_outliner_layout();                         // folders of the old world, while it is still known
            m_details_edit.reset();
            m_gizmo_edit.reset();
            m_undo_stack.clear();
            m_undo_stack.mark_clean();                          // freshly loaded: nothing unsaved
            clear_selection();

            m_world = world;
            m_inspector = m_world ? m_world->as<GLT::world::i_world_inspector>() : nullptr;
            load_outliner_layout();
        }
        update_outliner_layout();

        if (m_scene) {

            const auto extent = glm::uvec2(static_cast<u32>(std::max(1.f, m_viewport_size.x)),
                static_cast<u32>(std::max(1.f, m_viewport_size.y)));

            if (m_scene->get_extent() != extent)
                m_scene->resize(extent);
        }
        // closed content browsers are gone for good (a new one can be opened from the "Panels" menu)
        std::erase_if(m_content_browsers, [](const GLT::unique_ref<content_browser_window>& browser) { return browser->should_close(); });

        update_selection_debug_draw();
        m_undo_stack.set_max_steps(static_cast<size_t>(editor_settings::get().history.max_steps));

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


    bool world_viewport_window::undo() {

        flush_edits();
        return base_window::undo();
    }


    bool world_viewport_window::redo() {

        flush_edits();
        return base_window::redo();
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void world_viewport_window::add_content_browser() { m_content_browsers.push_back(GLT::create_unique_ref<content_browser_window>()); }


    void world_viewport_window::render_panel_bar() {

        const f32 frame_h = ImGui::GetFrameHeight();
        const f32 bar_h = frame_h + 8.f;
        const ImVec2 bar_min = ImGui::GetCursorScreenPos();
        const f32 width = ImGui::GetContentRegionAvail().x;
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        draw_list->AddRectFilled(bar_min, ImVec2(bar_min.x + width, bar_min.y + bar_h), ImGui::GetColorU32(ImGuiCol_TitleBg));

        // world name on the left
        std::string world_name = "No world loaded";
        if (m_world) {
            const auto& asset = m_world->asset();
            world_name = static_cast<bool>(asset) ? GLT::asset::registry::get_ref()->info(asset.get()).name : std::string("Untitled world");
        }
        const auto world_icon = icon_manager::get(icon_manager::icon::world);
        const f32 icon = ImGui::GetTextLineHeight();
        const f32 text_y = bar_min.y + (bar_h - icon) * 0.5f;
        draw_list->AddImage(world_icon.tex_ref, ImVec2(bar_min.x + 10.f, text_y), ImVec2(bar_min.x + 10.f + icon, text_y + icon),
            world_icon.uv0, world_icon.uv1, ImGui::GetColorU32(ImGuiCol_TextDisabled));
        ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::bold));
        draw_list->AddText(ImVec2(bar_min.x + 16.f + icon, text_y), ImGui::GetColorU32(ImGuiCol_Text), world_name.c_str());
        ImGui::PopFont();

        // "Panels" menu on the right
        const char* panels_label = "Panels";
        const f32 button_w = ImGui::CalcTextSize(panels_label).x + frame_h + 12.f;
        ImGui::SetCursorScreenPos(ImVec2(bar_min.x + width - button_w - 6.f, bar_min.y + 4.f));
        if (UI::subtle_button("##panels_button", ImVec2(button_w, frame_h)))
            ImGui::OpenPopup("##panels_menu");
        {
            const ImVec2 min = ImGui::GetItemRectMin();
            draw_list->AddText(ImVec2(min.x + 8.f, min.y + ImGui::GetStyle().FramePadding.y), ImGui::GetColorU32(ImGuiCol_Text), panels_label);
            ImGui::RenderArrow(draw_list, ImVec2(min.x + button_w - frame_h * 0.75f, min.y + ImGui::GetStyle().FramePadding.y),
                ImGui::GetColorU32(ImGuiCol_TextDisabled), ImGuiDir_Down, 0.7f);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Show, hide and reset the panels of this window");

        const ImVec2 btn_max = ImGui::GetItemRectMax();
        ImGui::SetNextWindowPos(ImVec2(btn_max.x, btn_max.y + 4.f), ImGuiCond_Appearing, ImVec2(1.f, 0.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 8.f));
        if (ImGui::BeginPopup("##panels_menu")) {
            render_panels_menu();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();

        ImGui::SetCursorScreenPos(ImVec2(bar_min.x, bar_min.y + bar_h));
        ImGui::Dummy(ImVec2(0.f, 0.f));
    }


    void world_viewport_window::render_panels_menu() {

        ImGui::TextDisabled("Panels");
        ImGui::MenuItem("Viewport", nullptr, &m_show_viewport_panel);
        ImGui::MenuItem("Outliner", nullptr, &m_show_outliner_panel);
        ImGui::MenuItem("Details", nullptr, &m_show_details_panel);

        for (size_t x = 0; x < m_content_browsers.size(); ++x) {
            const std::string label = m_content_browsers.size() == 1 ? std::string("Content Browser") : std::format("Content Browser {}", x + 1);
            bool open = true;
            if (ImGui::MenuItem(label.c_str(), nullptr, &open) && !open)
                m_content_browsers[x]->close_window();
        }
        if (ImGui::MenuItem("New Content Browser"))
            add_content_browser();

        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout"))
            m_reset_layout = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Reopen every panel and put it back to its default place");
    }


    void world_viewport_window::render_inner_dockspace() {

        ImGuiID inner_id = ImGui::GetID("##world_viewport_dockspace");
        const ImVec2  inner_size = ImGui::GetContentRegionAvail();

        const bool no_layout_yet = (ImGui::DockBuilderGetNode(inner_id) == nullptr);
        if (m_reset_layout || no_layout_yet) {

            m_reset_layout = false;
            m_show_viewport_panel = m_show_outliner_panel = m_show_details_panel = true;
            if (m_content_browsers.empty())
                add_content_browser();
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
            ImGui::DockBuilderDockWindow(m_content_browsers[0]->get_window_id().c_str(), dock_bottom);

        ImGui::DockBuilderFinish(dockspace_id);
    }


    void world_viewport_window::render_viewport() {

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ImageRounding, 0.0f);

        m_viewport_image_drawn = false;
        if (!m_show_viewport_panel) {
            ImGui::PopStyleVar(2);
            return;
        }
        const bool viewport_open = ImGui::Begin("Viewport", &m_show_viewport_panel);
        track_focus();
        if (viewport_open) {

            const ImVec2 image_min = ImGui::GetCursorScreenPos();
            const ImVec2 image_size = ImGui::GetContentRegionAvail();
            m_viewport_size = image_size;

            if (m_scene && m_scene->get_output_image()) {
                ImGui::Image(m_scene->get_output_image(), m_viewport_size);
                m_viewport_image_drawn = ImGui::IsItemVisible();
                handle_viewport_world_drop();
            } else
                ImGui::TextColored(ImVec4(1.f, 0.5f, 0.4f, 1.f), "Scene renderer not available");

            const bool image_hovered = ImGui::IsItemHovered();

            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && !m_cursor_captured
                && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
                frame_selection();

            const bool mode_popup_open = render_mode_selector_overlay(image_min);
            const bool settings_popup_open = render_settings_overlay();
            const bool overlay_active = mode_popup_open || settings_popup_open;
            const bool right_down = ImGui::IsMouseDown(ImGuiMouseButton_Right);

            render_gizmo_toolbar(image_min, image_size);
            {
                // One undo step per gizmo drag, for everything the gizmo moves (the whole selection)
                const GLT::world::entity_id target = primary_selection();
                collect_gizmo_targets(target);

                // The tracker compares the entities' state before and after the gizmo. With many of them that is only
                // done while the gizmo can change something: the mouse is on it or it is being dragged
                const bool track = m_gizmo_targets.size() <= 64 || m_gizmo_edit.is_open() || ImGuizmo::IsOver() || ImGuizmo::IsUsing();
                if (track)
                    m_gizmo_edit.begin(m_world, m_gizmo_targets);
                render_gizmo(image_min, image_size);

                const bool using_gizmo = ImGuizmo::IsUsing();
                const char* verb = m_gizmo_operation == ImGuizmo::ROTATE ? "Rotate"
                                 : m_gizmo_operation == ImGuizmo::SCALE  ? "Scale" : "Move";
                if (track || m_gizmo_edit.is_open())
                    m_gizmo_edit.end(!using_gizmo ? std::string{} : (m_gizmo_targets.size() > 1
                            ? std::format("{} {} entities", verb, m_gizmo_targets.size()) : std::format("{} {}", verb, entity_display_name(target))),
                        using_gizmo, using_gizmo);
            }

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

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
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
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f);
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

        if (!m_show_details_panel)
            return;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 10.f));
        const bool details_open = ImGui::Begin("Details", &m_show_details_panel, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();
        track_focus();
        if (!details_open) {
            ImGui::End();
            return;
        }

        if (!m_world || !m_inspector) {
            UI::empty_state("Inspector unavailable", "The loaded world plugin does not offer an inspector");
            ImGui::End();
            return;
        }

        // What is edited: one entity, or several at once (the primary one is shown, see below)
        const std::vector<GLT::world::entity_id> targets = details_targets();
        if (targets.empty()) {
            UI::empty_state("Nothing selected", "Click an entity in the Outliner to see its components");
            ImGui::End();
            return;
        }

        const auto id = targets.front();                        // the primary selection: its values are the ones shown
        const bool multi = targets.size() > 1;
        const bool is_world = m_world->is_world_entity(id);

        // Everything drawn from here on may change the entities. The tracker turns each interaction into one undo step.
        // It compares the entities' state before and after the frame: with a large selection only on frames in which
        // the user does something (nothing can change on the others)
        // "Something the user does" has to be in THIS panel: flying the camera or clicking in the viewport is input too,
        // and a snapshot of thousands of entities for every such frame made the editor crawl
        const bool input = user_input_this_frame();
        const bool panel_in_use = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
            || ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
        const bool track = targets.size() <= 64 || m_details_edit.is_open() || (input && panel_in_use);
        m_details_tracking = track;
        if (track)
            m_details_edit.begin(m_world, targets);

        // ---- which components ----
        if (multi)
            refresh_multi_components(targets);
        else {
            m_multi_components.clear();
            m_multi_key = 0;
            for (const auto* comp : m_inspector->components_on(id))
                m_multi_components.push_back({ comp, 1u });
        }
        const u32 target_count = static_cast<u32>(targets.size());

        // ---- header: icon, name, subtitle, add button ----
        {
            const f32 frame_h = ImGui::GetFrameHeight();
            const ImVec2 start = ImGui::GetCursorScreenPos();
            const f32 width = ImGui::GetContentRegionAvail().x;
            const f32 icon_size = frame_h * 1.4f;

            const auto icons = entity_component_icons(id);
            const auto icon = icon_manager::get(is_world ? icon_manager::icon::world : (icons.empty() ? icon_manager::icon::empty : icons.front()));
            ImGui::GetWindowDrawList()->AddRectFilled(start, ImVec2(start.x + icon_size, start.y + icon_size),
                ImGui::GetColorU32(GLT::imgui_config::get_action_color_gray_default_ref()), 6.f);
            const f32 inset = icon_size * 0.22f;
            ImGui::GetWindowDrawList()->AddImage(icon.tex_ref, ImVec2(start.x + inset, start.y + inset),
                ImVec2(start.x + icon_size - inset, start.y + icon_size - inset), icon.uv0, icon.uv1);

            const char* add_label = "+ Add";
            const f32 add_w = ImGui::CalcTextSize(add_label).x + ImGui::GetStyle().FramePadding.x * 4.f;
            const f32 text_x = start.x + icon_size + 8.f;
            const f32 text_w = std::max(20.f, start.x + width - add_w - 8.f - text_x);

            const std::string name = multi ? std::format("{} entities", target_count)
                : (is_world ? std::string("World") : entity_display_name(id));
            ImGui::SetCursorScreenPos(ImVec2(text_x, start.y - 1.f));
            ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::bold));
            ImGui::TextUnformatted(ellipsize_to_fit(name, text_w).c_str());
            ImGui::PopFont();

            const size_t component_count = m_multi_components.size();
            const std::string subtitle = multi
                ? std::format("Values of {}", entity_display_name(id))
                : std::format("{} · {} component{}", is_world ? "World settings" : std::format("Entity {}", id.index),
                    component_count, component_count == 1 ? "" : "s");
            ImGui::SetCursorScreenPos(ImVec2(text_x, ImGui::GetCursorScreenPos().y));
            ImGui::TextDisabled("%s", ellipsize_to_fit(subtitle, text_w).c_str());

            ImGui::SetCursorScreenPos(ImVec2(start.x + width - add_w, start.y + (icon_size - frame_h) * 0.5f));
            if (UI::accent_button(add_label, ImVec2(add_w, 0.f)))
                ImGui::OpenPopup("##add_comp");
            if (multi && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Add a component to every selected entity that does not have it");
            const ImVec2 add_btn_max = ImGui::GetItemRectMax();
            render_add_component_popup(targets, add_btn_max);

            ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + icon_size + 10.f));
            ImGui::Dummy(ImVec2(0.f, 0.f));
        }

        // ---- several entities: which components to show ----
        if (multi) {
            static constexpr const char* MODES[] = { "Shared", "All" };
            i32 mode = static_cast<i32>(m_multi_mode);
            if (UI::segmented_control("##multi_mode", mode, MODES, ImGui::GetContentRegionAvail().x))
                m_multi_mode = mode == 0 ? multi_edit_mode::shared : multi_edit_mode::all;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Shared: only the components every selected entity has. A change goes to all of them.\n"
                    "All: every component any of them has. A change goes to the entities that have the component.\n"
                    "Only the value you change is applied, the entities keep their own values everywhere else");
            ImGui::Dummy(ImVec2(0.f, 4.f));

            if (m_multi_components.empty())
                ImGui::TextDisabled(m_multi_mode == multi_edit_mode::shared
                    ? "The selected entities have no component in common" : "The selected entities have no components");
        }

        // ---- components, one card each ----
        ImGui::BeginChild("##comp_list", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        UI::begin_properties(UI::property_layout::compact);

        const f32 menu_size = ImGui::GetFrameHeight();
        for (const auto& entry : m_multi_components) {

            const auto* comp = entry.descriptor;
            if (!comp || !comp->has)
                continue;

            // The entity whose values are drawn: the primary one if it has the component, else the first that does
            GLT::world::entity_id shown = id;
            if (!comp->has(shown)) {
                shown = GLT::world::INVALID_ENTITY;
                for (const auto other : targets)
                    if (comp->has(other)) {
                        shown = other;
                        break;
                    }
            }
            if (!shown.is_valid())
                continue;                                       // removed since the list was made

            ImGui::PushID(static_cast<int>(comp->hash));

            const std::string title = UI::pretty_name(comp->name);
            const std::string summary = !multi ? (comp->summary ? comp->summary(shown) : std::string{})
                : (entry.count < target_count ? std::format("{} of {}", entry.count, target_count) : std::string{});
            bool removed = false;

            UI::card_options options{};
            options.summary = summary.c_str();
            options.collapsible = true;
            options.header_actions_width = menu_size;
            options.header_actions = [&]() {

                if (UI::menu_button("##comp_menu", menu_size, "Component options"))
                    ImGui::OpenPopup("##comp_ctx");

                const ImVec2 btn_max = ImGui::GetItemRectMax();
                ImGui::SetNextWindowPos(ImVec2(btn_max.x, btn_max.y + 3.f), ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
                if (ImGui::BeginPopup("##comp_ctx")) {

                    if (ImGui::MenuItem("Copy"))
                        m_clipboard_component = comp->hash;

                    ImGui::Separator();
                    bool removable = false;
                    for (const auto target : targets)
                        removable |= comp->has(target) && (!comp->can_remove || comp->can_remove(target));
                    if (ImGui::MenuItem(multi ? "Remove from all" : "Remove", nullptr, false, removable)) {
                        if (!m_details_tracking) {
                            m_details_edit.begin(m_world, targets);
                            m_details_tracking = true;
                        }
                        m_details_edit.set_name(std::format("Remove {}", title));
                        for (const auto target : targets)
                            if (comp->has(target) && (!comp->can_remove || comp->can_remove(target)))
                                m_inspector->remove_component(target, comp->hash);
                        removed = true;
                        m_multi_key = 0;                        // the list of components changed
                    }
                    ImGui::EndPopup();
                }
            };

            if (const auto icon = component_icon(comp->name); icon.has_value()) {
                const auto data = icon_manager::get(*icon);
                options.icon = data.tex_ref;
                options.icon_uv0 = data.uv0;
                options.icon_uv1 = data.uv1;
                options.has_icon = true;
            }

            if (UI::begin_card(title.c_str(), options) && !removed) {

                if (!comp->draw)
                    ImGui::TextDisabled("Runtime only, nothing to edit");
                else if (!multi)
                    comp->draw(shown);
                else {

                    // Draw the component of ONE entity; what the user changed in it is then applied to the others
                    // (i_storage::apply_changes: only the fields that differ from before the draw)
                    auto* storage = m_world->storage_for(comp->hash);
                    const std::shared_ptr<void> before = storage ? storage->capture(shown) : nullptr;

                    comp->draw(shown);

                    if (before && input) {
                        for (const auto target : targets)
                            if (target != shown && comp->has(target))
                                storage->apply_changes(shown, before.get(), target);
                    } else if (!before) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                        ImGui::PushTextWrapPos(0.f);
                        ImGui::Text("Changes here only reach %s: this component cannot be edited for several entities at once",
                            entity_display_name(shown).c_str());
                        ImGui::PopTextWrapPos();
                        ImGui::PopStyleColor();
                    }
                }
            }
            UI::end_card();

            ImGui::PopID();
        }

        UI::end_properties();
        ImGui::EndChild();

        if (m_details_tracking || m_details_edit.is_open())
            m_details_edit.end(multi ? std::format("Edit {} entities", target_count) : std::format("Edit {}", entity_display_name(id)),
                ImGui::IsAnyItemActive(), input);

        ImGui::End();
    }


    std::vector<GLT::world::entity_id> world_viewport_window::details_targets() const {

        std::vector<GLT::world::entity_id> targets;
        if (!m_world)
            return targets;

        const auto primary = primary_selection();
        if (!primary.is_valid() || !m_world->alive(primary))
            return targets;

        // (the selection never holds an entity twice: no search for duplicates, that was quadratic with thousands selected)
        targets.reserve(m_selected_entities.size());
        targets.push_back(primary);
        for (const auto id : m_selected_entities)
            if (id != primary && m_world->alive(id))
                targets.push_back(id);

        // The world entity has its own kind of components: it is edited alone
        if (targets.size() > 1)
            std::erase_if(targets, [this](const GLT::world::entity_id id) { return m_world->is_world_entity(id); });
        return targets;
    }


    // The components the Details panel shows for several entities, with how many of them have each. Asking every entity
    // for every component type is too much to do each frame for a large selection: redone when the selection or the
    // mode changes, and every few frames to follow components that were added / removed elsewhere
    void world_viewport_window::refresh_multi_components(const std::span<const GLT::world::entity_id> targets) {

        u64 key = 14695981039346656037ull ^ static_cast<u64>(m_multi_mode);
        for (const auto id : targets)
            key = (key ^ ((static_cast<u64>(id.generation) << 32) | id.index)) * 1099511628211ull;
        key |= 1ull;                                            // 0 means "rebuild"

        const u32 refresh_after = targets.size() > 256 ? 30u : 6u;
        if (key == m_multi_key && ++m_multi_age < refresh_after)
            return;
        m_multi_key = key;
        m_multi_age = 0;

        m_multi_components.clear();
        const u32 total = static_cast<u32>(targets.size());
        for (const auto& descriptor : m_inspector->descriptors()) {

            if (!descriptor.has)
                continue;
            u32 count = 0;
            for (const auto id : targets)
                count += descriptor.has(id) ? 1u : 0u;

            if (count == total || (count > 0 && m_multi_mode == multi_edit_mode::all))
                m_multi_components.push_back({ &descriptor, count });
        }
    }


    // Adds to every entity of [ids] that can have the component and does not have it yet
    void world_viewport_window::render_add_component_popup(const std::span<const GLT::world::entity_id> ids, const ImVec2& btn_max) {

        // true if at least one of the entities can get the component
        const auto addable = [&](const GLT::world::component::descriptor& d) {
            for (const auto id : ids)
                if (!d.has(id) && component_fits(d, id))
                    return true;
            return false;
        };
        const auto add_to_all = [&](const GLT::world::component::descriptor& d) {
            for (const auto id : ids)
                if (!d.has(id) && component_fits(d, id))
                    m_inspector->add_component(id, d.hash);
            m_multi_key = 0;                                    // the Details panel lists the components again
        };

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
        auto matches = [needle](const GLT::world::component::descriptor& d) {
            return needle.empty() || d.name.find(needle) != std::string_view::npos;
        };

        bool any = false;
        for (const auto& d : m_inspector->descriptors()) {

            if (!matches(d) || !addable(d))
                continue;

            any = true;
            ImGui::PushID(static_cast<int>(d.hash));
            if (ImGui::Selectable(d.name.data())) {
                if (!m_details_tracking) {                      // the panel skipped it this frame (large selection, no input)
                    m_details_edit.begin(m_world, ids);
                    m_details_tracking = true;
                }
                m_details_edit.set_name(std::format("Add {}", d.name));
                add_to_all(d);
                ImGui::CloseCurrentPopup();
            }
            if (!d.category.empty() && ImGui::IsItemHovered())
                ImGui::SetTooltip("%.*s", static_cast<int>(d.category.size()), d.category.data());
            ImGui::PopID();
        }

        if (!any)
            ImGui::TextDisabled(needle.empty() ? "All components already added" : "No matching components");

        if (m_clipboard_component != 0) {
            const GLT::world::component::descriptor* pasted = nullptr;
            for (const auto& e : m_inspector->descriptors())
                if (e.hash == m_clipboard_component) { pasted = &e; break; }

            if (pasted && addable(*pasted)) {
                ImGui::Separator();
                if (ImGui::Selectable(std::string("Paste: " + std::string(pasted->name)).c_str())) {
                    if (!m_details_tracking) {
                        m_details_edit.begin(m_world, ids);
                        m_details_tracking = true;
                    }
                    m_details_edit.set_name(std::format("Paste {}", pasted->name));
                    add_to_all(*pasted);
                    ImGui::CloseCurrentPopup();
                }
            }
        }

        ImGui::EndChild();
        ImGui::EndPopup();
    }

    // ---- outliner (main) ------------------------------------------------------------------------------------------

    void world_viewport_window::outliner_drag_autoscroll() {

        const ImGuiPayload* payload = ImGui::GetDragDropPayload();
        if (payload == nullptr || !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            return;                                             // the payload outlives the drop by a frame: mouse must be held

        const bool own_drag = payload->IsDataType(OUTLINER_ENTITY_PAYLOAD) || payload->IsDataType(OUTLINER_FOLDER_PAYLOAD);
        if (ImGui::GetScrollMaxY() <= 0.f)
            return;                                             // everything fits, nothing to scroll

        // The visible list area of this window (without title / tab bar and scrollbar)
        const ImRect inner = ImGui::GetCurrentWindow()->InnerRect;
        const ImVec2 mouse = ImGui::GetMousePos();
        if (inner.GetHeight() < 1.f)
            return;

        const f32 slack = own_drag ? DRAG_SCROLL_SIDE_SLACK : 0.f;
        if (mouse.x < inner.Min.x - slack || mouse.x > inner.Max.x + slack)
            return;
        if (!own_drag && (mouse.y < inner.Min.y || mouse.y > inner.Max.y || !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)))
            return;                                             // foreign drags: only while they are over this panel

        const f32 zone = std::min(DRAG_SCROLL_ZONE, inner.GetHeight() * 0.25f);

        // depth into the band: 0 at its inner border, 1 at the panel edge, > 1 past it
        f32 depth = 0.f;
        f32 direction = 0.f;
        if (mouse.y < inner.Min.y + zone) {
            depth = (inner.Min.y + zone - mouse.y) / zone;
            direction = -1.f;
        } else if (mouse.y > inner.Max.y - zone) {
            depth = (mouse.y - (inner.Max.y - zone)) / zone;
            direction = 1.f;
        }
        if (direction == 0.f)
            return;

        f32 speed;
        if (depth <= 1.f)
            speed = DRAG_SCROLL_MIN_SPEED + (DRAG_SCROLL_MAX_SPEED - DRAG_SCROLL_MIN_SPEED) * depth * depth;
        else
            speed = DRAG_SCROLL_MAX_SPEED * std::min(1.f + (depth - 1.f) * 0.5f, DRAG_SCROLL_BEYOND_MAX);

        const f32 dt = std::min(ImGui::GetIO().DeltaTime, 0.1f);    // a hitch must not throw the list to the end
        const f32 target = std::clamp(ImGui::GetScrollY() + direction * speed * dt, 0.f, ImGui::GetScrollMaxY());
        if (target != ImGui::GetScrollY())
            ImGui::SetScrollY(target);
    }


    void world_viewport_window::render_outliner() {

        if (!m_show_outliner_panel)
            return;

        const bool outliner_open = ImGui::Begin("Outliner", &m_show_outliner_panel);
        track_focus();
        if (!outliner_open) {
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
        if (UI::accent_button("+ Add"))
            ImGui::OpenPopup("+ Entity Preset Popup");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Add an entity from a preset");

        ImGui::SameLine();
        {
            const auto folder_icon = icon_manager::get(icon_manager::icon::folder_add);
            const f32 icon_size = ImGui::GetFrameHeight() - 2.f * ImGui::GetStyle().FramePadding.y;
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.10f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.20f));
            const bool new_folder = ImGui::ImageButton("##new_folder", folder_icon.tex_ref, ImVec2(icon_size, icon_size), folder_icon.uv0, folder_icon.uv1);
            ImGui::PopStyleColor(3);
            if (new_folder)
                create_folder(m_focused_folder, false);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("New folder (inside the folder that is selected)\n"
                    "Folders only organise this list: drag entities or folders onto a folder.\n"
                    "They are not part of the world and are not saved in it");
        }

        ImGui::SameLine();
        const ImVec2 search_min = ImGui::GetCursorScreenPos();
        UI::search_field("##outliner_filter", m_outliner_filter, sizeof(m_outliner_filter), "Filter   (>light, !>mesh, ...)");
        const ImVec2 search_max = ImVec2(search_min.x + ImGui::GetContentRegionAvail().x, search_min.y + ImGui::GetFrameHeight());

        // ---- search-bar syntax help ----
        if (ImGui::IsMouseHoveringRect(search_min, search_max) && ImGui::IsWindowHovered()
            && GImGui->HoveredIdTimer > 0.6f && !ImGui::IsAnyItemActive()) {
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

                    flush_edits();
                    undo_system::world_change change(*m_world, {});

                    const auto id = m_world->spawn_preset(preset.name);
                    if (id.is_valid()) {

                        std::vector<GLT::world::entity_id> spawned;
                        collect_subtree(id, spawned);                   // presets can spawn children
                        for (const auto e : spawned)
                            change.created(e);
                        change.commit(m_undo_stack, std::format("Add {}", preset.name),
                            [this](const undo_system::world_state& state) { select_after_undo(state); });

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

        // (no "N selected" line here: it moved every row down under the mouse the moment a second entity got selected.
        // The Details panel shows the count)

        ImGui::Dummy(ImVec2(0.f, 2.f));

        // ---- parse query ----
        const search_query query = parse_search(m_outliner_filter);
        const bool filter_mode = !query.empty();

        // ---- build the rows (the open tree, flattened; depth per row for the indent) ----
        std::vector<outliner_row> rows;
        std::vector<GLT::world::entity_id> visible_order;               // the entity rows, for range selection
        rows.reserve(64);
        visible_order.reserve(64);

        if (filter_mode) {
            for (auto root : m_world->root_entities())
                collect_filtered_matches_rec(root, query, m_world, m_inspector, visible_order);
            for (const auto id : visible_order)
                rows.push_back({ id, NO_FOLDER, 0 });                    // filter mode: flat, no folders
        } else
            build_outliner_rows(rows, visible_order);

        if (filter_mode && visible_order.empty()) {
            UI::empty_state("No entities match", "Hover the filter field for the search syntax");
            ImGui::End();
            return;
        }

        // ---- world entity (pinned, never streamed) ----
        if (!filter_mode)
            render_world_row();

        // ---- draw rows: only the ones on screen ----
        // Drawing every row (image buttons, icons, name ellipsizing) cost 12-22 ms per frame with 4k-27k entities. The
        // clipper measures the first row and skips the rest of the off-screen ones with a single cursor jump
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step())
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                if (rows[row].folder != NO_FOLDER)
                    render_folder_row(rows[row].folder, rows[row].depth, row);
                else
                    render_outliner_row(rows[row].entity, rows[row].depth, row, visible_order, /*flat=*/filter_mode);
            }
        clipper.End();

        // ---- dragging near the top / bottom edge scrolls the list ----
        outliner_drag_autoscroll();

        // ---- the empty space below the rows: drop here to take something out of its folder ----
        if (!filter_mode) {
            const ImVec2 rest = ImGui::GetContentRegionAvail();
            if (rest.y > 6.f) {
                ImGui::Dummy(ImVec2(std::max(rest.x, 1.f), rest.y - 2.f));
                accept_outliner_drop(NO_FOLDER);
            }
        }

        // ---- blank-click deselect ----
        if (!filter_mode
            && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)
            && !ImGui::IsAnyItemHovered()
            && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            clear_selection();
            m_focused_folder = NO_FOLDER;
        }

        // ---- a press on a row of the selection that did not become a drag: select just that row ----
        if (m_pending_click.is_valid() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (!m_entity_drag_started && m_world->alive(m_pending_click)) {
                m_selected_entities = { m_pending_click };
                m_selection_anchor = m_pending_click;
            }
            m_pending_click = GLT::world::INVALID_ENTITY;
        }

        // ---- changes that move rows around (drops, folder removal): now that no row is being drawn ----
        for (auto& change : m_outliner_deferred)
            change();
        m_outliner_deferred.clear();

        const bool outliner_keys = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput;

        // ---- Ctrl+A: every entity that has a row right now (with a filter: the ones that match it) ----
        if (outliner_keys && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false) && !visible_order.empty()) {
            m_selected_entities = visible_order;
            if (!is_selected(m_selection_anchor))
                m_selection_anchor = m_selected_entities.front();
            m_focused_folder = NO_FOLDER;
        }

        // ---- Ctrl+F: bring the selection into view ----
        if (outliner_keys && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
            frame_selection();

        // ---- F2: rename the folder ----
        if (outliner_keys && ImGui::IsKeyPressed(ImGuiKey_F2, false) && m_layout.find(m_focused_folder))
            begin_rename_folder(m_focused_folder);

        // ---- Delete key: the selected entities, or (nothing selected) the folder. Its content moves up one level ----
        if (outliner_keys && ImGui::IsKeyPressed(ImGuiKey_Delete)) {

            if (!m_selected_entities.empty())
                delete_entities(m_selected_entities);
            else if (const outliner_folder* folder = m_layout.find(m_focused_folder)) {
                outliner_layout before = m_layout;
                const std::string name = std::format("Remove folder {}", folder->name);
                m_layout.remove_folder(m_focused_folder);
                m_focused_folder = NO_FOLDER;
                commit_layout_change(name, std::move(before));
            }
        }

        ImGui::End();
    }

    // ---- outliner (world row) -------------------------------------------------------------------------------------

    // World-scope components only on the world entity, entity-scope ones everywhere else
    bool world_viewport_window::component_fits(const GLT::world::component::descriptor& d, GLT::world::entity_id id) const {

        using GLT::world::component::scope;
        const bool on_world = m_world && m_world->is_world_entity(id);
        return GLT::world::component::allows(d.scope, on_world ? scope::world : scope::entity);
    }


    void world_viewport_window::render_world_row() {

        const auto world_id = m_world->world_entity();
        if (!world_id.is_valid() || !m_world->alive(world_id))
            return;

        ImGui::PushID("##world_entity_row");

        const bool selected = is_selected(world_id);
        const auto icon_data = icon_manager::get(icon_manager::icon::sun);
        ImGui::Image(icon_data.tex_ref, ImVec2(ROW_ICON_SIZE, ROW_ICON_SIZE), icon_data.uv0, icon_data.uv1);
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Header, GLT::imgui_config::get_action_color00_faded_ref());
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, GLT::imgui_config::get_action_color_gray_hover_ref());
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, GLT::imgui_config::get_action_color_gray_active_ref());
        const bool world_clicked = ImGui::Selectable("World", selected, ImGuiSelectableFlags_SpanAvailWidth);
        ImGui::PopStyleColor(3);
        accept_outliner_drop(NO_FOLDER);                        // dropping on the World row takes things out of their folder
        if (world_clicked) {
            m_selected_entities = { world_id };
            m_selection_anchor = world_id;
            m_focused_folder = NO_FOLDER;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("World settings: sky, sun (directional light), ambient, ...\n"
                "Never streamed, saved with the world. Rotate it to move the sun.");

        if (ImGui::BeginPopupContextItem("##world_ctx")) {

            m_selected_entities = { world_id };
            m_selection_anchor = world_id;

            if (m_inspector && ImGui::BeginMenu("Add Component")) {
                bool any = false;
                for (const auto& d : m_inspector->descriptors()) {
                    if (d.has(world_id) || !component_fits(d, world_id))
                        continue;
                    any = true;
                    if (ImGui::MenuItem(d.name.data())) {
                        flush_edits();
                        undo_system::world_change change(*m_world, std::span<const GLT::world::entity_id>(&world_id, 1));
                        m_inspector->add_component(world_id, d.hash);
                        change.commit(m_undo_stack, std::format("Add {} to World", d.name),
                            [this](const undo_system::world_state& state) { select_after_undo(state); });
                    }
                }
                if (!any)
                    ImGui::TextDisabled("nothing to add");
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        ImGui::PopID();

        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + 2.f), ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y + 2.f),
            ImGui::GetColorU32(ImGuiCol_Separator, 0.35f));
        ImGui::Dummy(ImVec2(0.f, 5.f));
    }

    // ---- outliner (per-row) ---------------------------------------------------------------------------------------

    void world_viewport_window::render_outliner_row(
        GLT::world::entity_id id,
        int depth,
        int row_index,
        const std::vector<GLT::world::entity_id>& visible_order,
        bool flat) {

        if (!m_world->alive(id))
            return;

        ImGui::PushID(static_cast<int>(id.index));

        const bool striped = (row_index % 2) != 0;            // zebra

        const std::string name = entity_display_name(id);
        const bool has_kids = m_world->has_children(id);
        const bool is_sel = is_selected(id);

        // Hidden in the editor (outliner_layout): by its own eye, or because a parent / its folder is hidden
        const bool own_hidden = m_layout.entity_hidden(id);
        const bool inherited_hidden = entity_hidden_inherited(id);
        const bool visible = !own_hidden && !inherited_hidden;

        const float text_h = ImGui::GetTextLineHeight();
        const float row_h = ImGui::GetTextLineHeightWithSpacing();
        const ImVec2 row_start = ImGui::GetCursorScreenPos();
        const float avail_w = ImGui::GetContentRegionAvail().x;
        const ImVec2 row_end = { row_start.x + avail_w, row_start.y + row_h };

        // ---- background ----
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool hovered = ImGui::IsMouseHoveringRect(row_start, row_end);

        if (is_sel) {
            dl->AddRectFilled(row_start, row_end, ImGui::GetColorU32(GLT::imgui_config::get_action_color00_faded_ref()), ROW_ROUNDING);
            if (id == primary_selection())
                dl->AddRectFilled(ImVec2(row_start.x, row_start.y + 3.f), ImVec2(row_start.x + 3.f, row_end.y - 3.f),
                    ImGui::GetColorU32(GLT::imgui_config::get_action_color00_hover_ref()), 1.5f);
        } else if (hovered)
        {
            ImVec4 hover_color = GLT::imgui_config::get_action_color_gray_hover_ref();
            hover_color.w *= HOVER_ALPHA;
            dl->AddRectFilled(row_start, row_end, ImGui::GetColorU32(hover_color), ROW_ROUNDING);
        }
        else if (striped)
            dl->AddRectFilled(row_start, row_end, ZEBRA_COLOR, ROW_ROUNDING);

        // ---- visibility toggle ----
        // Hides the entity in the editor only (not saved in the world; the mesh component has its own "Visible" for the
        // game). A hidden entity can still be selected: its outline keeps being drawn
        {
            const auto vis_data = icon_manager::get(own_hidden ? icon_manager::icon::hide : icon_manager::icon::show);

            ImGui::SetCursorScreenPos({ row_start.x, row_start.y });
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.10f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.20f));

            // hidden through a parent / folder: the eye is still "on" for the entity itself, shown faint
            const ImVec4 tint(1.f, 1.f, 1.f, (!own_hidden && inherited_hidden) ? 0.35f : 1.f);
            if (ImGui::ImageButton("##vis", vis_data.tex_ref, ImVec2(ROW_ICON_SIZE, ROW_ICON_SIZE), vis_data.uv0, vis_data.uv1,
                ImVec4(0, 0, 0, 0), tint)) {

                // The eye of a selected row acts on the whole selection
                if (is_sel && m_selected_entities.size() > 1)
                    set_entities_hidden(m_selected_entities, !own_hidden);
                else
                    set_entities_hidden(std::span<const GLT::world::entity_id>(&id, 1), !own_hidden);
            }

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip(own_hidden ? "Show in the editor"
                    : (inherited_hidden ? "Hidden because its parent or folder is hidden" : "Hide in the editor (it stays selectable)"));

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
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.f, 0.f, 0.f, 0.f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.f, 0.f, 0.f, 0.f));
            ImGui::TreeNodeEx("##node", flags);
            ImGui::PopStyleColor(2);
            clicked = ImGui::IsItemClicked();
            begin_entity_drag(id);
            content_x = ImGui::GetItemRectMin().x + ImGui::GetTreeNodeToLabelSpacing();

        } else {

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                | ImGuiTreeNodeFlags_NoTreePushOnOpen
                | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (!has_kids)
                flags |= ImGuiTreeNodeFlags_Leaf;

            const float indent_spacing = ImGui::GetStyle().IndentSpacing;
            ImGui::SetCursorScreenPos({ row_start.x + VIS_COL_WIDTH + depth * indent_spacing, row_start.y });
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.f, 0.f, 0.f, 0.f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.f, 0.f, 0.f, 0.f));
            open = ImGui::TreeNodeEx("##node", flags);
            ImGui::PopStyleColor(2);
            clicked = ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen();
            begin_entity_drag(id);
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

        if (clicked) {
            const ImGuiIO& io = ImGui::GetIO();
            m_entity_drag_started = false;
            if (is_sel && m_selected_entities.size() > 1 && !io.KeyCtrl && !io.KeyShift)
                m_pending_click = id;                           // maybe the start of dragging the selection: decided on release
            else
                handle_outliner_click(id, visible_order);
            m_focused_folder = NO_FOLDER;
        }

        // Right click anywhere on the row (it used to open only on the last thing drawn in it, a small icon)
        if (hovered && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            ImGui::OpenPopup("##row_ctx");
        render_entity_context_menu(id, !own_hidden);

        ImGui::SetCursorScreenPos({ row_start.x, row_end.y });
        ImGui::Dummy(ImVec2());
        (void)open;                                             // children are their own rows (see collect_visible_tree_order)

        ImGui::PopID();
    }

    // ---- outliner (context menu) ----------------------------------------------------------------------------------

    void world_viewport_window::render_entity_context_menu(GLT::world::entity_id id, bool visible) {

        if (!ImGui::BeginPopup("##row_ctx"))                    // opened by the row (render_outliner_row)
            return;

        // Right-click selects (unless already part of a multi-selection)
        if (!is_selected(id)) {
            m_selected_entities = { id };
            m_selection_anchor = id;
        }

        const auto on_applied = [this](const undo_system::world_state& state) { select_after_undo(state); };
        const std::span<const GLT::world::entity_id> just_this(&id, 1);

        if (ImGui::MenuItem("Add Child Entity")) {
            flush_edits();
            undo_system::world_change change(*m_world, {});
            const auto child = m_world->spawn();
            m_world->set_parent(child, id);
            change.created(child);
            change.commit(m_undo_stack, std::format("Add Child to {}", entity_display_name(id)), on_applied);
        }

        if (ImGui::MenuItem("Duplicate")) {
            flush_edits();
            undo_system::world_change change(*m_world, {});
            const auto copy = m_world->spawn();
            if (m_inspector)
                m_inspector->copy_components(id, copy);
            if (const auto p = m_world->parent_of(id); p.is_valid())
                m_world->set_parent(copy, p);
            change.created(copy);
            change.commit(m_undo_stack, std::format("Duplicate {}", entity_display_name(id)), on_applied);
            m_selected_entities = { copy };
            m_selection_anchor = copy;
        }

        if (ImGui::MenuItem(visible ? "Hide" : "Show"))        // the whole selection (the right-clicked row is part of it)
            set_entities_hidden(m_selected_entities, visible);

        render_move_to_folder_menu();

        if (ImGui::MenuItem("Delete", "Del"))
            delete_entities(m_selected_entities);

        if (m_inspector && ImGui::BeginMenu("Add Component")) {

            std::map<std::string_view, std::vector<const GLT::world::component::descriptor*>> by_cat;
            for (const auto& d : m_inspector->descriptors())
                by_cat[d.category].push_back(&d);

            for (auto& [cat, list] : by_cat) {
                if (ImGui::BeginMenu(std::string(cat).c_str())) {
                    for (const auto* d : list) {
                        if (d->has(id) || !component_fits(*d, id))
                            continue;
                        if (ImGui::MenuItem(d->name.data())) {
                            flush_edits();
                            undo_system::world_change change(*m_world, just_this);
                            m_inspector->add_component(id, d->hash);
                            change.commit(m_undo_stack, std::format("Add {}", d->name), on_applied);
                        }
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

    // ---- outliner (folders) ---------------------------------------------------------------------------------------
    //
    // Folders exist only in the editor (window/outliner_layout.h): they group the top-level entities of the list.
    // Entities below another entity stay with their parent

    void world_viewport_window::build_outliner_rows(std::vector<outliner_row>& rows, std::vector<GLT::world::entity_id>& entity_order) {

        const auto add_entity = [&](const auto& self, const GLT::world::entity_id id, const int depth) -> void {

            if (!m_world->alive(id))
                return;
            rows.push_back({ id, NO_FOLDER, depth });
            entity_order.push_back(id);
            if (!is_tree_node_open(id))
                return;
            for (const auto child : m_world->children_of(id))
                self(self, child, depth + 1);
        };

        m_folder_entity_counts.clear();
        const auto roots = m_world->root_entities();

        if (!m_layout.has_folders()) {                          // the usual case: no lookups at all
            for (const auto root : roots)
                add_entity(add_entity, root, 0);
            return;
        }

        std::unordered_map<folder_id, std::vector<GLT::world::entity_id>> in_folder;
        std::vector<GLT::world::entity_id> top_level;
        for (const auto root : roots) {
            if (const folder_id folder = m_layout.folder_of(root); folder != NO_FOLDER)
                in_folder[folder].push_back(root);
            else
                top_level.push_back(root);
        }
        for (const auto& [folder, entities] : in_folder)
            m_folder_entity_counts[folder] = static_cast<u32>(entities.size());

        const auto add_folders = [&](const auto& self, const folder_id parent, const int depth) -> void {

            for (const auto& folder : m_layout.folders()) {

                if (folder.parent != parent)
                    continue;
                rows.push_back({ {}, folder.id, depth });
                if (!folder.open)
                    continue;
                self(self, folder.id, depth + 1);
                if (const auto it = in_folder.find(folder.id); it != in_folder.end())
                    for (const auto entity : it->second)
                        add_entity(add_entity, entity, depth + 1);
            }
        };
        add_folders(add_folders, NO_FOLDER, 0);

        for (const auto entity : top_level)
            add_entity(add_entity, entity, 0);
    }


    void world_viewport_window::render_folder_row(const folder_id id, const int depth, const int row_index) {

        const outliner_folder* folder = m_layout.find(id);
        if (!folder)
            return;

        // Copies: what happens in this row may change the layout
        const std::string name = folder->name;
        const bool is_open = folder->open;
        const bool own_hidden = folder->hidden;
        const bool inherited_hidden = !own_hidden && m_layout.folder_effectively_hidden(folder->parent);

        bool has_content = m_folder_entity_counts.contains(id);
        for (const auto& other : m_layout.folders())
            has_content |= other.parent == id;
        const u32 entity_count = m_folder_entity_counts.contains(id) ? m_folder_entity_counts.at(id) : 0u;

        ImGui::PushID("##outliner_folder");
        ImGui::PushID(static_cast<int>(id));

        const f32 text_h = ImGui::GetTextLineHeight();
        const f32 row_h = ImGui::GetTextLineHeightWithSpacing();
        const ImVec2 row_start = ImGui::GetCursorScreenPos();
        const ImVec2 row_end = { row_start.x + ImGui::GetContentRegionAvail().x, row_start.y + row_h };

        // ---- background ----
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (m_focused_folder == id) {
            dl->AddRectFilled(row_start, row_end, ImGui::GetColorU32(GLT::imgui_config::get_action_color00_faded_ref()), ROW_ROUNDING);
            dl->AddRectFilled(ImVec2(row_start.x, row_start.y + 3.f), ImVec2(row_start.x + 3.f, row_end.y - 3.f),
                ImGui::GetColorU32(GLT::imgui_config::get_action_color00_hover_ref()), 1.5f);
        } else if (ImGui::IsMouseHoveringRect(row_start, row_end)) {
            ImVec4 hover_color = GLT::imgui_config::get_action_color_gray_hover_ref();
            hover_color.w *= HOVER_ALPHA;
            dl->AddRectFilled(row_start, row_end, ImGui::GetColorU32(hover_color), ROW_ROUNDING);
        } else if ((row_index % 2) != 0)
            dl->AddRectFilled(row_start, row_end, ZEBRA_COLOR, ROW_ROUNDING);

        // ---- visibility toggle: everything in the folder ----
        {
            const auto vis_data = icon_manager::get(own_hidden ? icon_manager::icon::hide : icon_manager::icon::show);

            ImGui::SetCursorScreenPos({ row_start.x, row_start.y });
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.10f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.20f));
            const ImVec4 tint(1.f, 1.f, 1.f, inherited_hidden ? 0.35f : 1.f);
            if (ImGui::ImageButton("##vis", vis_data.tex_ref, ImVec2(ROW_ICON_SIZE, ROW_ICON_SIZE), vis_data.uv0, vis_data.uv1,
                ImVec4(0, 0, 0, 0), tint)) {
                outliner_layout before = m_layout;
                m_layout.set_folder_hidden(id, !own_hidden);
                commit_layout_change(std::format("{} folder {}", own_hidden ? "Show" : "Hide", name), std::move(before));
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip(own_hidden ? "Show the entities in this folder"
                    : (inherited_hidden ? "Hidden because the folder it is in is hidden" : "Hide the entities in this folder (they stay selectable)"));
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
        }

        // ---- arrow + the row's item (click, drag, drop, context menu) ----
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth
            | ImGuiTreeNodeFlags_AllowOverlap;                  // the rename field lies on top of it
        if (!has_content)
            flags |= ImGuiTreeNodeFlags_Leaf;

        ImGui::SetCursorScreenPos({ row_start.x + VIS_COL_WIDTH + static_cast<f32>(depth) * ImGui::GetStyle().IndentSpacing, row_start.y });
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::SetNextItemOpen(is_open, ImGuiCond_Always);      // the layout owns the open state (it is saved with the folders)
        const bool open_now = ImGui::TreeNodeEx("##node", flags);
        ImGui::PopStyleColor(2);
        const bool toggled = ImGui::IsItemToggledOpen();
        const bool clicked = ImGui::IsItemClicked() && !toggled;
        const bool double_clicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !toggled;
        const f32 content_x = ImGui::GetItemRectMin().x + ImGui::GetTreeNodeToLabelSpacing();

        if (has_content && open_now != is_open)
            m_layout.set_open(id, open_now);

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
            ImGui::SetDragDropPayload(OUTLINER_FOLDER_PAYLOAD, &id, sizeof(id));
            ImGui::TextUnformatted(name.c_str());
            ImGui::EndDragDropSource();
        }
        accept_outliner_drop(id);
        render_folder_context_menu(id);

        // ---- icon ----
        {
            const auto icon = icon_manager::get((is_open && has_content) ? icon_manager::icon::folder_open : icon_manager::icon::folder);
            const ImVec2 icon_min(content_x, row_start.y + (text_h - ROW_ICON_SIZE) * 0.5f);
            dl->AddImage(icon.tex_ref, icon_min, ImVec2(icon_min.x + ROW_ICON_SIZE, icon_min.y + ROW_ICON_SIZE), icon.uv0, icon.uv1,
                ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, (own_hidden || inherited_hidden) ? 0.45f : 1.f)));
        }
        const f32 name_x = content_x + ROW_ICON_SIZE + 5.f;

        // ---- number of entities (right) ----
        const std::string count_text = entity_count > 0 ? std::to_string(entity_count) : std::string{};
        const f32 count_w = count_text.empty() ? 0.f : ImGui::CalcTextSize(count_text.c_str()).x + 8.f;

        // ---- name, or the field that renames it ----
        if (m_renaming_folder == id) {

            ImGui::SetCursorScreenPos({ name_x - 3.f, row_start.y - 1.f });
            ImGui::SetNextItemWidth(std::max(40.f, row_end.x - name_x - 4.f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.f, 1.f));
            if (m_rename_focus) {
                ImGui::SetKeyboardFocusHere();
                m_rename_focus = false;
            }
            const bool entered = ImGui::InputText("##rename", m_rename_buffer, sizeof(m_rename_buffer),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            ImGui::PopStyleVar();

            const bool left = ImGui::IsItemDeactivated();
            const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
            if (entered || left) {
                if (!cancelled && m_rename_buffer[0] != '\0' && name != m_rename_buffer) {
                    outliner_layout before = m_layout;
                    if (m_layout.rename_folder(id, m_rename_buffer))
                        commit_layout_change(std::format("Rename folder {}", name), std::move(before));
                }
                m_renaming_folder = NO_FOLDER;
            }

        } else {

            ImGui::SetCursorScreenPos({ name_x, row_start.y });
            if (own_hidden || inherited_hidden)
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextUnformatted(ellipsize_to_fit(name, row_end.x - name_x - count_w - 4.f).c_str());
            if (own_hidden || inherited_hidden)
                ImGui::PopStyleColor();

            if (!count_text.empty()) {
                ImGui::SetCursorScreenPos({ row_end.x - count_w + 2.f, row_start.y });
                ImGui::TextDisabled("%s", count_text.c_str());
            }
        }

        if (clicked) {
            m_focused_folder = id;
            if (!ImGui::GetIO().KeyCtrl)
                clear_selection();                              // Ctrl keeps the entities selected, e.g. to drop them in afterwards
        }
        if (double_clicked)
            begin_rename_folder(id);

        ImGui::SetCursorScreenPos({ row_start.x, row_end.y });
        ImGui::Dummy(ImVec2());

        ImGui::PopID();
        ImGui::PopID();
    }


    void world_viewport_window::render_folder_context_menu(const folder_id id) {

        if (!ImGui::BeginPopupContextItem("##folder_ctx"))
            return;

        m_focused_folder = id;
        const outliner_folder* folder = m_layout.find(id);
        const std::string name = folder ? folder->name : std::string{};
        const bool hidden = folder && folder->hidden;

        if (ImGui::MenuItem("New Folder Inside"))
            m_outliner_deferred.push_back([this, id]() { create_folder(id, false); });

        if (ImGui::MenuItem("Rename", "F2"))
            begin_rename_folder(id);

        if (ImGui::MenuItem("Select Entities")) {
            m_selected_entities = folder_entities(id);
            m_selection_anchor = m_selected_entities.empty() ? GLT::world::INVALID_ENTITY : m_selected_entities.front();
        }

        if (ImGui::MenuItem("Add Selected Entities", nullptr, false, !m_selected_entities.empty()))
            m_outliner_deferred.push_back([this, id]() { move_entities_to_folder(m_selected_entities, id); });

        if (ImGui::MenuItem(hidden ? "Show" : "Hide")) {
            outliner_layout before = m_layout;
            m_layout.set_folder_hidden(id, !hidden);
            commit_layout_change(std::format("{} folder {}", hidden ? "Show" : "Hide", name), std::move(before));
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Remove Folder", "Del"))
            m_outliner_deferred.push_back([this, id, name]() {
                outliner_layout before = m_layout;
                m_layout.remove_folder(id);
                if (m_focused_folder == id)
                    m_focused_folder = NO_FOLDER;
                commit_layout_change(std::format("Remove folder {}", name), std::move(before));
            });
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("The entities and folders in it move up one level");

        if (ImGui::MenuItem("Delete Folder and Entities"))
            m_outliner_deferred.push_back([this, id, name]() {

                const std::vector<GLT::world::entity_id> entities = folder_entities(id);
                std::vector<folder_id> folders;
                m_layout.collect_subtree(id, folders);

                if (!entities.empty())
                    delete_entities(entities);                  // its own undo step (the world changes)

                outliner_layout before = m_layout;
                for (auto it = folders.rbegin(); it != folders.rend(); ++it)
                    m_layout.remove_folder(*it);
                if (std::find(folders.begin(), folders.end(), m_focused_folder) != folders.end())
                    m_focused_folder = NO_FOLDER;
                commit_layout_change(std::format("Delete folder {}", name), std::move(before));
            });

        ImGui::EndPopup();
    }


    // "Move to Folder" of the entity context menu. Only top-level entities can be in a folder
    void world_viewport_window::render_move_to_folder_menu() {

        const bool any_root = std::any_of(m_selected_entities.begin(), m_selected_entities.end(), [this](const GLT::world::entity_id id) {
            return m_world->alive(id) && !m_world->is_world_entity(id) && !m_world->parent_of(id).is_valid();
        });

        if (!ImGui::BeginMenu("Move to Folder", any_root)) {
            if (!any_root && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Entities below another entity stay with their parent");
            return;
        }

        if (ImGui::MenuItem("New Folder..."))
            m_outliner_deferred.push_back([this]() { create_folder(NO_FOLDER, true); });

        if (ImGui::MenuItem("No Folder"))
            m_outliner_deferred.push_back([this]() { move_entities_to_folder(m_selected_entities, NO_FOLDER); });

        if (m_layout.has_folders())
            ImGui::Separator();

        // The folder tree, indented
        const auto list = [this](const auto& self, const folder_id parent, const int depth) -> void {
            for (const auto& folder : m_layout.folders()) {
                if (folder.parent != parent)
                    continue;
                ImGui::PushID(static_cast<int>(folder.id));
                const std::string label = std::string(static_cast<size_t>(depth) * 3, ' ') + folder.name;
                if (ImGui::MenuItem(label.c_str()))
                    m_outliner_deferred.push_back([this, target = folder.id]() { move_entities_to_folder(m_selected_entities, target); });
                ImGui::PopID();
                self(self, folder.id, depth + 1);
            }
        };
        list(list, NO_FOLDER, 0);

        ImGui::EndMenu();
    }


    void world_viewport_window::begin_entity_drag(const GLT::world::entity_id id) {

        if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers))
            return;

        // A drag that starts on a selected row carries the whole selection
        if (ImGui::GetDragDropPayload() == nullptr || !ImGui::GetDragDropPayload()->IsDataType(OUTLINER_ENTITY_PAYLOAD)) {
            m_dragged_entities = is_selected(id) ? m_selected_entities : std::vector<GLT::world::entity_id>{ id };
            m_entity_drag_started = true;
        }
        const u32 count = static_cast<u32>(m_dragged_entities.size());
        ImGui::SetDragDropPayload(OUTLINER_ENTITY_PAYLOAD, &count, sizeof(count));
        if (count == 1)
            ImGui::TextUnformatted(entity_display_name(m_dragged_entities.front()).c_str());
        else
            ImGui::Text("%u entities", count);
        ImGui::EndDragDropSource();
    }


    void world_viewport_window::accept_outliner_drop(const folder_id target) {

        if (!ImGui::BeginDragDropTarget())
            return;

        if (ImGui::AcceptDragDropPayload(OUTLINER_ENTITY_PAYLOAD))
            m_outliner_deferred.push_back([this, target]() { move_entities_to_folder(m_dragged_entities, target); });

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(OUTLINER_FOLDER_PAYLOAD); payload && payload->DataSize == sizeof(folder_id)) {
            folder_id moved = NO_FOLDER;
            std::memcpy(&moved, payload->Data, sizeof(moved));
            m_outliner_deferred.push_back([this, moved, target]() {
                const outliner_folder* folder = m_layout.find(moved);
                if (!folder)
                    return;
                const std::string name = folder->name;
                outliner_layout before = m_layout;
                if (m_layout.move_folder(moved, target))        // refuses to put a folder into itself
                    commit_layout_change(std::format("Move folder {}", name), std::move(before));
            });
        }

        ImGui::EndDragDropTarget();
    }


    void world_viewport_window::move_entities_to_folder(const std::vector<GLT::world::entity_id>& ids, const folder_id folder) {

        if (!m_world)
            return;

        outliner_layout before = m_layout;
        u32 moved = 0;
        for (const auto id : ids) {
            if (!m_world->alive(id) || m_world->is_world_entity(id) || m_world->parent_of(id).is_valid())
                continue;                                       // children stay with their parent
            m_layout.assign(id, folder);
            ++moved;
        }
        if (moved == 0)
            return;

        const outliner_folder* target = m_layout.find(folder);
        commit_layout_change(target ? std::format("Move {} to folder {}", moved == 1 ? std::string("entity") : std::format("{} entities", moved), target->name)
            : std::string("Move out of folder"), std::move(before));
    }


    folder_id world_viewport_window::create_folder(const folder_id parent, const bool with_selection) {

        outliner_layout before = m_layout;
        const folder_id id = m_layout.add_folder("New Folder", parent);
        if (with_selection && m_world)
            for (const auto entity : m_selected_entities)
                if (m_world->alive(entity) && !m_world->is_world_entity(entity) && !m_world->parent_of(entity).is_valid())
                    m_layout.assign(entity, id);
        commit_layout_change("New folder", std::move(before));

        m_focused_folder = id;
        begin_rename_folder(id);                                // type the name right away
        return id;
    }


    void world_viewport_window::begin_rename_folder(const folder_id id) {

        const outliner_folder* folder = m_layout.find(id);
        if (!folder)
            return;
        m_renaming_folder = id;
        m_rename_focus = true;
        std::snprintf(m_rename_buffer, sizeof(m_rename_buffer), "%s", folder->name.c_str());
    }


    std::vector<GLT::world::entity_id> world_viewport_window::folder_entities(const folder_id id) const {

        std::vector<folder_id> folders;
        m_layout.collect_subtree(id, folders);

        std::vector<GLT::world::entity_id> out;
        if (!m_world || folders.empty())
            return out;
        for (const auto& [entity, folder] : m_layout.assignments())
            if (std::find(folders.begin(), folders.end(), folder) != folders.end() && m_world->alive(entity))
                out.push_back(entity);
        std::sort(out.begin(), out.end());                      // the map has no order
        return out;
    }

    // ---- outliner (hiding, saving the folders) ----------------------------------------------------------------------

    void world_viewport_window::set_entities_hidden(const std::span<const GLT::world::entity_id> ids, const bool hidden) {

        if (!m_world || ids.empty())
            return;

        outliner_layout before = m_layout;
        std::string name;
        for (const auto id : ids) {
            if (m_world->is_world_entity(id))
                continue;                                       // sun and sky
            if (name.empty())
                name = entity_display_name(id);
            m_layout.set_entity_hidden(id, hidden);
        }
        commit_layout_change(std::format("{} {}", hidden ? "Hide" : "Show", ids.size() == 1 ? name : std::format("{} entities", ids.size())),
            std::move(before));
    }


    bool world_viewport_window::entity_hidden_inherited(const GLT::world::entity_id id) const {

        if (!m_world || m_layout.empty())
            return false;

        // Up the hierarchy; the top-most entity is the one that can sit in a folder
        GLT::world::entity_id top = id;
        for (u32 guard = 0; guard < 256; ++guard) {
            const auto parent = m_world->parent_of(top);
            if (!parent.is_valid())
                break;
            if (m_layout.entity_hidden(parent))
                return true;
            top = parent;
        }
        return m_layout.folder_effectively_hidden(m_layout.folder_of(top));
    }


    // The world does the actual hiding (i_world::set_hidden: the entity and what is below it, meshes and lights). It is
    // told which entities: the ones with a closed eye and the ones in a hidden folder
    void world_viewport_window::sync_hidden_to_world() {

        if (!m_world)
            return;

        std::unordered_set<GLT::world::entity_id> wanted;
        m_layout.collect_hidden(wanted);

        for (const auto id : m_applied_hidden)
            if (!wanted.contains(id))
                m_world->set_hidden(id, false);
        for (const auto id : wanted)
            if (!m_applied_hidden.contains(id))
                m_world->set_hidden(id, true);
        m_applied_hidden = std::move(wanted);
    }


    void world_viewport_window::commit_layout_change(std::string name, outliner_layout before) {

        if (before.same_content(m_layout))
            return;

        // Whole states, like the world steps: undo and redo just put one of them back
        auto states = std::make_shared<std::pair<outliner_layout, outliner_layout>>(std::move(before), m_layout);
        const auto put_back = [this](const outliner_layout& state) {
            m_layout = state;
            m_layout_unsynced = true;                           // the revision counter went back with the state: say so explicitly
            m_layout_unsaved = true;
            m_layout_seen_revision = m_layout.revision();
            if (!m_layout.find(m_focused_folder))
                m_focused_folder = NO_FOLDER;
            m_renaming_folder = NO_FOLDER;
        };

        undo_system::step step;
        step.name = std::move(name);
        step.undo = [states, put_back]() { put_back(states->first); };
        step.redo = [states, put_back]() { put_back(states->second); };
        m_undo_stack.push(std::move(step));
    }


    void world_viewport_window::update_outliner_layout() {

        if (m_layout.revision() != m_layout_seen_revision) {
            m_layout_seen_revision = m_layout.revision();
            m_layout_unsynced = true;
            m_layout_unsaved = true;
        }

        if (m_layout_unsynced && m_world) {
            sync_hidden_to_world();
            m_layout_unsynced = false;
        }

        // Not while something is being dragged or typed (opening folders one after the other would write every time)
        if (m_layout_unsaved && !ImGui::IsAnyItemActive() && m_renaming_folder == NO_FOLDER)
            save_outliner_layout();
    }


    std::filesystem::path world_viewport_window::outliner_layout_path() const {

        const std::filesystem::path project = GLT::application::get().get_project_path();
        auto registry = GLT::asset::registry::get_ref();
        if (!m_world || !registry || project.empty() || m_world->asset_handle() == INVALID_HANDLE)
            return {};

        const u64 world_id = static_cast<u64>(registry->info(m_world->asset_handle()).id);
        if (world_id == 0)
            return {};
        return project / GLT::config::METADATA_DIR / OUTLINER_LAYOUT_DIR / std::format("{:016x}.txt", world_id);
    }


    void world_viewport_window::load_outliner_layout() {

        m_layout.clear();
        m_applied_hidden.clear();                               // a new world object: nothing is hidden in it yet
        m_focused_folder = NO_FOLDER;
        m_renaming_folder = NO_FOLDER;
        m_dragged_entities.clear();
        m_outliner_deferred.clear();

        if (const auto path = outliner_layout_path(); !path.empty()) {
            std::error_code error{};
            if (GLT::vfs::exists(path, error) && !error) {
                const std::string text = GLT::vfs::read_text_file(path, error);
                if (!error)
                    m_layout.from_text(text);
                else
                    LOG(warn, "outliner: could not read [{}] ({})", path.generic_string(), error.message())
            }
        }

        m_layout_seen_revision = m_layout.revision();
        m_layout_unsynced = true;
        m_layout_unsaved = false;
    }


    void world_viewport_window::save_outliner_layout() {

        const auto path = outliner_layout_path();
        if (path.empty())
            return;                                             // a world that was never saved: tried again once it has a file

        m_layout_unsaved = false;
        std::error_code error{};
        if (m_layout.empty()) {                                 // nothing to remember: no file
            if (GLT::vfs::exists(path, error) && !error)
                std::filesystem::remove(path, error);
            return;
        }

        std::filesystem::create_directories(path.parent_path(), error);
        if (!GLT::vfs::write_text_file(path, m_layout.to_text()))
            LOG(error, "outliner: could not write [{}]", path.generic_string())
    }

    // ---- outliner (hierarchy helpers) -----------------------------------------------------------------------------

    // Rows push only their own id (no nesting), so the open state read here is exactly the one the row's tree node writes
    void world_viewport_window::collect_visible_tree_order(GLT::world::entity_id id, int depth,
        std::vector<GLT::world::entity_id>& out, std::vector<int>& depths) const {

        if (!m_world->alive(id))
            return;

        out.push_back(id);
        depths.push_back(depth);

        if (!is_tree_node_open(id))
            return;

        for (auto c : m_world->children_of(id))
            collect_visible_tree_order(c, depth + 1, out, depths);
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


    std::optional<icon_manager::icon_type> world_viewport_window::component_icon(std::string_view component_name) {

        if (component_name == "transform")         return icon_manager::icon::transfrom_translation;
        if (component_name == "mesh")              return icon_manager::icon::mesh_asset;
        if (component_name == "light")             return icon_manager::icon::point_light;
        if (component_name == "camera")            return icon_manager::icon::camera;
        if (component_name == "audio source")      return icon_manager::icon::audio;
        if (component_name == "hierarchy")         return icon_manager::icon::relation;
        if (component_name == "sky")               return icon_manager::icon::world;
        if (component_name == "ambient_light")     return icon_manager::icon::sun;
        if (component_name == "name")              return icon_manager::icon::file;
        return std::nullopt;
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
        if (has("audio source"))
            icons.push_back(icon_manager::icon::audio);

        return icons;
    }

    // ---- opening worlds -------------------------------------------------------------------------------------------

    // Accepts exactly one .glt_world from the content browser. Other drags (meshes, materials, several files) are not
    // taken, so the image stays free for future drop targets
    void world_viewport_window::handle_viewport_world_drop() {

        const ImGuiPayload* dragged = ImGui::GetDragDropPayload();
        if (!dragged || !dragged->IsDataType(GLT::UI::ASSET_DRAG_PAYLOAD))
            return;

        const auto paths = content_browser_window::parse_drag_payload(dragged);
        if (paths.size() != 1 || GLT::asset::extension_to_type(paths.front().extension().string()) != GLT::asset::core_types::world)
            return;

        if (!ImGui::BeginDragDropTarget())
            return;

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(GLT::UI::ASSET_DRAG_PAYLOAD, ImGuiDragDropFlags_AcceptBeforeDelivery)) {
            if (payload->IsDelivery())
                request_open_world(paths.front());
            else
                ImGui::SetTooltip("Open world  %s", paths.front().stem().string().c_str());
        }
        ImGui::EndDragDropTarget();
    }


    void world_viewport_window::request_open_world(const std::filesystem::path& path) {

        if (!GLT::editor::editor_layer::is_simulation_idle()) {
            GLT::event_bus::post(GLT::notification_event("Open world", "Stop the simulation before opening another world",
                GLT::logger::severity::warn));
            return;
        }

        // already open: nothing to do (dropping it again is not a "revert")
        if (m_world && m_world->asset()) {
            const auto& current = GLT::asset::registry::get_ref()->info(m_world->asset().get()).virtual_path;
            if (GLT::project::to_content_relative(path) == current.lexically_normal()) {
                GLT::event_bus::post(GLT::notification_event("Open world", std::format("[{}] is already open", path.stem().string()),
                    GLT::logger::severity::info));
                return;
            }
        }

        flush_edits();                                          // an open property / gizmo edit becomes an undo step first
        if (m_world && !m_undo_stack.is_clean()) {
            m_world_awaiting_confirm = path;
            m_open_world_popup_requested = true;
            return;
        }
        m_world_to_open = path;
    }


    void world_viewport_window::render_open_world_popup() {

        constexpr const char* POPUP = "Unsaved changes##open_world";
        if (m_open_world_popup_requested) {
            ImGui::OpenPopup(POPUP);
            m_open_world_popup_requested = false;
        }

        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal(POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        const std::string current = (m_world && m_world->asset())
            ? GLT::asset::registry::get_ref()->info(m_world->asset().get()).name : std::string("the current world");
        ImGui::Text("%s has unsaved changes.", current.c_str());
        ImGui::Text("Open %s anyway?", m_world_awaiting_confirm.stem().string().c_str());
        ImGui::Dummy(ImVec2(0.f, 4.f));

        auto close = [this]() {
            m_world_awaiting_confirm.clear();
            ImGui::CloseCurrentPopup();
        };

        if (ImGui::Button("Save and open")) {
            if (auto result = m_world ? m_world->save() : std::expected<void, GLT::asset::load_error>{}; result) {
                m_undo_stack.mark_clean();
                m_world_to_open = m_world_awaiting_confirm;
            } else
                GLT::event_bus::post(GLT::notification_event("Open world", std::format("Saving failed [{}], world not switched",
                    GLT::util::enum_to_string(result.error())), GLT::logger::severity::warn));
            close();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard changes and open")) {
            m_world_to_open = m_world_awaiting_confirm;
            close();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
            close();

        ImGui::EndPopup();
    }


    void world_viewport_window::open_requested_world() {

        if (m_world_to_open.empty())
            return;
        const std::filesystem::path path = std::exchange(m_world_to_open, {});

        if (!GLT::editor::editor_layer::is_simulation_idle())   // started playing while the popup was open
            return;

        auto* game_layer = GLT::application::get().get_game_layer_ptr();
        auto* session = game_layer ? game_layer->get_primary_session() : nullptr;
        auto registry = GLT::asset::registry::get_ref();
        VALIDATE(session && registry, return, "", "cannot open world [{}]: no primary session or asset registry", (path.string()))

        auto asset = registry->load(path);
        if (!asset) {
            GLT::event_bus::post(GLT::notification_event("Open world", std::format("Failed to load [{}]: {}", path.stem().string(),
                GLT::util::enum_to_string(asset.error())), GLT::logger::severity::error));
            return;
        }

        // load_world (not load_world_from_path): it also rebinds scripts and runs world generators, like the PIE windows do
        if (auto result = session->load_world(std::move(*asset)); !result) {
            GLT::event_bus::post(GLT::notification_event("Open world", std::format("Failed to open [{}]: {}", path.stem().string(),
                GLT::util::enum_to_string(result.error())), GLT::logger::severity::error));
            return;
        }
        LOG(info, "opened world [{}]", path.string())
        // update() continues and sees the new world: selection, undo history and trackers are reset there
    }

    // ---- saving ---------------------------------------------------------------------------------------------------

    void world_viewport_window::on_save_event(const GLT::save_event& /*event*/) {

        GLT::application::get().get_project_ref().serialize_projects_data(GLT::serializer::option::save);

        auto* session = GLT::application::get().get_game_layer_ptr()->get_primary_session();
        VALIDATE(session, return, "", "No primary session");

        auto* world = session->get_world();
        VALIDATE(world, return, "", "Failed to get world");

        if (session->is_simulating()) {                 // playing in the viewport: the world is not the edited one right now
            GLT::event_bus::post(GLT::notification_event("Save", "Stop playing to save the world (Stop restores it)",
                GLT::logger::severity::warn));
            return;
        }

        if (auto result = world->save(); result) {
            if (world == m_world)
                m_undo_stack.mark_clean();
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

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
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

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 10.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.08f, 0.08f, 0.08f, 0.94f));

        ImGui::SetNextWindowPos(ImVec2(btn_min.x, btn_max.y + 4.f), ImGuiCond_Appearing);
        ImGui::SetNextWindowSizeConstraints(ImVec2(380.f, 0.f), ImVec2(380.f, 640.f));

        bool open = false;
        if (ImGui::BeginPopup("##render_settings_popup", ImGuiWindowFlags_NoMove)) {

            open = true;

            // ---- camera: view distance of the camera this viewport looks through ---------------------------------------
            if (auto* scene = m_world ? m_world->as<GLT::world::i_world_scene>() : nullptr) {
                const GLT::world::entity_id camera = scene->get_active_camera();
                GLT::world::camera_snapshot view{};
                if (camera.is_valid() && scene->get_camera_view(camera, view)) {

                    ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::bold));
                    ImGui::TextUnformatted("Camera");
                    ImGui::PopFont();
                    ImGui::TextDisabled("The camera this viewport looks through (saved with it)");
                    ImGui::Dummy(ImVec2(0.f, 2.f));

                    f32 far_plane = view.far_plane;
                    f32 near_plane = view.near_plane;
                    UI::begin_properties(UI::property_layout::compact);
                    bool clip = UI::property_drag("View distance", far_plane, std::max(far_plane * 0.01f, 1.f), 10.f, 1.0e6f, "%.0f m",
                        "Farther things are not drawn (the camera's far plane)");
                    clip |= UI::property_drag("Near plane", near_plane, 0.001f, 0.001f, 100.f, "%.3f m",
                        "Closer things are cut off. Raise it a little for long view distances: depth precision depends on far / near");
                    UI::end_properties();
                    if (clip)
                        scene->set_camera_clip(camera, near_plane, far_plane);
                    if (far_plane / std::max(near_plane, 0.001f) > 2.0e5f)
                        ImGui::TextColored(ImVec4(1.f, 0.72f, 0.3f, 1.f), "far / near > 200000: distant surfaces may flicker (raise the near plane)");
                    ImGui::Dummy(ImVec2(0.f, 6.f));
                }
            }

            ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::bold));
            ImGui::TextUnformatted("Renderer");
            ImGui::PopFont();
            ImGui::TextDisabled("Applies to every viewport");
            ImGui::Dummy(ImVec2(0.f, 4.f));

            // One card per category ("General" for members without one)
            UI::begin_properties(UI::property_layout::compact);
            std::string_view current_cat{};
            bool card_open = false;
            bool card_visible = false;
            bool changed = false;

            GLT::reflect::for_each_member(*type_data, data,
                [&](const GLT::reflect::member_descriptor& member_descriptor, void* ptr) -> bool {

                    if (member_descriptor.flags & (u32)GLT::reflect::member_flags::skip)
                        return true;

                    // "Page|Section" (sections of the Engine Settings window): one card per page here
                    std::string_view cat = (member_descriptor.category && member_descriptor.category[0])
                        ? std::string_view(member_descriptor.category) : std::string_view("General");
                    cat = cat.substr(0, cat.find('|'));

                    if (!card_open || cat != current_cat) {
                        if (card_open)
                            UI::end_card();
                        current_cat = cat;
                        const std::string title(cat);
                        card_visible = UI::begin_card(title.c_str(), UI::card_options{ .collapsible = true });
                        card_open = true;
                    }

                    if (card_visible && draw_reflected_setting(member_descriptor, ptr))
                        changed = true;

                    return true;
                });

            if (card_open)
                UI::end_card();
            UI::end_properties();

            if (changed)
                m_renderer->on_settings_changed();

            m_render_settings_edited |= changed;

            ImGui::EndPopup();
        }

        // These are engine settings: keep what was changed here as a change on this computer (Edit > Engine Settings
        // shows it and can put it back). Once the user lets go, not on every frame of a drag
        if (m_render_settings_edited && !ImGui::IsAnyItemActive()) {
            m_render_settings_edited = false;
            if (type_data->name)
                GLT::engine_settings::adopt_live(type_data->name);
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

        const std::string label_str = UI::pretty_name(label);
        const char* text = label_str.c_str();
        const char* desc = (md.tooltip && md.tooltip[0]) ? md.tooltip : nullptr;

        switch (md.kind) {

            case GLT::reflect::type_kind::boolean:
                return UI::property_toggle(text, *static_cast<bool*>(ptr), desc);

            case GLT::reflect::type_kind::u32: {
                auto& value = *static_cast<u32*>(ptr);
                i32 buffer = static_cast<i32>(value);
                const bool changed = has_range
                    ? UI::property_slider(text, buffer, static_cast<i32>(lo), static_cast<i32>(hi), "%d", desc)
                    : UI::property_custom(text, [&](f32 width) {
                        ImGui::SetNextItemWidth(width);
                        return ImGui::DragInt("##value", &buffer, 0.2f, 0, INT32_MAX);
                    }, desc);
                if (changed)
                    value = static_cast<u32>(std::max(0, buffer));
                return changed;
            }

            case GLT::reflect::type_kind::i32: {
                auto& value = *static_cast<i32*>(ptr);
                return has_range
                    ? UI::property_slider(text, value, static_cast<i32>(lo), static_cast<i32>(hi), "%d", desc)
                    : UI::property_custom(text, [&](f32 width) {
                        ImGui::SetNextItemWidth(width);
                        return ImGui::DragInt("##value", &value, 0.2f);
                    }, desc);
            }

            case GLT::reflect::type_kind::f32: {
                auto& value = *static_cast<f32*>(ptr);
                return has_range
                    ? UI::property_slider(text, value, lo, hi, "%.3f", desc)
                    : UI::property_drag(text, value, 0.01f, 0.f, 0.f, "%.3f", desc);
            }

            case GLT::reflect::type_kind::f64: {
                auto& value = *static_cast<f64*>(ptr);
                f32 buffer = static_cast<f32>(value);
                const bool changed = has_range
                    ? UI::property_slider(text, buffer, lo, hi, "%.3f", desc)
                    : UI::property_drag(text, buffer, 0.01f, 0.f, 0.f, "%.3f", desc);
                if (changed)
                    value = static_cast<f64>(buffer);
                return changed;
            }

            default:
                UI::property_value(text, "(unsupported type)");
                return false;
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

        // The gizmo works in world space: the entity's transform is relative to its parent, so put the parent's world
        // transform in front of it (and take it out again after the edit)
        const glm::mat4 parent_world = entity_world_matrix(m_world->parent_of(id));
        const glm::mat4 local_model = glm::translate(glm::mat4(1.f), position)
            * glm::mat4_cast(rotation)
            * glm::scale(glm::mat4(1.f), scale);
        glm::mat4 model = parent_world * local_model;
        const glm::mat4 world_before = model;

        float* snap_ptr = nullptr;
        if (gizmo_cfg().snap) {
            switch (m_gizmo_operation) {
                case ImGuizmo::TRANSLATE:
                    snap_ptr = &gizmo_cfg().snap_translate.x;
                    break;
                case ImGuizmo::ROTATE:
                    snap_ptr = &gizmo_cfg().snap_rotate;
                    break;
                case ImGuizmo::SCALE:
                    snap_ptr = &gizmo_cfg().snap_scale;
                    break;
                default: break;
            }
        }

        ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), m_gizmo_operation,
            gizmo_cfg().space == gizmo_space::local ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
            glm::value_ptr(model), nullptr, snap_ptr);

        if (!ImGuizmo::IsUsing())
            return;

        // Back into the parent's space
        const glm::mat4 new_local = glm::inverse(parent_world) * model;
        const glm::vec3 new_pos = glm::vec3(new_local[3]);
        const glm::vec3 new_scale = glm::vec3(
            glm::length(glm::vec3(new_local[0])),
            glm::length(glm::vec3(new_local[1])),
            glm::length(glm::vec3(new_local[2])));
        const glm::mat3 new_basis(
            std::abs(new_scale.x) > 1e-8f ? glm::vec3(new_local[0]) / new_scale.x : glm::vec3(1.f, 0.f, 0.f),
            std::abs(new_scale.y) > 1e-8f ? glm::vec3(new_local[1]) / new_scale.y : glm::vec3(0.f, 1.f, 0.f),
            std::abs(new_scale.z) > 1e-8f ? glm::vec3(new_local[2]) / new_scale.z : glm::vec3(0.f, 0.f, 1.f));
        const glm::quat new_rot = glm::normalize(glm::quat_cast(new_basis));

        // The edit in world space, for the other selected entities (each has its own parent)
        const glm::vec3 world_delta = glm::vec3(model[3]) - glm::vec3(world_before[3]);
        const glm::quat world_rot_before = glm::normalize(glm::quat_cast(glm::mat3(
            glm::normalize(glm::vec3(world_before[0])), glm::normalize(glm::vec3(world_before[1])), glm::normalize(glm::vec3(world_before[2])))));
        const glm::quat world_rot_after = glm::normalize(glm::quat_cast(glm::mat3(
            glm::normalize(glm::vec3(model[0])), glm::normalize(glm::vec3(model[1])), glm::normalize(glm::vec3(model[2])))));
        const glm::quat world_turn = world_rot_after * glm::inverse(world_rot_before);

        // The other selected entities get the same change (m_gizmo_targets: the primary one first, then the others;
        // entities below a selected one are not in it, they follow their parent)
        const std::span<const GLT::world::entity_id> others = m_gizmo_targets.size() > 1
            ? std::span<const GLT::world::entity_id>(m_gizmo_targets).subspan(1) : std::span<const GLT::world::entity_id>{};

        // translate_local() moves along the entity's own axes, inside its parent's space
        const glm::vec3 parent_delta = new_pos - position;
        if (glm::length(parent_delta) > 1e-6f) {
            m_world->translate_local(id, glm::inverse(rotation) * parent_delta);
            for (const auto other : others) {                   // the same distance in the same world direction
                const glm::mat3 other_parent(entity_world_matrix(m_world->parent_of(other)));
                const glm::vec3 delta_in_parent = glm::inverse(other_parent) * world_delta;
                m_world->translate_local(other, glm::inverse(m_world->get_local_rotation_q(other)) * delta_in_parent);
            }
        }

        if (glm::abs(glm::dot(rotation, new_rot)) < 0.9999f) {
            m_world->set_local_rotation_q(id, new_rot);
            for (const auto other : others) {                   // each one turns in place by the same world rotation
                const glm::mat4 other_parent = entity_world_matrix(m_world->parent_of(other));
                const glm::quat parent_rot = glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(other_parent[0])),
                    glm::normalize(glm::vec3(other_parent[1])), glm::normalize(glm::vec3(other_parent[2])))));
                const glm::quat turn_in_parent = glm::inverse(parent_rot) * world_turn * parent_rot;
                m_world->set_local_rotation_q(other, glm::normalize(turn_in_parent * m_world->get_local_rotation_q(other)));
            }
        }

        if (glm::distance(scale, new_scale) > 1e-6f) {
            m_world->set_local_scale(id, new_scale);
            const glm::vec3 factor(
                std::abs(scale.x) > 1e-6f ? new_scale.x / scale.x : 1.f,
                std::abs(scale.y) > 1e-6f ? new_scale.y / scale.y : 1.f,
                std::abs(scale.z) > 1e-6f ? new_scale.z / scale.z : 1.f);
            for (const auto other : others)                     // the same factor per axis
                m_world->set_local_scale(other, m_world->get_local_scale(other) * factor);
        }
    }


    glm::mat4 world_viewport_window::entity_world_matrix(const GLT::world::entity_id id) const {

        if (!m_world || !id.is_valid() || !m_world->alive(id))
            return glm::mat4(1.f);
        return m_world->get_world_transform(id);
    }


    // What the gizmo moves: the primary selection (its transform is the one the gizmo shows) and the other selected
    // entities. An entity below another selected one is left out: it moves with its parent already
    void world_viewport_window::collect_gizmo_targets(const GLT::world::entity_id primary) {

        m_gizmo_targets.clear();
        if (!m_world || !primary.is_valid() || !m_world->alive(primary))
            return;
        m_gizmo_targets.push_back(primary);
        if (m_selected_entities.size() < 2 || m_world->is_world_entity(primary))
            return;

        const std::unordered_set<GLT::world::entity_id> selected(m_selected_entities.begin(), m_selected_entities.end());
        for (const auto id : m_selected_entities) {

            if (id == primary || !m_world->alive(id) || m_world->is_world_entity(id))
                continue;
            bool below_selected = false;
            GLT::world::entity_id parent = m_world->parent_of(id);
            for (u32 guard = 0; parent.is_valid() && guard < 256; ++guard) {
                if (selected.contains(parent)) {
                    below_selected = true;
                    break;
                }
                parent = m_world->parent_of(parent);
            }
            if (!below_selected)
                m_gizmo_targets.push_back(id);
        }
    }


    // Ctrl+F: moves the editor camera (keeping its direction) to where the selection fills most of the view. How far
    // away depends on how large the selected entities are: their meshes, lights, and everything below them
    void world_viewport_window::frame_selection() {

        auto* scene = m_world ? m_world->as<GLT::world::i_world_scene>() : nullptr;
        if (!scene || m_selected_entities.empty())
            return;

        const GLT::world::entity_id camera = scene->get_active_camera();
        GLT::world::camera_snapshot view{};
        if (!camera.is_valid() || !scene->get_camera_view(view))
            return;

        // The selection with everything below it
        std::vector<GLT::world::entity_id> entities;
        for (const auto id : m_selected_entities)
            if (!m_world->is_world_entity(id) && id != camera)
                collect_subtree(id, entities);
        if (entities.empty())
            return;
        const std::unordered_set<GLT::world::entity_id> wanted(entities.begin(), entities.end());
        std::unordered_set<GLT::world::entity_id> measured;

        glm::vec3 low(std::numeric_limits<f32>::max());
        glm::vec3 high(std::numeric_limits<f32>::lowest());
        const auto include = [&](const glm::vec3& point) {
            if (!std::isfinite(point.x + point.y + point.z))
                return;
            low = glm::min(low, point);
            high = glm::max(high, point);
        };

        // Meshes: the asset's bounding box where the instance is
        std::vector<GLT::asset::mesh::instance> instances;
        scene->gather_scene(instances);
        auto registry = GLT::asset::registry::get_ref();
        for (const auto& instance : instances) {

            if (!wanted.contains(instance.entity))
                continue;
            const auto* mesh = (registry && instance.mesh != INVALID_HANDLE)
                ? registry->data_as<GLT::asset::mesh::mesh_asset>(instance.mesh) : nullptr;
            if (!mesh)
                continue;
            for (u32 corner = 0; corner < 8; ++corner) {
                const glm::vec3 local((corner & 1u) ? mesh->bounds.max.x : mesh->bounds.min.x, (corner & 2u) ? mesh->bounds.max.y : mesh->bounds.min.y,
                    (corner & 4u) ? mesh->bounds.max.z : mesh->bounds.min.z);
                include(glm::vec3(instance.transform * glm::vec4(local, 1.f)));
            }
            measured.insert(instance.entity);
        }

        // Lights: where they are, with a little room (their range would put the camera far away from a small lamp)
        std::vector<GLT::world::light_snapshot> lights;
        scene->gather_lights(lights);
        for (const auto& light : lights) {
            if (!wanted.contains(light.entity))
                continue;
            const f32 room = std::clamp(light.radius * 0.1f, 0.25f, 1.5f);
            include(light.position - glm::vec3(room));
            include(light.position + glm::vec3(room));
            measured.insert(light.entity);
        }

        // Anything else (an empty entity, a camera, a sound): its position, from the transforms up the hierarchy
        for (const auto id : entities) {

            if (measured.contains(id))
                continue;
            const glm::vec3 position(entity_world_matrix(id)[3]);
            include(position - glm::vec3(0.5f));
            include(position + glm::vec3(0.5f));
        }

        if (low.x > high.x)
            return;

        // A sphere around all of it has to fit the narrower of the two view angles
        const glm::vec3 center = 0.5f * (low + high);
        const f32 radius = std::max(0.5f * glm::length(high - low), 0.25f);
        const f32 aspect = (m_viewport_size.y > 1.f) ? m_viewport_size.x / m_viewport_size.y : 1.f;
        const f32 half_vertical = glm::radians(std::clamp(view.fov, 5.f, 170.f)) * 0.5f;
        const f32 half_horizontal = std::atan(std::tan(half_vertical) * aspect);
        const f32 distance = std::max(radius / std::sin(std::min(half_vertical, half_horizontal)) * 1.15f, view.near_plane * 2.f + radius);

        const glm::mat3 camera_rotation = glm::transpose(glm::mat3(view.view));             // view = inverse of the camera's transform
        const glm::vec3 forward = -camera_rotation[2];                                      // -Z is forward
        const glm::vec3 wanted_position = center - forward * distance;

        // translate_local() takes the step in the camera's own axes
        m_world->translate_local(camera, glm::mat3(view.view) * (wanted_position - view.position));
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
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
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
                if (gizmo_toggle_button("Global", gizmo_cfg().space == gizmo_space::world))
                    gizmo_cfg().space = gizmo_space::world;

                ImGui::SameLine();
                if (gizmo_toggle_button("Local", gizmo_cfg().space == gizmo_space::local))
                    gizmo_cfg().space = gizmo_space::local;
            }
            ImGui::EndChild();
            pop_card_style();

            ImGui::SameLine();
            push_card_style();
            if (ImGui::BeginChild("##gizmo_snap_toggle", ImVec2(0, 0), card_flags)) {
                if (gizmo_toggle_button("Snap", gizmo_cfg().snap))
                    gizmo_cfg().snap = !gizmo_cfg().snap;
            }
            ImGui::EndChild();
            pop_card_style();

            row1_pos = ImGui::GetWindowPos();
            row1_size = ImGui::GetWindowSize();
        }
        ImGui::End();

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(4);

        if (gizmo_cfg().snap) {

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
                            ImGui::DragFloat3("##snap_t", &gizmo_cfg().snap_translate.x, .1f, 0.f, 100.f, "%.2f");
                            break;
                        case ImGuizmo::ROTATE:
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextUnformatted("Rotate snap");
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(120.f);
                            ImGui::DragFloat("##snap_r", &gizmo_cfg().snap_rotate, 1.f, 0.f, 100.f, "%.1f deg");
                            break;
                        case ImGuizmo::SCALE:
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextUnformatted("Scale snap");
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(120.f);
                            ImGui::DragFloat("##snap_s", &gizmo_cfg().snap_scale, .1f, 0.f, 100.f, "%.2f");
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

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.f);
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
        if (!debug || !m_world || m_selected_entities.empty())
            return;

        // Sent again every frame, so the outline disappears soon after an entity is deselected. "Soon" has to be longer
        // than a frame takes: with a fixed 0.1 s the outline went out between two slow frames (flicker)
        const f32 lifetime = std::max(0.1f, 3.f * ImGui::GetIO().DeltaTime);

        // All at once: one call per entity made the renderer search its list once per entity
        const auto& selection = editor_settings::get().selection;
        const auto primary = primary_selection();
        debug->highlight_entity(m_selected_entities, selection.secondary_color, lifetime, true);
        if (primary.is_valid())
            debug->highlight_entity(primary, selection.primary_color, lifetime, true);
    }

    // ---- misc -----------------------------------------------------------------------------------------------------

    // ---- undo / redo ------------------------------------------------------------------------------------------------

    void world_viewport_window::flush_edits() {

        m_details_edit.commit();
        m_gizmo_edit.commit();
    }


    void world_viewport_window::select_after_undo(const undo_system::world_state& state) {

        // Select the top-most entities of what the step touched, so the user sees what changed. A step that ends in
        // "nothing exists" (undo of a spawn) clears the selection
        if (!editor_settings::get().history.select_after_undo) {
            prune_dead_selection();
            return;
        }

        m_selected_entities.clear();

        if (m_world) {
            for (const auto id : state.alive) {

                if (!m_world->alive(id))
                    continue;

                const auto parent = m_world->parent_of(id);
                if (parent.is_valid() && std::find(state.alive.begin(), state.alive.end(), parent) != state.alive.end())
                    continue;

                m_selected_entities.push_back(id);
            }
        }

        m_selection_anchor = m_selected_entities.empty() ? GLT::world::INVALID_ENTITY : m_selected_entities.front();
    }


    void world_viewport_window::delete_entities(std::vector<GLT::world::entity_id> ids) {

        if (!m_world)
            return;

        flush_edits();

        // Delete whole subtrees: the children are part of what the user sees as "this entity"
        std::vector<GLT::world::entity_id> victims;
        for (const auto id : ids)
            if (m_world->alive(id) && !m_world->is_world_entity(id)
                && std::find(victims.begin(), victims.end(), id) == victims.end())
                collect_subtree(id, victims);

        if (victims.empty())
            return;

        const std::string name = ids.size() == 1
            ? std::format("Delete {}", entity_display_name(ids.front()))
            : std::format("Delete {} entities", ids.size());

        undo_system::world_change change(*m_world, victims);

        clear_selection();
        for (auto it = victims.rbegin(); it != victims.rend(); ++it)        // children before their parents
            if (m_world->alive(*it))
                m_world->despawn(*it);

        change.commit(m_undo_stack, name, [this](const undo_system::world_state& state) { select_after_undo(state); });
    }


    void world_viewport_window::collect_subtree(GLT::world::entity_id id, std::vector<GLT::world::entity_id>& out) const {

        if (!m_world || !m_world->alive(id) || std::find(out.begin(), out.end(), id) != out.end())
            return;

        out.push_back(id);                                                  // pre-order: parent before children
        for (const auto child : m_world->children_of(id))
            collect_subtree(child, out);
    }


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
