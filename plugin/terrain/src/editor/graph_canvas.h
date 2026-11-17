
#pragma once

#include <terrain/node.h>

#include "graph/graph.h"



namespace VisNodeSys {
    class NodeArea;
}

namespace GLT::terrain {

    // @brief The node canvas of the terrain editor: a VisualNodeSystem area kept in sync with a terrain graph
    //
    // The terrain graph is the truth for nodes, parameters and links; the canvas adds positions. Every frame the canvas is
    // compiled back into a graph (nodes present, links between sockets, parameters edited in the node bodies); if that
    // differs from the current graph, draw() returns the new graph. Adding a node (right-click palette), deleting,
    // connecting, disconnecting, editing a parameter and copy / paste therefore all end up as a new graph without
    // per-operation code
    class graph_canvas {
    public:

        explicit graph_canvas(const i_node_registry& registry);
        ~graph_canvas();

        graph_canvas(const graph_canvas&) = delete;
        graph_canvas& operator=(const graph_canvas&) = delete;

        // Rebuilds the canvas. [layout]: "id x y" per line (positions), missing nodes are laid out automatically
        void load(const terrain_graph& graph, std::string_view layout);

        // Draws into the current ImGui window / child. Returns a new graph when the user changed the structure.
        // [revision]: the asset's graph revision; a revision the canvas did not acknowledge means the graph was changed
        // elsewhere (reload, another editor): the canvas is rebuilt from it instead of overwriting it
        [[nodiscard]] std::optional<terrain_graph> draw(const terrain_graph& current, u64 revision);

        // The owner applied a graph (from draw() or its own edits): this revision is the canvas's own
        void acknowledge(u64 revision) noexcept { m_revision = revision; }

        // Graph id of the (first) selected node, INVALID_NODE_ID if none
        [[nodiscard]] u32 selected() const noexcept { return m_selected; }

        // Node positions, for CHUNK_EDITOR
        [[nodiscard]] std::string layout() const;

        // A node's title changed (label edited in the details panel)
        void rename(u32 graph_id, std::string_view title);

        // Frames all nodes
        void focus_all();

        // The node the map shows: nodes that do not feed it warn when hovered (INVALID_NODE_ID = none)
        void set_preview_node(u32 graph_id) noexcept { m_preview = graph_id; }

    private:

        struct add_request {
            std::string                                 type;
            glm::vec2                                   position{ 0.0f };
        };

        [[nodiscard]] std::optional<terrain_graph> compile(const terrain_graph& current);
        void draw_palette();
        void mark_preview(const terrain_graph& graph);

        const i_node_registry&                          m_registry;
        VisNodeSys::NodeArea*                           m_area = nullptr;
        std::optional<add_request>                      m_add{};
        u32                                             m_selected = 0;
        u32                                             m_preview = INVALID_NODE_ID;
        u64                                             m_registry_generation = 0;
        u64                                             m_revision = 0;
        bool                                            m_focus_pending = false;   // frame all nodes after the first Update()
        char                                            m_filter[64]{};
    };

}
