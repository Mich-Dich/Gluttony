
#pragma once

#include <terrain/node.h>
#include <VisualNodeSystem.h>



namespace GLT::terrain {

    // @brief The one VisualNodeSystem node class behind every terrain node on the canvas
    //
    // It mirrors the terrain node's pins (sockets by pin name) and its parameters, which are edited right in the node body
    // (one row per parameter, below the pin rows). Edits mark the node dirty; the canvas copies the values into the
    // terrain graph when it compiles. Built-in node types and node packs need no editor code: everything is generated
    // from the node_type. A node whose type is unknown (pack not loaded) is drawn red with the sockets its links use,
    // so nothing is lost when the graph is saved again
    class vns_terrain_node final : public VisNodeSys::Node {
    public:

        static constexpr const char*                    FACTORY_TYPE = "glt_terrain_node";

        // Socket type strings (VisualNodeSystem matches connections by these)
        static constexpr const char*                    SOCKET_HEIGHTFIELD = "HEIGHTFIELD";
        static constexpr const char*                    SOCKET_MASK = "MASK";
        static constexpr const char*                    SOCKET_SCALAR = "SCALAR";

        // Factory default (paste: FromJson fills it)
        vns_terrain_node();

        vns_terrain_node(std::shared_ptr<const node_type> type, u32 graph_id, std::string_view label);

        // Placeholder for an unknown type: sockets named after the links that use them
        vns_terrain_node(std::string_view type_name, u32 graph_id, std::span<const std::string> inputs, std::span<const std::string> outputs);

        // Copy: a new graph node with the same parameters once the canvas compiles
        vns_terrain_node(const vns_terrain_node& other);

        [[nodiscard]] u32 graph_id() const noexcept { return m_graph_id; }
        void set_graph_id(u32 id) noexcept { m_graph_id = id; }

        [[nodiscard]] u32 copied_from() const noexcept { return m_copied_from; }
        void clear_copied_from() noexcept { m_copied_from = 0; }

        [[nodiscard]] const std::string& terrain_type() const noexcept { return m_terrain_type; }
        [[nodiscard]] bool is_placeholder() const noexcept { return m_placeholder; }

        // ---- parameters ----
        // The values shown in the node (every parameter of the type; missing ones at their default)
        [[nodiscard]] const std::map<std::string, param_value>& params() const noexcept { return m_params; }
        void set_params(const std::map<std::string, param_value>& stored);
        void reset_params();

        // A pasted node (made by the factory from JSON) gets its type here: parameter rows, colours, size
        void adopt_type(std::shared_ptr<const node_type> type);
        [[nodiscard]] bool has_type() const noexcept { return m_type != nullptr; }

        // Edited since the canvas last took the values
        [[nodiscard]] bool params_dirty() const noexcept { return m_params_dirty; }
        void clear_params_dirty() noexcept { m_params_dirty = false; }

        // Copy / paste goes through JSON (VisualNodeSystem serializes the selection): keep what the terrain needs
        Json::Value ToJson() override;
        bool FromJson(Json::Value json) override;

        // Registers the factory entry (copy / paste). Idempotent
        static void register_factory();

        // Look of the canvas (grid, wires, font, zoom range, no built-in context menu)
        static void apply_canvas_style(VisNodeSys::NodeArea& area, f32 min_zoom, f32 max_zoom);
        // Colours that follow the engine's main colour (it can change at any time): call every frame
        static void update_canvas_colors(VisNodeSys::NodeArea& area);

        // ---- relation to the node the map previews ----
        enum class preview_role : u8 {
            none,                                       // nothing is previewed
            target,                                     // this node is previewed
            feeds,                                      // its output reaches the previewed node
            unrelated,                                  // edits here do not change the preview
        };
        void set_preview_role(preview_role role, std::string_view target_name);

    protected:

        void Draw() override;
        bool DrawFrame(const VisNodeSys::NodeFrameContext& context) override;
        bool GetSocketLocalPosition(const VisNodeSys::NodeSocket* socket, ImVec2& out_position) const override;

    private:

        // Layout in node units (unzoomed), computed from the pins and parameters
        void relayout();
        [[nodiscard]] f32 param_row_height(const param_desc& desc) const;

        bool draw_param(const param_desc& desc, param_value& value, ImVec2 min, ImVec2 max, f32 zoom);

        u32                                             m_graph_id = 0;
        u32                                             m_copied_from = 0;      // graph id of the copy source, 0 = none
        std::string                                     m_terrain_type{};
        bool                                            m_placeholder = false;
        std::shared_ptr<const node_type>                m_type{};               // null for placeholders

        std::map<std::string, param_value>              m_params{};
        bool                                            m_params_dirty = false;

        ImColor                                         m_accent{ 85, 85, 85 };
        f32                                             m_params_top = 0.0f;    // node units
        u32                                             m_pin_rows = 0;

        preview_role                                    m_preview_role = preview_role::none;
        std::string                                     m_preview_target{};     // name of the previewed node

        std::string                                     m_expression_source{};  // last checked expression ...
        std::string                                     m_expression_error{};   // ... and its compile error ("" = ok)
    };

}
