
#include "util/pch.h"
#include "terrain_editor_window.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <asset/i_asset_registry.h>

#include "util/ui/panel_collection.h"
#include "config/imgui_config.h"

#include <world/i_world.h>

#include "asset/terrain_graph_asset.h"
#include "expression/expression.h"
#include "user_texture.h"
#include "world/terrain_component.h"



namespace GLT::terrain {

    namespace {

        constexpr const char*                           SURFACE_TYPE = "terrain.surface";
        constexpr const char*                           PARAMETER_TYPE = "terrain.parameter";

        constexpr f32                                   SPLITTER = 6.0f;            // thickness of the drag handles
        constexpr f32                                   MIN_CANVAS_WIDTH = 240.0f;
        constexpr f32                                   MIN_SIDEBAR_WIDTH = 280.0f;
        constexpr f32                                   MIN_MAP_HEIGHT = 120.0f;
        constexpr f32                                   MIN_DETAILS_HEIGHT = 120.0f;

        void tooltip(const char* text) {
            if (text && *text && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", text);
        }

        // Drag handle between two panels. [vertical]: a vertical bar that moves along x. [value] changes by the mouse
        // delta times [sign] while dragged
        void splitter(const char* id, const bool vertical, const f32 length, f32& value, const f32 sign) {

            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const ImVec2 size = vertical ? ImVec2(SPLITTER, length) : ImVec2(length, SPLITTER);
            ImGui::InvisibleButton(id, size);
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();
            if (hovered || active)
                ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
            if (active)
                value += (vertical ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y) * sign;

            // A thin line, highlighted while hovered / dragged
            const ImU32 color = active ? ImGui::GetColorU32(GLT::imgui_config::get_action_color00_active_ref())
                : hovered ? ImGui::GetColorU32(GLT::imgui_config::get_action_color00_hover_ref())
                : ImGui::GetColorU32(ImGuiCol_Separator, 0.45f);
            const f32 thickness = (hovered || active) ? 2.0f : 1.0f;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            if (vertical) {
                const f32 x = pos.x + SPLITTER * 0.5f;
                draw->AddLine(ImVec2(x, pos.y), ImVec2(x, pos.y + length), color, thickness);
            } else {
                const f32 y = pos.y + SPLITTER * 0.5f;
                draw->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + length, y), color, thickness);
            }
        }

        // Small dot + text, vertically centred on a frame
        void status_text(const char* text, const ImU32 dot) {

            ImGui::AlignTextToFramePadding();
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const f32 radius = 3.5f;
            const f32 y = pos.y + ImGui::GetFrameHeight() * 0.5f;
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + radius, y), radius, dot);
            UI::shift_cursor_pos(radius * 2.0f + 6.0f, 0.0f);
            ImGui::TextDisabled("%s", text);
        }

        // Heading of a panel section: bold title + dim subtitle
        void heading(const char* title, const char* subtitle = nullptr) {

            ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::bold_big));
            ImGui::TextUnformatted(title);
            ImGui::PopFont();
            if (subtitle && *subtitle)
                ImGui::TextDisabled("%s", subtitle);
        }

    }

    // CLASS IMPLEMENTATION ============================================================================================

    terrain_editor_window::terrain_editor_window(std::filesystem::path asset_path, GLT::asset::asset_ref asset,
        const i_node_registry& registry, evaluator& eval)
        : m_path(std::move(asset_path)), m_asset(std::move(asset)), m_registry(registry), m_eval(eval), m_canvas(registry), m_map(eval), m_preview_3d(eval) {}


    terrain_editor_window::~terrain_editor_window() = default;


    std::string terrain_editor_window::title() const { return "Terrain: " + m_path.stem().generic_string(); }


    int terrain_editor_window::window_flags() const { return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse; }


    terrain_graph_asset* terrain_editor_window::asset() const {

        auto* registry = GLT::asset::registry::get_ptr();
        if (!registry || !m_asset.valid())
            return nullptr;
        return static_cast<terrain_graph_asset*>(registry->data(m_asset.get()));
    }


    void terrain_editor_window::apply(terrain_graph graph) {

        if (terrain_graph_asset* graph_asset = asset()) {
            graph_asset->set_graph(std::move(graph));
            m_canvas.acknowledge(graph_asset->revision());      // our own change: the canvas already shows it
            m_dirty = true;
        }
    }


    void terrain_editor_window::save() {

        terrain_graph_asset* graph_asset = asset();
        auto* registry = GLT::asset::registry::get_ptr();
        if (!graph_asset || !registry)
            return;

        graph_asset->editor_json = m_canvas.layout();
        m_saved_layout = graph_asset->editor_json;
        if (auto saved = registry->save(m_asset.get()); !saved) {
            m_save_status = std::format("Save failed (error {})", static_cast<int>(saved.error()));
            LOG(error, "terrain: saving [{}] failed (error {})", m_path.generic_string(), static_cast<int>(saved.error()))
            return;
        }
        m_dirty = false;
        m_save_status = "Saved";
    }


    void terrain_editor_window::draw_content(f32 /*delta_time*/) {

        user_texture::collect_released();

        terrain_graph_asset* graph_asset = asset();
        if (!graph_asset) {
            UI::empty_state("The terrain graph is not loaded", "It may have failed to load, see the log");
            return;
        }

        if (!m_loaded) {
            m_canvas.load(*graph_asset->graph(), graph_asset->editor_json);
            m_canvas.acknowledge(graph_asset->revision());
            m_saved_layout = graph_asset->editor_json;
            m_map.frame(graph_asset->preview.domain_min, graph_asset->preview.domain_max);
            m_loaded = true;
        }

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
            save();

        draw_toolbar(*graph_asset);

        constexpr ImGuiWindowFlags NO_SCROLL = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        m_sidebar_width = std::clamp(m_sidebar_width, MIN_SIDEBAR_WIDTH, std::max(MIN_SIDEBAR_WIDTH, avail.x - MIN_CANVAS_WIDTH - SPLITTER));
        const f32 canvas_width = std::max(avail.x - m_sidebar_width - SPLITTER, 1.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

        // ---- node canvas -----------------------------------------------------------------------------------------------
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        const bool canvas_open = ImGui::BeginChild("##canvas", ImVec2(canvas_width, avail.y), ImGuiChildFlags_None, NO_SCROLL | ImGuiWindowFlags_NoMove);
        ImGui::PopStyleVar();
        if (canvas_open) {
            ImGui::PopStyleVar();                           // item spacing: the canvas uses its own
            const std::shared_ptr<const terrain_graph> graph = graph_asset->graph();
            m_canvas.set_preview_node(map_target(*graph_asset).node);    // nodes that don't feed it warn on hover
            if (auto changed = m_canvas.draw(*graph, graph_asset->revision()))
                apply(std::move(*changed));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        }
        ImGui::EndChild();

        // Moving nodes changes only the layout: unsaved too (checked when a drag ends)
        if (!m_dirty && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && m_canvas.layout() != m_saved_layout)
            m_dirty = true;

        // Selection drives details and map
        if (const u32 selected = m_canvas.selected(); selected != m_inspected_node) {
            m_inspected_node = selected;
            m_inspected_pin = 0;
        }

        // ---- canvas | sidebar --------------------------------------------------------------------------------------------
        ImGui::SameLine();
        splitter("##split_sidebar", true, avail.y, m_sidebar_width, -1.0f);
        ImGui::SameLine();

        ImGui::PopStyleVar();
        if (ImGui::BeginChild("##sidebar", ImVec2(m_sidebar_width, avail.y), ImGuiChildFlags_None, NO_SCROLL))
            draw_sidebar(*graph_asset);
        ImGui::EndChild();
    }

    // CLASS PRIVATE ===================================================================================================

    void terrain_editor_window::draw_toolbar(terrain_graph_asset& graph_asset) {

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);

        // Save: accent while there is something to save
        if (m_dirty ? UI::accent_button("Save") : UI::subtle_button("Save"))
            save();
        tooltip("Writes the graph, node positions and preview settings (Ctrl+S)");

        ImGui::SameLine(0.0f, 10.0f);
        if (m_dirty)
            status_text("Unsaved changes", ImGui::GetColorU32(ImVec4(1.0f, 0.72f, 0.28f, 1.0f)));
        else if (!m_save_status.empty() && m_save_status != "Saved")
            status_text(m_save_status.c_str(), ImGui::GetColorU32(ImVec4(1.0f, 0.4f, 0.35f, 1.0f)));
        else
            status_text("Saved", ImGui::GetColorU32(ImVec4(0.45f, 0.8f, 0.45f, 1.0f)));

        UI::separation_vertical();

        // Seed of the preview
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Seed");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::InputScalar("##seed", ImGuiDataType_U64, &graph_asset.preview.seed))
            m_dirty = true;
        tooltip("Seed of the preview (worlds use their own)");
        ImGui::SameLine();
        if (UI::subtle_button("Randomize")) {
            graph_asset.preview.seed = hash_mix(graph_asset.preview.seed ^ static_cast<u64>(ImGui::GetTime() * 1000.0));
            m_dirty = true;
        }

        UI::separation_vertical();

        if (UI::subtle_button("Frame nodes"))
            m_canvas.focus_all();
        tooltip("Canvas: show all nodes");

        UI::separation_vertical();

        // The edited world gets (or updates) its terrain component: this graph, the preview seed and overrides, live
        GLT::editor::i_editor* editor = GLT::editor::get();
        GLT::world::i_world* world = editor ? editor->edited_world() : nullptr;
        if (!world)
            ImGui::BeginDisabled();
        if (UI::accent_button("Apply to world") && world) {
            if (terrain_component* terrain = ensure_terrain(*world)) {
                terrain->graph = m_asset;
                terrain->seed = graph_asset.preview.seed;
                terrain->overrides.assign(m_overrides.begin(), m_overrides.end());
                terrain->mode = terrain_mode::live;
                terrain->enabled = true;
                world->mark_changed(world->world_entity());
                m_apply_status = "applied (save the world to keep it)";
            } else
                m_apply_status = "this world has no world entity";
        }
        if (!world)
            ImGui::EndDisabled();
        tooltip(world ? "The open world generates its terrain from this graph (live, around the camera). Settings: World > Terrain component"
                      : "Open a world first");
        if (!m_apply_status.empty()) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", m_apply_status.c_str());
        }

        // Right aligned: shortcuts
        const char* hint = "Right-click: add node   Del: remove   Ctrl+C / V: copy";
        const f32 hint_width = ImGui::CalcTextSize(hint).x;
        ImGui::SameLine();
        const f32 space = ImGui::GetContentRegionAvail().x;
        if (space > hint_width + 20.0f) {
            UI::shift_cursor_pos(space - hint_width, 0.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", hint);
        } else
            ImGui::NewLine();

        ImGui::PopStyleVar(2);
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
    }


    void terrain_editor_window::draw_sidebar(terrain_graph_asset& graph_asset) {

        draw_map_header(graph_asset);

        // ---- map (resizable) -------------------------------------------------------------------------------------------
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (m_map_height <= 0.0f)
            m_map_height = avail.x;                     // square to start with
        m_map_height = std::clamp(m_map_height, MIN_MAP_HEIGHT, std::max(MIN_MAP_HEIGHT, avail.y - MIN_DETAILS_HEIGHT - SPLITTER));

        if (m_view_mode == 1)
            m_preview_3d.draw(preview_3d_target(graph_asset), ImVec2(avail.x, m_map_height));
        else {
            m_preview_3d.set_visible(false);            // no GPU work while the map is shown
            m_map.draw(map_target(graph_asset), ImVec2(avail.x, m_map_height));
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        splitter("##split_map", false, avail.x, m_map_height, 1.0f);
        ImGui::PopStyleVar();

        // ---- details (scrolls) -------------------------------------------------------------------------------------------
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 8.0f));
        const bool open = ImGui::BeginChild("##details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
        if (open) {
            const std::shared_ptr<const terrain_graph> graph = graph_asset.graph();
            if (auto changed = draw_details(*graph))
                apply(std::move(*changed));
        }
        ImGui::EndChild();
    }


    void terrain_editor_window::draw_map_header(terrain_graph_asset& graph_asset) {

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);

        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::bold));
        ImGui::TextUnformatted("Preview");
        ImGui::PopFont();

        // Map (2D) or 3D
        static constexpr const char* VIEWS[] = { "2D", "3D" };
        ImGui::SameLine(0.0f, 10.0f);
        UI::segmented_control("##view", m_view_mode, VIEWS, 80.0f);
        tooltip("2D: map of any node's output. 3D: the surface (or the selected heightfield) over the preview domain");

        // Which pin of the selection (nodes with several outputs, sinks: their inputs)
        const std::shared_ptr<const terrain_graph> graph = graph_asset.graph();
        std::shared_ptr<const node_type> type;          // keeps the pin names alive
        std::vector<const char*> pins;
        if (const graph_node* node = graph->find(m_inspected_node))
            if ((type = m_registry.find(node->type)))
                for (const pin_desc& pin : type->is_sink ? type->inputs : type->outputs)
                    pins.push_back(pin.name.c_str());

        const char* frame_label = "Frame domain";
        const f32 frame_width = ImGui::CalcTextSize(frame_label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        const f32 right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;

        if (pins.size() > 1) {
            m_inspected_pin = std::clamp(m_inspected_pin, 0, static_cast<i32>(pins.size()) - 1);
            ImGui::SameLine(0.0f, 12.0f);
            const f32 width = std::max(80.0f, right - ImGui::GetCursorPosX() - frame_width - 12.0f);
            if (pins.size() <= 3)
                UI::segmented_control("##pin", m_inspected_pin, pins, std::min(width, 90.0f * static_cast<f32>(pins.size())));
            else {
                ImGui::SetNextItemWidth(std::min(width, 180.0f));
                if (ImGui::BeginCombo("##pin", pins[static_cast<size_t>(m_inspected_pin)])) {
                    for (size_t i = 0; i < pins.size(); ++i)
                        if (ImGui::Selectable(pins[i], static_cast<i32>(i) == m_inspected_pin))
                            m_inspected_pin = static_cast<i32>(i);
                    ImGui::EndCombo();
                }
            }
            tooltip("Which pin of the selected node the map shows");
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right - frame_width));
        if (UI::subtle_button(frame_label)) {
            m_map.frame(graph_asset.preview.domain_min, graph_asset.preview.domain_max);
            m_preview_3d.frame();
        }
        tooltip("Show the whole preview domain (set it in the details below when nothing is selected)");

        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
    }


    terrain_map::target terrain_editor_window::map_target(const terrain_graph_asset& graph_asset) const {

        terrain_map::target t{};
        t.graph = graph_asset.graph();
        t.revision = graph_asset.revision();
        t.seed = graph_asset.preview.seed;
        for (const auto& [name, value] : m_overrides) {
            t.parameters[name] = value;
            t.parameters_hash = hash_combine(hash_combine(t.parameters_hash, hash_string(name)), std::bit_cast<u32>(value));
        }

        // The selection (any node: its output, a sink: its input) ...
        if (const graph_node* node = t.graph->find(m_inspected_node))
            if (const std::shared_ptr<const node_type> type = m_registry.find(node->type)) {
                const std::vector<pin_desc>& pins = type->is_sink ? type->inputs : type->outputs;
                if (!pins.empty()) {
                    const pin_desc& pin = pins[static_cast<size_t>(std::clamp(m_inspected_pin, 0, static_cast<i32>(pins.size()) - 1))];
                    t.node = node->id;
                    t.pin = pin.name;
                    t.mask = pin.type == pin_type::mask;
                    return t;
                }
            }

        // ... otherwise the surface
        for (const auto& node : t.graph->nodes)
            if (node.type == SURFACE_TYPE) {
                t.node = node.id;
                t.pin = "height";
                return t;
            }
        return t;
    }


    void terrain_editor_window::draw_graph_overview(const terrain_graph& graph) {

        heading(m_path.stem().generic_string().c_str(), "Terrain graph");
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        if (UI::begin_card("Graph", { .description = "Select a node to edit it and to preview any of its pins" })) {
            UI::property_value("Nodes", std::to_string(graph.nodes.size()));
            UI::property_value("Links", std::to_string(graph.links.size()));
        }
        UI::end_card();

        if (terrain_graph_asset* graph_asset = asset()) {
            if (UI::begin_card("Preview domain", { .description = "World area (x, z) that \"Frame domain\" shows" })) {
                glm::vec2 min(graph_asset->preview.domain_min);
                glm::vec2 max(graph_asset->preview.domain_max);
                bool changed = UI::property_drag("Min", min, 10.0f, 0.0f, 0.0f, "%.0f");
                changed |= UI::property_drag("Max", max, 10.0f, 0.0f, 0.0f, "%.0f");
                if (changed) {
                    graph_asset->preview.domain_min = glm::dvec2(min);
                    graph_asset->preview.domain_max = glm::dvec2(max);
                    m_dirty = true;
                }
            }
            UI::end_card();
        }

        // Exposed parameters: preview overrides
        std::vector<const graph_node*> parameters;
        for (const auto& n : graph.nodes)
            if (n.type == PARAMETER_TYPE)
                parameters.push_back(&n);

        const std::string summary = parameters.empty() ? std::string() : std::to_string(parameters.size());
        if (UI::begin_card("Exposed parameters", { .description = "Preview overrides; worlds set their own in the terrain component",
            .summary = summary.c_str() })) {
            for (const graph_node* n : parameters) {
                const auto name_it = n->params.find("Name");
                const auto default_it = n->params.find("Default");
                const std::string name = name_it != n->params.end() ? detail::convert_param<std::string>(name_it->second) : std::string("parameter");
                const f32 fallback = default_it != n->params.end() ? detail::convert_param<f32>(default_it->second) : 0.0f;

                ImGui::PushID(static_cast<int>(n->id));
                const auto it = m_overrides.find(name);
                f32 value = it != m_overrides.end() ? it->second : fallback;
                if (UI::property_drag(name.c_str(), value, std::max(std::abs(value) * 0.01f, 0.01f), 0.0f, 0.0f, "%.4g", nullptr, &fallback)) {
                    if (value == fallback)
                        m_overrides.erase(name);
                    else
                        m_overrides[name] = value;
                }
                ImGui::PopID();
            }
            if (parameters.empty())
                ImGui::TextDisabled("None yet. Add \"Exposed Parameter\" nodes");
        }
        UI::end_card();
    }


    terrain_preview_3d::target terrain_editor_window::preview_3d_target(const terrain_graph_asset& graph_asset) const {

        const terrain_map::target map = map_target(graph_asset);
        terrain_preview_3d::target t{};
        t.graph = map.graph;
        t.revision = map.revision;
        t.seed = map.seed;
        t.parameters = map.parameters;
        t.parameters_hash = map.parameters_hash;
        t.domain_min = graph_asset.preview.domain_min;
        t.domain_max = graph_asset.preview.domain_max;

        if (!map.mask && map.node != INVALID_NODE_ID) {
            t.node = map.node;
            t.pin = map.pin;
            return t;
        }
        // A mask is not a height: show the surface
        if (map.graph)
            for (const auto& node : map.graph->nodes)
                if (node.type == SURFACE_TYPE) {
                    t.node = node.id;
                    t.pin = "height";
                    return t;
                }
        return t;
    }


    std::optional<terrain_graph> terrain_editor_window::draw_details(const terrain_graph& graph) {

        const graph_node* node = graph.find(m_inspected_node);
        if (!node) {
            draw_graph_overview(graph);
            return std::nullopt;
        }

        const std::shared_ptr<const node_type> type = m_registry.find(node->type);
        if (!type) {
            UI::empty_state("Unknown node type",
                std::format("'{}' is not registered (node pack not loaded or renamed). Its parameters and links are kept.", node->type).c_str());
            return std::nullopt;
        }

        // ---- header --------------------------------------------------------------------------------------------------
        heading(node->label.empty() ? type->display_name.c_str() : node->label.c_str(),
            std::format("{}  /  {}  /  #{}", type->category, type->type_name, node->id).c_str());
        if (!type->description.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(type->description.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        std::optional<terrain_graph> result;
        auto edit = [&]() -> graph_node& {
            if (!result)
                result = graph;
            return *result->find(node->id);
        };

        // ---- node --------------------------------------------------------------------------------------------------------
        if (UI::begin_card("Node")) {
            std::string label = node->label;
            if (UI::property_text("Title", label, "Shown on the canvas instead of the type name", type->display_name.c_str())) {
                edit().label = label;
                m_canvas.rename(node->id, label.empty() ? type->display_name : label);
            }
        }
        UI::end_card();

        // Parameters are edited in the node itself (canvas); here: a read-only summary of the non-default ones
        if (!type->params.empty()) {
            u32 changed_count = 0;
            for (const param_desc& desc : type->params)
                if (const auto it = node->params.find(desc.name); it != node->params.end() && it->second != desc.default_value)
                    ++changed_count;
            const std::string summary = std::format("{} of {} changed", changed_count, type->params.size());
            if (UI::begin_card("Parameters", { .description = "Edit them in the node. Right-click the node: reset",
                .summary = summary.c_str(), .collapsible = true, .default_open = false })) {
                for (const param_desc& desc : type->params) {
                    const auto it = node->params.find(desc.name);
                    const param_value& value = it != node->params.end() ? it->second : desc.default_value;
                    const std::string text = std::visit([&](const auto& v) -> std::string {
                        using T = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<T, glm::vec2>) return std::format("{:.4g}, {:.4g}", v.x, v.y);
                        else if constexpr (std::is_same_v<T, std::string>) return v;
                        else if constexpr (std::is_same_v<T, bool>) return v ? "on" : "off";
                        else if constexpr (std::is_same_v<T, i32>) {
                            if (desc.kind == param_kind::choice && v >= 0 && static_cast<size_t>(v) < desc.choices.size())
                                return desc.choices[static_cast<size_t>(v)];
                            return std::to_string(v);
                        } else return std::format("{:.4g}", v);
                    }, value);
                    UI::property_value(desc.name.c_str(), text, desc.tooltip.empty() ? nullptr : desc.tooltip.c_str());
                }
            }
            UI::end_card();
        }

        // ---- pins, for reference ---------------------------------------------------------------------------------------
        const std::string pin_summary = std::format("{} in, {} out", type->inputs.size(), type->outputs.size());
        if (UI::begin_card("Pins", { .summary = pin_summary.c_str(), .collapsible = true, .default_open = false })) {
            for (const auto& pin : type->inputs) {
                std::string text;
                if (const graph_link* link = graph.source_of(node->id, pin.name)) {
                    const graph_node* from = graph.find(link->from_node);
                    const std::shared_ptr<const node_type> from_type = from ? m_registry.find(from->type) : nullptr;
                    const std::string from_name = from && !from->label.empty() ? from->label : from_type ? from_type->display_name : std::format("#{}", link->from_node);
                    text = std::format("<- {} . {}", from_name, link->from_pin);
                } else
                    text = std::format("unconnected ({:g})", pin.default_value);
                UI::property_value(std::format("in: {}", pin.name).c_str(), text);
            }
            for (const auto& pin : type->outputs)
                UI::property_value(std::format("out: {}", pin.name).c_str(),
                    pin.type == pin_type::mask ? "mask" : pin.type == pin_type::scalar ? "scalar" : "heightfield");
        }
        UI::end_card();

        return result;
    }

}
