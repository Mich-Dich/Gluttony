
#include "util/pch.h"
#include "vns_terrain_node.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include "config/imgui_config.h"
#include "util/ui/panel_collection.h"

#include "expression/expression.h"



namespace GLT::terrain {

    namespace {

        // ---- layout (node units, scaled by the canvas zoom) ------------------------------------------------------------
        constexpr f32                                   WIDTH = 200.0f;
        constexpr f32                                   WIDTH_WITH_PARAMS = 250.0f;
        constexpr f32                                   HEADER = 22.0f;
        constexpr f32                                   TOP_GAP = 4.0f;         // header -> first pin row
        constexpr f32                                   PIN_ROW = 20.0f;
        constexpr f32                                   SEPARATOR = 6.0f;       // pins -> parameters
        constexpr f32                                   PARAM_ROW = 22.0f;
        constexpr f32                                   EXPRESSION_ROW = 110.0f;
        constexpr f32                                   SIDE = 6.0f;            // parameter rows: left / right margin
        constexpr f32                                   BOTTOM = 6.0f;
        constexpr f32                                   ROUNDING = 2.0f;        // nearly rectangular
        constexpr f32                                   FIELD_ROUNDING = 1.5f;
        constexpr f32                                   FONT_SIZE = 13.0f;      // canvas font at zoom 1
        constexpr f32                                   MIN_WIDGET_FONT = 5.0f; // below this (far zoomed out) no widgets

        [[nodiscard]] const char* socket_type(pin_type type) {
            switch (type) {
                case pin_type::mask:    return vns_terrain_node::SOCKET_MASK;
                case pin_type::scalar:  return vns_terrain_node::SOCKET_SCALAR;
                default:                return vns_terrain_node::SOCKET_HEIGHTFIELD;
            }
        }

        // All pins carry fields, so any output may feed any input: an input lists its own type first (its colour) and
        // accepts the others
        [[nodiscard]] std::vector<std::string> input_types(pin_type type) {
            std::vector<std::string> types{ socket_type(type) };
            for (const char* other : { vns_terrain_node::SOCKET_HEIGHTFIELD, vns_terrain_node::SOCKET_MASK, vns_terrain_node::SOCKET_SCALAR })
                if (types.front() != other)
                    types.emplace_back(other);
            return types;
        }

        [[nodiscard]] ImColor category_color(std::string_view category) {
            if (category == "Generators") return ImColor(86, 150, 74);
            if (category == "Shapes")     return ImColor(56, 146, 138);
            if (category == "Combine")    return ImColor(70, 110, 182);
            if (category == "Modify")     return ImColor(188, 122, 56);
            if (category == "Masks")      return ImColor(140, 90, 175);
            if (category == "Hydrology")  return ImColor(52, 134, 192);
            if (category == "Output")     return ImColor(196, 78, 78);
            return ImColor(110, 110, 118);
        }

        [[nodiscard]] ImU32 with_alpha(ImVec4 color, f32 alpha) { color.w = alpha; return ImGui::GetColorU32(color); }

        // The engine's main colour (imgui_config): selection, value bars, switches
        [[nodiscard]] ImVec4 accent() { return GLT::imgui_config::get_main_color_ref(); }

        // ---- JSON of a parameter value (copy / paste) ----------------------------------------------------------------
        [[nodiscard]] Json::Value encode(const param_value& value) {
            Json::Value json;
            json["i"] = static_cast<Json::UInt>(value.index());
            std::visit([&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, glm::vec2>) {
                    json["v"].append(v.x);
                    json["v"].append(v.y);
                } else if constexpr (std::is_same_v<T, std::string>)
                    json["v"] = v;
                else if constexpr (std::is_same_v<T, f32>)
                    json["v"] = static_cast<double>(v);
                else
                    json["v"] = v;
            }, value);
            return json;
        }

        [[nodiscard]] std::optional<param_value> decode(const Json::Value& json) {
            if (!json.isObject() || !json.isMember("i") || !json.isMember("v"))
                return std::nullopt;
            const Json::Value& v = json["v"];
            switch (json["i"].asUInt()) {
                case 0: return v.isNumeric() ? std::optional<param_value>(v.asFloat()) : std::nullopt;
                case 1: return v.isNumeric() ? std::optional<param_value>(v.asInt()) : std::nullopt;
                case 2: return v.isBool() ? std::optional<param_value>(v.asBool()) : std::nullopt;
                case 3: return v.isArray() && v.size() == 2 ? std::optional<param_value>(glm::vec2(v[0].asFloat(), v[1].asFloat())) : std::nullopt;
                case 4: return v.isString() ? std::optional<param_value>(v.asString()) : std::nullopt;
                default: return std::nullopt;
            }
        }

        // ---- widgets ---------------------------------------------------------------------------------------------------

        void tooltip(const std::string& text) {
            if (!text.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsItemActive())
                ImGui::SetTooltip("%s", text.c_str());
        }

        // Dim label, vertically centred in [a, b], clipped to [a.x, max_x]
        void row_label(const char* text, ImVec2 a, ImVec2 b, f32 max_x) {
            const f32 font = ImGui::GetFontSize();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImVec4 clip(a.x, a.y, max_x, b.y);
            draw->AddText(nullptr, 0.0f, ImVec2(a.x, a.y + (b.y - a.y - font) * 0.5f), IM_COL32(200, 202, 210, 190), text, nullptr, 0.0f, &clip);
        }

        // Blender style number field: one frame with the label on the left and the value on the right, a fill bar for
        // bounded values. Drag to change (the whole range over the field's width when bounded), double-click or
        // Ctrl+click to type a value
        bool value_field(const char* id, const char* label, f32& value, const f32 min, const f32 max, const bool integer, const bool logarithmic,
            const ImVec2 a, const ImVec2 b, const f32 zoom, const ImU32 fill) {

            const ImVec2 size(b.x - a.x, b.y - a.y);
            const f32 font = ImGui::GetFontSize();
            const bool bounded = max > min;
            const ImGuiID widget_id = ImGui::GetID(id);
            const bool typing = ImGui::TempInputIsActive(widget_id);

            ImGui::SetCursorScreenPos(a);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f * zoom, std::max(0.0f, (size.y - font) * 0.5f)));
            if (!typing) {
                // The widget only does the interaction, the look is drawn below
                ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
            } else
                ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255, 255, 255, 20));
            ImGui::SetNextItemWidth(size.x);

            ImGuiSliderFlags flags = ImGuiSliderFlags_NoRoundToFormat;
            if (bounded)
                flags |= ImGuiSliderFlags_AlwaysClamp;
            if (bounded && logarithmic)
                flags |= ImGuiSliderFlags_Logarithmic;

            bool changed = false;
            if (integer) {
                i32 v = static_cast<i32>(std::lround(value));
                const i32 lo = static_cast<i32>(min), hi = static_cast<i32>(max);
                const f32 speed = bounded ? std::max((max - min) / std::max(size.x, 1.0f), 0.02f) : 0.1f;
                changed = ImGui::DragScalar(id, ImGuiDataType_S32, &v, speed, bounded ? &lo : nullptr, bounded ? &hi : nullptr, "%d", flags);
                if (changed)
                    value = static_cast<f32>(v);
            } else {
                const f32 speed = bounded ? (max - min) / std::max(size.x, 1.0f) : std::max(std::abs(value) * 0.01f, 0.001f);
                changed = ImGui::DragScalar(id, ImGuiDataType_Float, &value, speed, bounded ? &min : nullptr, bounded ? &max : nullptr, "%.4g", flags);
            }
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();
            ImGui::PopStyleColor(typing ? 1 : 4);
            ImGui::PopStyleVar();

            if (typing)
                return changed;
            if (hovered || active)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

            ImDrawList* draw = ImGui::GetWindowDrawList();
            const f32 rounding = FIELD_ROUNDING * zoom;
            draw->AddRectFilled(a, b, IM_COL32(255, 255, 255, active ? 26 : hovered ? 18 : 10), rounding);

            if (bounded) {
                f32 t = logarithmic && min > 0.0f ? std::log(std::max(value, min) / min) / std::log(max / min) : (value - min) / (max - min);
                t = std::clamp(t, 0.0f, 1.0f);
                if (t > 0.0f)
                    draw->AddRectFilled(a, ImVec2(a.x + size.x * t, b.y), fill, rounding, t > 0.98f ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersLeft);
            }

            const std::string text = integer ? std::format("{}", static_cast<i32>(std::lround(value))) : std::format("{:.4g}", value);
            const ImVec2 text_size = ImGui::CalcTextSize(text.c_str());
            const f32 pad = 6.0f * zoom;
            const f32 y = a.y + (size.y - font) * 0.5f;
            const ImVec4 clip(a.x, a.y, b.x, b.y);
            draw->AddText(nullptr, 0.0f, ImVec2(a.x + pad, y), IM_COL32(205, 207, 215, 200), label, nullptr, 0.0f, &clip);
            draw->AddText(nullptr, 0.0f, ImVec2(b.x - pad - text_size.x, y), IM_COL32(245, 245, 248, 255), text.c_str(), nullptr, 0.0f, &clip);
            return changed;
        }

        bool toggle_field(const char* id, const char* label, bool& value, const ImVec2 a, const ImVec2 b, const f32 zoom) {

            ImGui::SetCursorScreenPos(a);
            const bool clicked = ImGui::InvisibleButton(id, ImVec2(b.x - a.x, b.y - a.y));
            if (clicked)
                value = !value;
            const bool hovered = ImGui::IsItemHovered();

            ImDrawList* draw = ImGui::GetWindowDrawList();
            if (hovered)
                draw->AddRectFilled(a, b, IM_COL32(255, 255, 255, 10), FIELD_ROUNDING * zoom);
            row_label(label, ImVec2(a.x + 6.0f * zoom, a.y), b, b.x);

            // Switch on the right
            const f32 h = (b.y - a.y) * 0.62f;
            const f32 w = h * 1.8f;
            const ImVec2 s0(b.x - 6.0f * zoom - w, a.y + (b.y - a.y - h) * 0.5f);
            const ImVec2 s1(s0.x + w, s0.y + h);
            draw->AddRectFilled(s0, s1, value ? with_alpha(accent(), 0.85f) : IM_COL32(255, 255, 255, 40), h * 0.5f);
            const f32 knob = h * 0.5f - 2.0f * zoom;
            draw->AddCircleFilled(ImVec2(value ? s1.x - h * 0.5f : s0.x + h * 0.5f, s0.y + h * 0.5f), std::max(knob, 1.0f), IM_COL32(240, 240, 242, 255));
            return clicked;
        }

        // Frame colours of in-node inputs (combo, text)
        void push_frame_colors() {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255, 255, 255, 10));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(255, 255, 255, 18));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(255, 255, 255, 26));
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 10));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 255, 255, 18));
        }
        void pop_frame_colors() { ImGui::PopStyleColor(5); }

    }

    // CLASS IMPLEMENTATION ============================================================================================

    vns_terrain_node::vns_terrain_node() { Type = FACTORY_TYPE; }


    vns_terrain_node::vns_terrain_node(std::shared_ptr<const node_type> type, u32 graph_id, std::string_view label)
        : m_graph_id(graph_id), m_terrain_type(type->type_name), m_type(std::move(type)) {

        Type = FACTORY_TYPE;
        SetName(label.empty() ? m_type->display_name : std::string(label));
        m_accent = category_color(m_type->category);

        for (const auto& pin : m_type->inputs)
            AddSocket(new VisNodeSys::NodeSocket(this, input_types(pin.type), pin.name, VisNodeSys::NodeSocket::SocketFlow::Input));
        for (const auto& pin : m_type->outputs)
            AddSocket(new VisNodeSys::NodeSocket(this, std::string(socket_type(pin.type)), pin.name, VisNodeSys::NodeSocket::SocketFlow::Output));

        reset_params();
        m_params_dirty = false;
        relayout();
    }


    vns_terrain_node::vns_terrain_node(std::string_view type_name, u32 graph_id, std::span<const std::string> inputs, std::span<const std::string> outputs)
        : m_graph_id(graph_id), m_terrain_type(type_name), m_placeholder(true) {

        Type = FACTORY_TYPE;
        SetName(std::format("Missing: {}", type_name));
        m_accent = ImColor(190, 50, 50);

        for (const auto& name : inputs)
            AddSocket(new VisNodeSys::NodeSocket(this, input_types(pin_type::heightfield), name, VisNodeSys::NodeSocket::SocketFlow::Input));
        for (const auto& name : outputs)
            AddSocket(new VisNodeSys::NodeSocket(this, std::string(SOCKET_HEIGHTFIELD), name, VisNodeSys::NodeSocket::SocketFlow::Output));
        relayout();
    }


    vns_terrain_node::vns_terrain_node(const vns_terrain_node& other)
        : VisNodeSys::Node(other), m_graph_id(0), m_copied_from(other.m_graph_id ? other.m_graph_id : other.m_copied_from),
          m_terrain_type(other.m_terrain_type), m_placeholder(other.m_placeholder), m_type(other.m_type), m_params(other.m_params),
          m_params_dirty(true), m_accent(other.m_accent), m_params_top(other.m_params_top), m_pin_rows(other.m_pin_rows) {}


    void vns_terrain_node::set_params(const std::map<std::string, param_value>& stored) {

        reset_params();
        for (const auto& [name, value] : stored)
            if (m_params.contains(name))
                m_params[name] = value;
        m_params_dirty = false;
    }


    void vns_terrain_node::adopt_type(std::shared_ptr<const node_type> type) {

        if (!type || type->type_name != m_terrain_type || m_type == type)
            return;
        m_type = std::move(type);
        m_placeholder = false;
        m_accent = category_color(m_type->category);

        // Keep the values that came with the node (paste), defaults for the rest, nothing the type does not know
        std::map<std::string, param_value> values;
        for (const param_desc& desc : m_type->params) {
            const auto it = m_params.find(desc.name);
            values[desc.name] = it != m_params.end() ? it->second : desc.default_value;
        }
        m_params = std::move(values);
        m_params_dirty = true;
        relayout();
    }


    void vns_terrain_node::reset_params() {

        if (!m_type)
            return;
        m_params.clear();
        for (const param_desc& desc : m_type->params)
            m_params[desc.name] = desc.default_value;
        m_params_dirty = true;
    }


    Json::Value vns_terrain_node::ToJson() {

        Json::Value json = VisNodeSys::Node::ToJson();
        json["glt_type"] = m_terrain_type;
        json["glt_placeholder"] = m_placeholder;
        json["glt_source"] = static_cast<Json::UInt>(m_copied_from ? m_copied_from : m_graph_id);
        Json::Value params(Json::objectValue);
        for (const auto& [name, value] : m_params)
            params[name] = encode(value);
        json["glt_params"] = params;
        return json;
    }


    bool vns_terrain_node::FromJson(Json::Value json) {

        if (!VisNodeSys::Node::FromJson(json))
            return false;
        m_terrain_type = json["glt_type"].asString();
        m_placeholder = json["glt_placeholder"].asBool();
        m_copied_from = json["glt_source"].asUInt();
        m_graph_id = 0;                                 // a new graph node once the canvas compiles

        // The node type (parameter rows, colours) is set by graph_canvas::compile() through adopt_type()
        if (json.isMember("glt_params") && json["glt_params"].isObject())
            for (const auto& name : json["glt_params"].getMemberNames())
                if (auto value = decode(json["glt_params"][name]))
                    m_params[name] = std::move(*value);
        m_params_dirty = true;
        relayout();
        return true;
    }


    void vns_terrain_node::register_factory() {

        static bool s_registered = false;
        if (s_registered)
            return;
        s_registered = true;
        NODE_FACTORY.RegisterNodeType(FACTORY_TYPE,
            []() -> VisNodeSys::Node* { return new vns_terrain_node(); },
            [](const VisNodeSys::Node& other) -> VisNodeSys::Node* { return new vns_terrain_node(static_cast<const vns_terrain_node&>(other)); });
    }


    void vns_terrain_node::apply_canvas_style(VisNodeSys::NodeArea& area, f32 min_zoom, f32 max_zoom) {

        VisNodeSys::NodeAreaSettings& settings = area.GetSettings();
        settings.bShowDefaultMainContextMenu = false;           // its "rename socket" / "sub area" entries break the pin mapping

        VisNodeSys::NodeAreaStyle& style = settings.Style;
        style.Grid.BOLD_LINE_WIDTH = 1.0f;
        style.Grid.GridBackgroundColor = ImVec4(22.0f / 255.0f, 23.0f / 255.0f, 27.0f / 255.0f, 1.0f);
        style.Grid.GridLinesColor = ImVec4(1.0f, 1.0f, 1.0f, 0.03f);
        style.Grid.GridBoldLinesColor = ImVec4(1.0f, 1.0f, 1.0f, 0.06f);

        style.GeneralConnection.HoveredOutlineColor = ImVec4(1.0f, 1.0f, 1.0f, 0.3f);
        style.GeneralConnection.ConnectionThickness = 2.0f;
        style.GeneralConnection.bColorBySocketType = true;                               // a link shows its data type (heightfield / mask / scalar)
        style.GeneralConnection.Shape = VisNodeSys::ConnectionShape::Octilinear;        // subway lines: 8 directions only
        style.GeneralConnection.OctilinearStub = 15.0f;                                 // = grid step
        style.GeneralConnection.OctilinearCornerCut = 8.0f;
        style.Font = GLT::imgui_config::get_font(GLT::imgui_config::font_type::regular);
        style.FontSize = FONT_SIZE;

        area.SetZoomLimits(min_zoom, max_zoom);
        update_canvas_colors(area);
    }


    void vns_terrain_node::update_canvas_colors(VisNodeSys::NodeArea& area) {

        VisNodeSys::NodeAreaStyle& style = area.GetSettings().Style;
        ImVec4 main = accent();
        main.w = 1.0f;
        style.GeneralConnection.Color = main;
        // Selected links: the main colour, lightened
        style.GeneralConnection.SelectionOutlineColor = ImVec4(std::min(main.x + 0.35f, 1.0f), std::min(main.y + 0.35f, 1.0f), std::min(main.z + 0.35f, 1.0f), 1.0f);
        style.NodeSelectionColor = main;
        style.MouseSelectRegionColor = ImVec4(main.x, main.y, main.z, 0.15f);
    }


    void vns_terrain_node::set_preview_role(preview_role role, std::string_view target_name) {

        m_preview_role = role;
        if (m_preview_target != target_name)
            m_preview_target = target_name;
    }

    // CLASS PROTECTED =================================================================================================

    void vns_terrain_node::relayout() {

        m_pin_rows = static_cast<u32>(std::max(Input.size(), Output.size()));
        f32 y = HEADER + TOP_GAP + static_cast<f32>(m_pin_rows) * PIN_ROW;
        m_params_top = y;

        const bool has_params = m_type && !m_type->params.empty();
        if (has_params) {
            m_params_top = y + SEPARATOR;
            y = m_params_top;
            for (const param_desc& desc : m_type->params)
                y += param_row_height(desc);
        }
        if (m_placeholder)
            y += PIN_ROW;                               // "node type not loaded" note
        SetSize(ImVec2(has_params ? WIDTH_WITH_PARAMS : WIDTH, y + BOTTOM));
        SetTitleBarHeight(HEADER);
    }


    f32 vns_terrain_node::param_row_height(const param_desc& desc) const {

        return desc.kind == param_kind::text && m_terrain_type == "terrain.expression" ? EXPRESSION_ROW : PARAM_ROW;
    }


    bool vns_terrain_node::GetSocketLocalPosition(const VisNodeSys::NodeSocket* socket, ImVec2& out_position) const {

        const bool input = socket->GetFlowDirection() == VisNodeSys::NodeSocket::SocketFlow::Input;
        const auto& list = input ? Input : Output;
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i] == socket) {
                // On the node's edge, one row per pin (inputs left, outputs right)
                out_position = ImVec2(input ? 0.0f : GetSize().x, HEADER + TOP_GAP + PIN_ROW * (static_cast<f32>(i) + 0.5f));
                return true;
            }
        return false;
    }


    bool vns_terrain_node::DrawFrame(const VisNodeSys::NodeFrameContext& context) {

        ImDrawList* draw = context.DrawList;
        const f32 z = context.Zoom;
        const ImVec2 a = context.LeftTop;
        const ImVec2 b = context.RightBottom;
        const f32 rounding = ROUNDING * z;
        const f32 header = HEADER * z;

        // Shadow, body
        draw->AddRectFilled(ImVec2(a.x + 2.0f * z, a.y + 3.0f * z), ImVec2(b.x + 2.0f * z, b.y + 3.0f * z), IM_COL32(0, 0, 0, 70), rounding);
        draw->AddRectFilled(a, b, IM_COL32(33, 35, 41, 252), rounding);

        // Header: muted category colour, a thin line of it at the bottom
        const ImVec4 c = m_accent.Value;
        draw->AddRectFilled(a, ImVec2(b.x, a.y + header), ImGui::GetColorU32(ImVec4(c.x * 0.45f, c.y * 0.45f, c.z * 0.45f, 1.0f)), rounding, ImDrawFlags_RoundCornersTop);
        draw->AddLine(ImVec2(a.x, a.y + header), ImVec2(b.x, a.y + header), ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, 0.8f)), 1.0f);

        // Title
        ImFont* bold = GLT::imgui_config::get_font(GLT::imgui_config::font_type::bold);
        const f32 title_size = FONT_SIZE * z;
        const ImVec4 clip(a.x + 6.0f * z, a.y, b.x - header, a.y + header);       // room for the preview badge
        draw->AddText(bold ? bold : ImGui::GetFont(), title_size, ImVec2(a.x + 8.0f * z, a.y + (header - title_size) * 0.5f),
            IM_COL32(240, 240, 244, 255), GetName().c_str(), nullptr, 0.0f, &clip);

        // Parameters below a faint line
        if (m_type && !m_type->params.empty()) {
            const f32 y = a.y + (m_params_top - SEPARATOR * 0.5f) * z;
            draw->AddLine(ImVec2(a.x + SIDE * z, y), ImVec2(b.x - SIDE * z, y), IM_COL32(255, 255, 255, 18), 1.0f);
        }
        if (m_placeholder) {
            const f32 y = a.y + (HEADER + TOP_GAP + static_cast<f32>(m_pin_rows) * PIN_ROW) * z;
            draw->AddText(nullptr, 0.0f, ImVec2(a.x + 10.0f * z, y + 3.0f * z), IM_COL32(255, 140, 120, 230), "node type not loaded");
        }

        // Relation to the previewed node, shown at the right of the header
        const ImVec2 badge(b.x - header * 0.5f, a.y + header * 0.5f);
        const f32 badge_size = header * 0.22f;
        const bool unrelated_hovered = m_preview_role == preview_role::unrelated && context.bHovered;
        if (m_preview_role == preview_role::target) {
            // "eye": the map shows this node
            draw->AddCircle(badge, badge_size * 1.5f, with_alpha(accent(), 1.0f), 0, std::max(1.0f, 1.2f * z));
            draw->AddCircleFilled(badge, badge_size * 0.7f, with_alpha(accent(), 1.0f));
        } else if (unrelated_hovered) {
            // warning triangle: editing this node does not change the preview
            const f32 s = badge_size * 1.6f;
            draw->AddTriangleFilled(ImVec2(badge.x, badge.y - s), ImVec2(badge.x + s, badge.y + s * 0.8f), ImVec2(badge.x - s, badge.y + s * 0.8f), IM_COL32(240, 170, 50, 255));
            draw->AddLine(ImVec2(badge.x, badge.y - s * 0.4f), ImVec2(badge.x, badge.y + s * 0.25f), IM_COL32(30, 30, 30, 255), std::max(1.0f, 1.2f * z));
        }

        // Border: selection > unrelated (hovered) > hover > faint
        if (context.bSelected)
            draw->AddRect(a, b, with_alpha(accent(), 1.0f), rounding, 0, std::max(1.5f, 2.0f * z));
        else if (unrelated_hovered)
            draw->AddRect(a, b, IM_COL32(240, 170, 50, 200), rounding, 0, std::max(1.0f, 1.5f * z));
        else
            draw->AddRect(a, b, context.bHovered ? IM_COL32(255, 255, 255, 70) : IM_COL32(255, 255, 255, 24), rounding, 0, 1.0f);

        // Tooltip over the header (not over the parameter rows: they have their own)
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (unrelated_hovered && mouse.y >= a.y && mouse.y <= a.y + header && mouse.x >= a.x && mouse.x <= b.x) {
            ImGui::BeginTooltip();
            ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::regular), 14.0f);
            ImGui::TextColored(ImVec4(0.94f, 0.67f, 0.2f, 1.0f), "Not part of the preview");
            ImGui::TextDisabled("This node does not feed '%s' (shown on the map).", m_preview_target.c_str());
            ImGui::TextDisabled("Edits here do not change the preview.");
            ImGui::PopFont();
            ImGui::EndTooltip();
        }
        return true;
    }


    void vns_terrain_node::Draw() {

        if (!m_type || m_type->params.empty() || !ParentArea)
            return;

        const f32 z = ParentArea->GetZoomFactor();
        if (ImGui::GetFontSize() < MIN_WIDGET_FONT)
            return;                                     // far zoomed out: the frame alone

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, FIELD_ROUNDING * z);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * z, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(4.0f * z, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * z, 6.0f * z));       // combo popups

        const ImVec2 origin = LeftTop;
        f32 y = m_params_top;
        for (const param_desc& desc : m_type->params) {
            const f32 h = param_row_height(desc);
            const ImVec2 min(origin.x + SIDE * z, origin.y + (y + 2.0f) * z);
            const ImVec2 max(origin.x + (GetSize().x - SIDE) * z, origin.y + (y + h - 2.0f) * z);

            ImGui::PushID(desc.name.c_str());
            param_value& value = m_params[desc.name];
            if (draw_param(desc, value, min, max, z))
                m_params_dirty = true;
            ImGui::PopID();
            y += h;
        }

        ImGui::PopStyleVar(4);
    }

    // CLASS PRIVATE ===================================================================================================

    bool vns_terrain_node::draw_param(const param_desc& desc, param_value& value, ImVec2 a, ImVec2 b, f32 z) {

        const bool bounded = desc.max > desc.min;
        const f32 width = b.x - a.x;
        const f32 height = b.y - a.y;
        const f32 font = ImGui::GetFontSize();
        const ImU32 fill = with_alpha(accent(), 0.4f);        // engine main colour, not the category colour
        const char* label = desc.name.c_str();

        switch (desc.kind) {

            case param_kind::number: {
                f32 v = detail::convert_param<f32>(value);
                const bool logarithmic = desc.min > 0.0f && desc.max / desc.min > 100.0f;
                const bool changed = value_field("##v", label, v, desc.min, desc.max, false, logarithmic, a, b, z, fill);
                tooltip(desc.tooltip);
                if (changed)
                    value = v;
                return changed;
            }

            case param_kind::integer: {
                f32 v = static_cast<f32>(detail::convert_param<i32>(value));
                const bool changed = value_field("##v", label, v, desc.min, desc.max, true, false, a, b, z, fill);
                tooltip(desc.tooltip);
                if (changed)
                    value = static_cast<i32>(std::lround(v));
                return changed;
            }

            case param_kind::toggle: {
                bool v = detail::convert_param<bool>(value);
                const bool changed = toggle_field("##v", label, v, a, b, z);
                tooltip(desc.tooltip);
                if (changed)
                    value = v;
                return changed;
            }

            case param_kind::vector2: {
                // label | [X ...] [Y ...]
                glm::vec2 v = detail::convert_param<glm::vec2>(value);
                const f32 label_width = width * 0.32f;
                row_label(label, ImVec2(a.x + 6.0f * z, a.y), b, a.x + label_width - 4.0f * z);
                const f32 gap = 4.0f * z;
                const f32 field = (width - label_width - gap) * 0.5f;
                const f32 x0 = a.x + label_width;
                bool changed = value_field("##x", "X", v.x, desc.min, desc.max, false, false, ImVec2(x0, a.y), ImVec2(x0 + field, b.y), z, fill);
                tooltip(desc.tooltip);
                changed |= value_field("##y", "Y", v.y, desc.min, desc.max, false, false, ImVec2(x0 + field + gap, a.y), ImVec2(b.x, b.y), z, fill);
                tooltip(desc.tooltip);
                if (changed)
                    value = v;
                return changed;
            }

            case param_kind::choice: {
                i32 index = detail::convert_param<i32>(value);
                std::vector<const char*> options;
                size_t longest = 0;
                for (const auto& choice : desc.choices) {
                    options.push_back(choice.c_str());
                    longest = std::max(longest, choice.size());
                }
                if (options.empty())
                    return false;
                index = std::clamp(index, 0, static_cast<i32>(options.size()) - 1);

                const f32 label_width = width * 0.32f;
                row_label(label, ImVec2(a.x + 6.0f * z, a.y), b, a.x + label_width - 4.0f * z);
                const ImVec2 control(a.x + label_width, a.y);
                const f32 control_width = width - label_width;

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f * z, std::max(0.0f, (height - font) * 0.5f)));
                push_frame_colors();
                ImGui::SetCursorScreenPos(control);
                bool changed = false;
                if (options.size() <= 3 && longest <= 8)
                    changed = UI::segmented_control("##v", index, options, control_width);
                else {
                    ImGui::SetNextItemWidth(control_width);
                    if (ImGui::BeginCombo("##v", options[static_cast<size_t>(index)], ImGuiComboFlags_HeightLarge)) {
                        for (size_t i = 0; i < options.size(); ++i)
                            if (ImGui::Selectable(options[i], static_cast<i32>(i) == index) && static_cast<i32>(i) != index) {
                                index = static_cast<i32>(i);
                                changed = true;
                            }
                        ImGui::EndCombo();
                    }
                }
                tooltip(desc.tooltip);
                pop_frame_colors();
                ImGui::PopStyleVar();
                if (changed)
                    value = index;
                return changed;
            }

            case param_kind::text: {
                std::string v = detail::convert_param<std::string>(value);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f * z, std::max(0.0f, (PARAM_ROW * z - 4.0f * z - font) * 0.5f)));
                push_frame_colors();
                bool changed = false;

                if (param_row_height(desc) == EXPRESSION_ROW) {
                    // Expression: label + state, a code field, the compile error
                    if (v != m_expression_source) {
                        m_expression_source = v;
                        const auto compiled = expression::compile(v);
                        m_expression_error = compiled ? std::string() : compiled.error();
                    }
                    const f32 line = PARAM_ROW * z - 4.0f * z;
                    row_label(label, ImVec2(a.x + 2.0f * z, a.y), ImVec2(b.x, a.y + line), b.x);
                    const char* state = m_expression_error.empty() ? "ok" : "error";
                    const ImVec2 state_size = ImGui::CalcTextSize(state);
                    ImGui::GetWindowDrawList()->AddText(ImVec2(b.x - state_size.x - 2.0f * z, a.y + (line - font) * 0.5f),
                        m_expression_error.empty() ? IM_COL32(120, 200, 120, 220) : IM_COL32(255, 120, 100, 240), state);

                    const f32 code_top = a.y + line + 2.0f * z;
                    const f32 code_bottom = b.y - line - 2.0f * z;
                    ImGui::SetCursorScreenPos(ImVec2(a.x, code_top));
                    ImFont* mono = GLT::imgui_config::get_font(GLT::imgui_config::font_type::monospace_regular);
                    ImGui::PushFont(mono ? mono : ImGui::GetFont(), (FONT_SIZE - 1.0f) * z);
                    changed = ImGui::InputTextMultiline("##code", &v, ImVec2(width, std::max(code_bottom - code_top, 8.0f)), ImGuiInputTextFlags_AllowTabInput);
                    ImGui::PopFont();
                    tooltip(desc.tooltip);

                    if (!m_expression_error.empty()) {
                        const ImVec4 clip(a.x, b.y - line, b.x, b.y);
                        ImGui::GetWindowDrawList()->AddText(nullptr, 0.0f, ImVec2(a.x + 2.0f * z, b.y - line + (line - font) * 0.5f),
                            IM_COL32(255, 120, 100, 240), m_expression_error.c_str(), nullptr, 0.0f, &clip);
                        ImGui::SetCursorScreenPos(ImVec2(a.x, b.y - line));
                        ImGui::InvisibleButton("##error", ImVec2(width, line));
                        tooltip(m_expression_error);
                    }
                } else {
                    const f32 label_width = width * 0.32f;
                    row_label(label, ImVec2(a.x + 6.0f * z, a.y), b, a.x + label_width - 4.0f * z);
                    ImGui::SetCursorScreenPos(ImVec2(a.x + label_width, a.y));
                    ImGui::SetNextItemWidth(width - label_width);
                    changed = ImGui::InputText("##v", &v);
                    tooltip(desc.tooltip);
                }

                pop_frame_colors();
                ImGui::PopStyleVar();
                if (changed)
                    value = v;
                return changed;
            }
        }
        return false;
    }

}
