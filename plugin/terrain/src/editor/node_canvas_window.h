
#pragma once

#include <editor/i_editor.h>



// FORWARD DECLARATIONS ================================================================================================

namespace VisNodeSys {
    class NodeArea;
}

namespace GLT::terrain {

    // CONSTANTS =======================================================================================================

    // Socket types of the terrain graph (VisualNodeSystem matches connections by these strings)
    inline constexpr const char*                                SOCKET_HEIGHTFIELD = "HEIGHTFIELD";
    inline constexpr const char*                                SOCKET_MASK = "MASK";
    inline constexpr const char*                                SOCKET_SCALAR = "SCALAR";

    // Canvas zoom range. VisualNodeSystem allows 0.05 (patched in CMakeLists.txt) .. 5.0; far in, every zoom step
    // rebuilds huge font glyphs
    inline constexpr f32                                        MIN_ZOOM = 0.08f;
    inline constexpr f32                                        MAX_ZOOM = 1.25f;

    // Node positions snap to this (the canvas grid: VisualNodeSystem's NODE_GRID_STEP)
    inline constexpr f32                                        GRID_STEP = 15.0f;

    // FUNCTION DECLARATION ============================================================================================

    // NODE_SYSTEM.Initialize(), node type registration, socket colors. Idempotent; needs a live ImGui context (adds the
    // zoom fonts to the atlas)
    void init_node_system();

    // CLASS DECLARATION ===============================================================================================

    // T0 spike: a VisualNodeSystem canvas inside an editor window, with a few demo nodes. Proves the library builds against
    // the engine's ImGui, renders, zooms (fonts) and connects. Replaced by the terrain graph editor in T2
    class node_canvas_window final : public GLT::editor::i_editor_window {
    public:

        node_canvas_window();
        ~node_canvas_window() override;

        node_canvas_window(const node_canvas_window&) = delete;
        node_canvas_window& operator=(const node_canvas_window&) = delete;

        [[nodiscard]] std::string title() const override { return "Terrain Node Canvas (test)"; }
        [[nodiscard]] int window_flags() const override;

        void draw_content(f32 delta_time) override;

    private:

        void add_demo_nodes();

        VisNodeSys::NodeArea*                                   m_area = nullptr;       // owned by NODE_SYSTEM, deleted in the destructor
    };

}
