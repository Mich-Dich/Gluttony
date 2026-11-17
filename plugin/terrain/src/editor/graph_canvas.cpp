
#include "util/pch.h"
#include "graph_canvas.h"

#include <imgui.h>
#include <VisualNodeSystem.h>

#include "util/ui/panel_collection.h"

#include "node_canvas_window.h"         // init_node_system, zoom limits, grid step
#include "vns_terrain_node.h"



namespace GLT::terrain {

    namespace {

        [[nodiscard]] i32 socket_index(const VisNodeSys::Node* node, std::string_view name, VisNodeSys::NodeSocket::SocketFlow flow) {
            const size_t count = flow == VisNodeSys::NodeSocket::SocketFlow::Input ? node->GetInputSocketCount() : node->GetOutputSocketCount();
            for (size_t i = 0; i < count; ++i)
                if (const VisNodeSys::NodeSocket* socket = node->GetSocketByIndex(i, flow); socket && socket->GetName() == name)
                    return static_cast<i32>(i);
            return -1;
        }

        // Pin name of a socket: from the node type by index (the truth), the socket's own name for placeholders
        [[nodiscard]] std::string pin_name(const i_node_registry& registry, const vns_terrain_node& node, const VisNodeSys::NodeSocket& socket) {
            if (!node.is_placeholder())
                if (const std::shared_ptr<const node_type> type = registry.find(node.terrain_type())) {
                    const size_t index = node.GetSocketIndexByID(socket.GetID());
                    const auto& pins = socket.GetFlowDirection() == VisNodeSys::NodeSocket::SocketFlow::Input ? type->inputs : type->outputs;
                    if (index < pins.size())
                        return pins[index].name;
                }
            return socket.GetName();
        }


        [[nodiscard]] std::vector<graph_link> sorted_links(std::vector<graph_link> links) {
            std::sort(links.begin(), links.end(), [](const graph_link& a, const graph_link& b) {
                return std::tie(a.to_node, a.to_pin, a.from_node, a.from_pin) < std::tie(b.to_node, b.to_pin, b.from_node, b.from_pin);
            });
            return links;
        }

    }

    // CLASS IMPLEMENTATION ============================================================================================

    graph_canvas::graph_canvas(const i_node_registry& registry) : m_registry(registry) {}


    graph_canvas::~graph_canvas() {

        if (m_area)
            NODE_SYSTEM.DeleteNodeArea(m_area);
        m_area = nullptr;
    }


    void graph_canvas::load(const terrain_graph& graph, std::string_view layout) {

        init_node_system();
        vns_terrain_node::register_factory();

        const f32 zoom = m_area ? m_area->GetZoomFactor() : 1.0f;
        const ImVec2 offset = m_area ? m_area->GetRenderOffset() : ImVec2(0.0f, 0.0f);
        if (m_area)
            NODE_SYSTEM.DeleteNodeArea(m_area);
        m_area = NODE_SYSTEM.CreateNodeArea();
        m_area->SetIsFillingWindow(true);
        vns_terrain_node::apply_canvas_style(*m_area, MIN_ZOOM, MAX_ZOOM);
        m_area->SetZoomFactor(std::clamp(zoom, MIN_ZOOM, MAX_ZOOM));      // a rebuild keeps the view
        m_area->SetRenderOffset(offset);
        m_area->SetMainContextMenuFunction([this]() { draw_palette(); });
        m_registry_generation = m_registry.generation();
        m_selected = INVALID_NODE_ID;

        // Saved positions
        std::unordered_map<u32, glm::vec2> positions;
        {
            std::istringstream in{ std::string(layout) };
            u32 id = 0;
            f32 x = 0.0f, y = 0.0f;
            while (in >> id >> x >> y)
                positions[id] = glm::vec2(x, y);
        }

        // Automatic layout for the rest: column = longest path from a source, rows in graph order
        std::unordered_map<u32, u32> depth;
        for (size_t pass = 0; pass < graph.nodes.size(); ++pass) {
            bool changed = false;
            for (const auto& link : graph.links) {
                const u32 d = depth[link.from_node] + 1;
                if (d > depth[link.to_node] && d <= graph.nodes.size()) {
                    depth[link.to_node] = d;
                    changed = true;
                }
            }
            if (!changed)
                break;
        }
        std::unordered_map<u32, u32> rows;
        for (const auto& node : graph.nodes) {
            glm::vec2 position(0.0f);
            if (auto it = positions.find(node.id); it != positions.end())
                position = it->second;
            else {
                const u32 column = depth[node.id];
                position = glm::vec2(static_cast<f32>(column) * 300.0f, static_cast<f32>(rows[column]++) * 180.0f);
            }

            // Placeholder sockets: the pins the links use
            std::vector<std::string> inputs, outputs;
            for (const auto& link : graph.links) {
                if (link.to_node == node.id && std::find(inputs.begin(), inputs.end(), link.to_pin) == inputs.end())
                    inputs.push_back(link.to_pin);
                if (link.from_node == node.id && std::find(outputs.begin(), outputs.end(), link.from_pin) == outputs.end())
                    outputs.push_back(link.from_pin);
            }

            vns_terrain_node* created = nullptr;
            if (std::shared_ptr<const node_type> type = m_registry.find(node.type)) {
                created = new vns_terrain_node(std::move(type), node.id, node.label);
                created->set_params(node.params);
            } else
                created = new vns_terrain_node(node.type, node.id, inputs, outputs);
            created->SetPosition(ImVec2(position.x, position.y));
            m_area->AddNode(created);
        }

        // Links
        std::unordered_map<u32, VisNodeSys::Node*> by_id;
        m_area->RunOnEachNode([&](VisNodeSys::Node* n) {
            if (auto* t = dynamic_cast<vns_terrain_node*>(n))
                by_id[t->graph_id()] = n;
        });
        for (const auto& link : graph.links) {
            auto from = by_id.find(link.from_node);
            auto to = by_id.find(link.to_node);
            const i32 out = from != by_id.end() ? socket_index(from->second, link.from_pin, VisNodeSys::NodeSocket::SocketFlow::Output) : -1;
            const i32 in = to != by_id.end() ? socket_index(to->second, link.to_pin, VisNodeSys::NodeSocket::SocketFlow::Input) : -1;
            const bool connected = out >= 0 && in >= 0
                && m_area->TryToConnect(from->second, static_cast<size_t>(out), to->second, static_cast<size_t>(in));
            // The next compile drops it from the graph: say so instead of changing the asset silently
            if (!connected)
                LOG(warn, "terrain graph: link {}.{} -> {}.{} cannot be shown (missing node or pin) and is removed",
                    link.from_node, link.from_pin, link.to_node, link.to_pin)
        }

        m_focus_pending = positions.empty();            // CenterViewOnAllElements needs the area's window (first Update)
    }


    std::optional<terrain_graph> graph_canvas::draw(const terrain_graph& current, u64 revision) {

        // Node types changed (pack reloaded): rebuild sockets. Graph changed elsewhere: show it, don't overwrite it
        if (!m_area || m_registry.generation() != m_registry_generation || revision != m_revision) {
            load(current, m_area ? layout() : std::string());
            m_revision = revision;
        }

        vns_terrain_node::update_canvas_colors(*m_area);
        mark_preview(current);
        m_area->Update();                               // zoom range: apply_canvas_style()

        if (m_focus_pending) {
            m_area->CenterViewOnAllElements();
            m_focus_pending = false;
        }

        // Grid snap on release
        const bool mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (!mouse_down)
            m_area->RunOnEachNode([](VisNodeSys::Node* node) {
                const ImVec2 p = node->GetPosition();
                const ImVec2 snapped(std::round(p.x / GRID_STEP) * GRID_STEP, std::round(p.y / GRID_STEP) * GRID_STEP);
                if (snapped.x != p.x || snapped.y != p.y)
                    node->SetPosition(snapped);
            });

        // Selection
        m_selected = INVALID_NODE_ID;
        for (VisNodeSys::Node* node : m_area->GetSelected())
            if (auto* t = dynamic_cast<vns_terrain_node*>(node)) {
                m_selected = t->graph_id();
                break;
            }

        // Structure -> graph
        std::optional<terrain_graph> result = compile(current);

        if (m_add) {
            terrain_graph next = result ? std::move(*result) : terrain_graph(current);
            const std::shared_ptr<const node_type> type = m_registry.find(m_add->type);
            if (type) {
                const u32 id = next.add_node(type->type_name, type.get());
                auto* created = new vns_terrain_node(type, id, {});
                created->SetPosition(ImVec2(std::round(m_add->position.x / GRID_STEP) * GRID_STEP, std::round(m_add->position.y / GRID_STEP) * GRID_STEP));
                m_area->AddNode(created);
                result = std::move(next);
            }
            m_add.reset();
        }
        return result;
    }


    std::optional<terrain_graph> graph_canvas::compile(const terrain_graph& current) {

        std::vector<vns_terrain_node*> live;
        m_area->RunOnEachNode([&](VisNodeSys::Node* n) {
            if (auto* t = dynamic_cast<vns_terrain_node*>(n))
                live.push_back(t);
        });

        // The graph is copied only once something differs (this runs every frame)
        std::optional<terrain_graph> next;
        auto edit = [&]() -> terrain_graph& {
            if (!next)
                next = current;
            return *next;
        };

        // New canvas nodes (copy / paste): give them graph nodes. Parameters come with the node (JSON), the label from the
        // copy source (same type only)
        for (vns_terrain_node* t : live) {
            if (t->graph_id() != INVALID_NODE_ID && current.find(t->graph_id()))
                continue;
            const graph_node* source = t->copied_from() ? current.find(t->copied_from()) : nullptr;
            const std::shared_ptr<const node_type> type = m_registry.find(t->terrain_type());
            if (type && !t->has_type())
                t->adopt_type(type);
            terrain_graph& g = edit();
            const u32 id = g.add_node(t->terrain_type(), type.get());
            if (t->has_type())
                g.find(id)->params = t->params();
            else if (source && source->type == t->terrain_type())
                g.find(id)->params = source->params;
            if (source && source->type == t->terrain_type())
                g.find(id)->label = source->label;
            t->set_graph_id(id);
            t->clear_copied_from();
            t->clear_params_dirty();
        }

        // Parameters edited in the nodes
        for (vns_terrain_node* t : live) {
            if (!t->params_dirty())
                continue;
            t->clear_params_dirty();
            const graph_node* node = current.find(t->graph_id());
            if (node && node->params != t->params())
                edit().find(t->graph_id())->params = t->params();
        }

        // Deleted canvas nodes
        std::unordered_set<u32> present;
        for (vns_terrain_node* t : live)
            present.insert(t->graph_id());
        for (const auto& node : current.nodes)
            if (!present.contains(node.id))
                edit().remove_node(node.id);

        // Links from the sockets. An input keeps only its newest connection
        std::vector<graph_link> links;
        for (vns_terrain_node* t : live) {
            for (size_t i = 0; i < t->GetInputSocketCount(); ++i) {
                VisNodeSys::NodeSocket* socket = t->GetSocketByIndex(i, VisNodeSys::NodeSocket::SocketFlow::Input);
                if (!socket)
                    continue;
                const std::vector<VisNodeSys::NodeSocket*> connected = socket->GetConnectedSockets();     // copy: may change below
                if (connected.empty())
                    continue;
                VisNodeSys::NodeSocket* source = connected.back();
                if (connected.size() > 1) {
                    m_area->TryToDisconnect(t, socket->GetID());
                    m_area->TryToConnect(source->GetParent(), source->GetID(), t, socket->GetID());
                }
                auto* from = dynamic_cast<vns_terrain_node*>(source->GetParent());
                if (!from)
                    continue;                           // e.g. a sub-area boundary node: not supported by the terrain graph yet
                links.push_back({ from->graph_id(), pin_name(m_registry, *from, *source), t->graph_id(), pin_name(m_registry, *t, *socket) });
            }
        }

        if (sorted_links(links) != sorted_links(current.links))
            edit().links = std::move(links);
        return next;
    }


    void graph_canvas::mark_preview(const terrain_graph& graph) {

        // Everything upstream of the previewed node (walking the links backwards)
        std::unordered_set<u32> upstream;
        std::string target_name;
        if (const graph_node* target = m_preview != INVALID_NODE_ID ? graph.find(m_preview) : nullptr) {
            std::unordered_multimap<u32, u32> sources;          // to_node -> from_node
            for (const auto& link : graph.links)
                sources.emplace(link.to_node, link.from_node);

            std::vector<u32> stack{ m_preview };
            while (!stack.empty()) {
                const u32 id = stack.back();
                stack.pop_back();
                if (!upstream.insert(id).second)
                    continue;
                const auto [begin, end] = sources.equal_range(id);
                for (auto it = begin; it != end; ++it)
                    stack.push_back(it->second);
            }

            target_name = target->label;
            if (target_name.empty())
                if (const std::shared_ptr<const node_type> type = m_registry.find(target->type))
                    target_name = type->display_name;
        }

        using role = vns_terrain_node::preview_role;
        m_area->RunOnEachNode([&](VisNodeSys::Node* n) {
            auto* t = dynamic_cast<vns_terrain_node*>(n);
            if (!t)
                return;
            role r = role::none;
            if (!upstream.empty())
                r = t->graph_id() == m_preview ? role::target : upstream.contains(t->graph_id()) ? role::feeds : role::unrelated;
            t->set_preview_role(r, target_name);
        });
    }


    void graph_canvas::draw_palette() {

        // Called by VisualNodeSystem inside its context popup. On a node: no palette (it would add a node on top of it)
        VisNodeSys::NodeAreaContextMenuOpenState state = m_area->GetContextMenuOpenState();
        if (VisNodeSys::Node* clicked = state.GetNode()) {
            ImGui::TextDisabled("%s", clicked->GetName().c_str());
            ImGui::Separator();
            if (auto* t = dynamic_cast<vns_terrain_node*>(clicked); t && t->has_type() && !t->params().empty())
                if (ImGui::MenuItem("Reset parameters"))
                    t->reset_params();
            if (ImGui::MenuItem("Delete node"))
                m_area->Delete(clicked);
            ImGui::TextDisabled("Del: delete   Ctrl+C / Ctrl+V: copy / paste");
            return;
        }

        if (ImGui::IsWindowAppearing()) {
            m_filter[0] = '\0';
            ImGui::SetKeyboardFocusHere();
        }
        UI::search_field("##filter", m_filter, sizeof(m_filter), "Search nodes", 240.0f);
        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        auto types = m_registry.all();
        std::sort(types.begin(), types.end(), [](const auto& a, const auto& b) {
            return std::tie(a->category, a->display_name) < std::tie(b->category, b->display_name);
        });

        const glm::vec2 position(m_area->GetContextMenuOpenState().MousePositionRecorded.x, m_area->GetContextMenuOpenState().MousePositionRecorded.y);
        const std::string filter = GLT::util::to_lower(m_filter);

        if (!filter.empty()) {
            for (const auto& type : types) {
                if (GLT::util::to_lower(type->display_name).find(filter) == std::string::npos)
                    continue;
                if (ImGui::MenuItem(std::format("{}  ({})", type->display_name, type->category).c_str()))
                    m_add = add_request{ type->type_name, position };
            }
            return;
        }

        std::string open_category;
        bool open = false;
        for (const auto& type : types) {
            if (type->category != open_category) {
                if (open)
                    ImGui::EndMenu();
                open_category = type->category;
                open = ImGui::BeginMenu(open_category.c_str());
            }
            if (open && ImGui::MenuItem(type->display_name.c_str()))
                m_add = add_request{ type->type_name, position };
            if (open && !type->description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", type->description.c_str());
        }
        if (open)
            ImGui::EndMenu();
    }


    std::string graph_canvas::layout() const {

        std::string out;
        if (!m_area)
            return out;
        m_area->RunOnEachNode([&](VisNodeSys::Node* n) {
            if (auto* t = dynamic_cast<vns_terrain_node*>(n); t && t->graph_id() != INVALID_NODE_ID)
                out += std::format("{} {} {}\n", t->graph_id(), n->GetPosition().x, n->GetPosition().y);
        });
        return out;
    }


    void graph_canvas::rename(u32 graph_id, std::string_view title) {

        if (!m_area)
            return;
        m_area->RunOnEachNode([&](VisNodeSys::Node* n) {
            if (auto* t = dynamic_cast<vns_terrain_node*>(n); t && t->graph_id() == graph_id)
                t->SetName(std::string(title));
        });
    }


    void graph_canvas::focus_all() {

        if (m_area)
            m_area->CenterViewOnAllElements();
    }

}
