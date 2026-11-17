
#include "util/pch.h"
#include "node_canvas_window.h"

#include <imgui.h>
#include <VisualNodeSystem.h>



namespace GLT::terrain {

    // TYPES ===========================================================================================================

    // Demo node kinds of the T0 spike. The real node library (T1) replaces this with one adapter class over the terrain
    // node SDK (documentation/terrain_generation.md, "VisualNodeSystem integration")
    enum class demo_kind : u8 { noise, terrace, slope_mask, blend, output, count };


    struct demo_info {

        const char*                                             type;       // factory key, stored in the graph JSON
        const char*                                             name;
        std::vector<std::pair<const char*, const char*>>        inputs;     // { socket type, label }
        std::vector<std::pair<const char*, const char*>>        outputs;
    };


    static const demo_info& info_of(demo_kind kind) {

        static const demo_info s_infos[] = {
            { "glt_terrain_demo_noise",      "Perlin Noise", {},                                                    { { SOCKET_HEIGHTFIELD, "height" } } },
            { "glt_terrain_demo_terrace",    "Terrace",      { { SOCKET_HEIGHTFIELD, "height" } },                  { { SOCKET_HEIGHTFIELD, "height" } } },
            { "glt_terrain_demo_slope_mask", "Slope Mask",   { { SOCKET_HEIGHTFIELD, "height" } },                  { { SOCKET_MASK, "mask" } } },
            { "glt_terrain_demo_blend",      "Blend",        { { SOCKET_HEIGHTFIELD, "a" }, { SOCKET_HEIGHTFIELD, "b" }, { SOCKET_MASK, "mask" } },
                                                                                                                    { { SOCKET_HEIGHTFIELD, "height" } } },
            { "glt_terrain_demo_output",     "Terrain Output", { { SOCKET_HEIGHTFIELD, "height" } },                {} },
        };
        return s_infos[static_cast<size_t>(kind)];
    }


    class demo_node final : public VisNodeSys::Node {
    public:

        explicit demo_node(demo_kind kind) : m_kind(kind) {

            const demo_info& info = info_of(kind);
            Type = info.type;
            SetName(info.name);
            for (const auto& [type, label] : info.inputs)
                AddSocket(new VisNodeSys::NodeSocket(this, type, label, VisNodeSys::NodeSocket::SocketFlow::Input));
            for (const auto& [type, label] : info.outputs)
                AddSocket(new VisNodeSys::NodeSocket(this, type, label, VisNodeSys::NodeSocket::SocketFlow::Output));
        }

        [[nodiscard]] demo_kind kind() const { return m_kind; }

    private:

        demo_kind                                               m_kind;
    };

    // FUNCTION IMPLEMENTATION =========================================================================================

    void init_node_system() {

        static bool s_initialized = false;
        if (s_initialized)
            return;
        s_initialized = true;

        NODE_SYSTEM.Initialize();
        NODE_SYSTEM.AssociateSocketTypeToColor(SOCKET_HEIGHTFIELD, ImColor(110, 190, 90));
        NODE_SYSTEM.AssociateSocketTypeToColor(SOCKET_MASK,        ImColor(200, 200, 200));
        NODE_SYSTEM.AssociateSocketTypeToColor(SOCKET_SCALAR,      ImColor(90, 160, 230));

        for (u8 index = 0; index < static_cast<u8>(demo_kind::count); ++index) {

            const auto kind = static_cast<demo_kind>(index);
            NODE_FACTORY.RegisterNodeType(info_of(kind).type,
                [kind]() -> VisNodeSys::Node* { return new demo_node(kind); },
                [](const VisNodeSys::Node& other) -> VisNodeSys::Node* { return new demo_node(static_cast<const demo_node&>(other).kind()); });
        }
        LOG(info, "VisualNodeSystem [{}] initialized", NODE_SYSTEM.GetVersion());
    }

    // CLASS PUBLIC ====================================================================================================

    node_canvas_window::node_canvas_window() {

        init_node_system();
        m_area = NODE_SYSTEM.CreateNodeArea();
        m_area->SetName("terrain test canvas");
        m_area->SetIsFillingWindow(true);
        m_area->SetZoomLimits(MIN_ZOOM, MAX_ZOOM);
        add_demo_nodes();
    }


    node_canvas_window::~node_canvas_window() {

        if (m_area)
            NODE_SYSTEM.DeleteNodeArea(m_area);
        m_area = nullptr;
    }


    int node_canvas_window::window_flags() const {

        // the mouse wheel zooms the canvas instead of scrolling the window
        return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    }


    void node_canvas_window::draw_content(f32 /*delta_time*/) {

        if (!m_area)
            return;

        m_area->Update();

        // Grid snap: nodes move freely while dragged and land on the grid when the mouse is released (also places nodes
        // created from the context menu on the grid)
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            m_area->RunOnEachNode([](VisNodeSys::Node* node) {
                const ImVec2 p = node->GetPosition();
                const ImVec2 snapped(std::round(p.x / GRID_STEP) * GRID_STEP, std::round(p.y / GRID_STEP) * GRID_STEP);
                if (snapped.x != p.x || snapped.y != p.y)
                    node->SetPosition(snapped);
            });
        }
    }

    // CLASS PRIVATE ===================================================================================================

    void node_canvas_window::add_demo_nodes() {

        auto add = [this](demo_kind kind, f32 x, f32 y) -> VisNodeSys::Node* {
            auto* node = new demo_node(kind);
            node->SetPosition(ImVec2(x, y));
            m_area->AddNode(node);
            return node;
        };

        VisNodeSys::Node* noise   = add(demo_kind::noise,      40.f,  60.f);
        VisNodeSys::Node* terrace = add(demo_kind::terrace,    300.f, 20.f);
        VisNodeSys::Node* slope   = add(demo_kind::slope_mask, 300.f, 180.f);
        VisNodeSys::Node* blend   = add(demo_kind::blend,      560.f, 80.f);
        VisNodeSys::Node* output  = add(demo_kind::output,     820.f, 100.f);

        m_area->TryToConnect(noise, 0, terrace, 0);
        m_area->TryToConnect(noise, 0, slope, 0);
        m_area->TryToConnect(noise, 0, blend, 0);
        m_area->TryToConnect(terrace, 0, blend, 1);
        m_area->TryToConnect(slope, 0, blend, 2);
        m_area->TryToConnect(blend, 0, output, 0);
    }

}
