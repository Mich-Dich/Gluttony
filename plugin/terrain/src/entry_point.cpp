
#include "util/pch.h"

#include <plugin_system/i_plugin.h>
#include <asset/i_asset_registry.h>
#include <editor/i_editor.h>
#include <event/event_bus.h>
#include <event/application_event.h>

#include <terrain/node.h>

#include "asset/terrain_graph_asset.h"
#include "editor/node_canvas_window.h"
#include "editor/terrain_editor_window.h"
#include "graph/evaluator.h"
#include "graph/node_registry.h"
#include "nodes/node_helpers.h"
#include "test/self_test.h"
#include "world/terrain_component.h"
#include "world/terrain_world_system.h"



namespace GLT::terrain {

    // STATIC VARIABLES ================================================================================================

    static constexpr const char*                                dependencies_names[] = {

        nullptr
    };

    static constexpr GLT::plugin_manager::interface             dependencies_interfaces[] = {

        GLT::plugin_manager::interface::asset_registry,
        GLT::plugin_manager::interface::world,
    };

    static constexpr GLT::plugin_manager::plugin_descriptor     descriptor = {

        .name                                                   = GLT_MODULE_NAME,
        .display_name                                           = "Terrain Generator",
        .description                                            = "Node graph based terrain generation with preview, live streaming and bake",
        .category                                               = "World|Procedural",
        .author                                                 = "Mich-Dich",
        .plugin_version                                         = {0, 2, 0},
        .api_version                                            = {1, 0, 0},
        .load_phase                                             = GLT::plugin_manager::phase::application_ready,
        .unload_phase                                           = GLT::plugin_manager::phase::pre_application_shutdown,
        .target                                                 = GLT::plugin_manager::interface::custom,
        .dependency_names_count                                 = ARRAY_SIZE(dependencies_names),
        .dependency_names                                       = dependencies_names,
        .dependency_interface_count                             = ARRAY_SIZE(dependencies_interfaces),
        .dependency_interfaces                                  = dependencies_interfaces,
    };

    // FUNCTION IMPLEMENTATION =========================================================================================

    // The graph a new terrain asset starts with: fBm hills plus one mountain, into the surface output
    static terrain_graph make_default_graph(const i_node_registry& registry) {

        terrain_graph graph{};
        auto add = [&](const char* type) { return graph.add_node(type, registry.find(type).get()); };

        const u32 hills = add("terrain.fbm");
        const u32 mountain = add("terrain.mountain");
        const u32 sum = add("terrain.add");
        const u32 surface = add("terrain.surface");

        graph.find(mountain)->params["Radius"] = 1500.0f;
        graph.find(mountain)->params["Height"] = 400.0f;

        graph.connect(hills, "out", sum, "a");
        graph.connect(mountain, "out", sum, "b");
        graph.connect(sum, "out", surface, "height");
        return graph;
    }

    // CLASS DECLARATION ===============================================================================================

    // The runtime part (asset type, node registry, evaluator) works without the editor; the editor part is registered
    // through an editor hook, so it appears whether the editor loads before or after this plugin
    class plugin final : public GLT::plugin_manager::i_plugin, public i_terrain {
    public:

        void on_load() override {

            nodes::register_builtin(m_nodes, GLT_MODULE_NAME);
            m_evaluator = std::make_unique<evaluator>(m_nodes);

            m_registry = GLT::asset::registry::get_ref();
            if (m_registry) {
                m_graph_type = m_registry->reserve_type(TERRAIN_GRAPH_TYPE_NAME);
                m_handler.set_type(m_graph_type);
                m_registry->register_handler(&m_handler);
            } else {
                LOG(error, "terrain: no asset registry, terrain graph assets are unavailable")
            }

            // World side: the terrain component (world entity) and the system that streams / bakes it
            m_world_system = std::make_unique<terrain_world_system>(*m_evaluator);
            terrain_component_actions actions{};
            actions.bake = [this](GLT::world::i_world& world) { m_world_system->bake(world); };
            actions.clear_bake = [this](GLT::world::i_world& world) { m_world_system->clear_bake(world); };
            actions.status = [this](GLT::world::i_world& world) { return m_world_system->status(world); };
            register_terrain_component(m_graph_type, std::move(actions));
            m_update = GLT::event_bus::subscribe_scoped<GLT::update_event>([this](GLT::update_event&) {
                if (m_world_system)
                    m_world_system->update();
            });

            GLT::editor::add_editor_hook(GLT_MODULE_NAME, [this](GLT::editor::i_editor& editor) { register_editor(editor); });
            LOG(info, "terrain plugin loaded: {} node type(s)", m_nodes.all().size())
        }


        void on_unload() override {

            GLT::editor::remove_editor_hook(GLT_MODULE_NAME);
            if (GLT::editor::i_editor* editor = GLT::editor::get())
                editor->unregister_all(GLT_MODULE_NAME);            // closes our windows before this library goes away

            // Generated chunk entities leave the worlds; the component's values become payloads the worlds keep (saved
            // with them, back when the plugin is loaded again)
            m_update.reset();
            if (m_world_system)
                m_world_system->shutdown();
            unregister_terrain_component();

            // Evaluations still queued on the thread pool reference the evaluator and node code of this library
            GLT::thread_pool::wait_for_all();
            m_world_system.reset();

            if (m_registry)
                m_registry->unregister_handler(&m_handler);         // drops loaded terrain graphs
            m_registry.reset();

            m_evaluator.reset();
            m_nodes.clear();
            LOG(info, "terrain plugin unloaded")
        }

        // i_terrain ---------------------------------------------------------------------------------------------------

        [[nodiscard]] i_node_registry& nodes() override { return m_nodes; }

    private:

        void register_editor(GLT::editor::i_editor& editor) {

            if (m_registry && m_graph_type != GLT::asset::core_types::invalid) {

                editor.register_asset_editor(GLT_MODULE_NAME, m_graph_type, [this](const std::filesystem::path& path) -> GLT::unique_ref<GLT::editor::i_editor_window> {
                    if (!m_registry)
                        return nullptr;
                    auto loaded = m_registry->load(path);
                    if (!loaded) {
                        LOG(error, "terrain: could not load [{}]", path.generic_string())
                        return nullptr;
                    }
                    return GLT::create_unique_ref<terrain_editor_window>(path, std::move(*loaded), m_nodes, *m_evaluator);
                });

                GLT::editor::asset_creator creator{};
                creator.type = m_graph_type;
                creator.label = "Terrain Graph";
                creator.tooltip = "A node graph that generates terrain: noise, shapes, erosion, masks -> surface";
                creator.default_name = "New Terrain";
                creator.create = [this](const std::filesystem::path& content_path) -> std::expected<void, std::string> {
                    if (!m_registry)
                        return std::unexpected(std::string("no asset registry"));
                    auto asset = GLT::create_unique_ref<terrain_graph_asset>(m_graph_type);
                    asset->set_graph(make_default_graph(m_nodes));
                    auto registered = m_registry->register_runtime(std::move(asset), content_path, content_path.stem().string());
                    if (!registered)
                        return std::unexpected(std::format("could not register the asset (error {})", static_cast<int>(registered.error())));
                    if (auto saved = m_registry->save(registered->get()); !saved)
                        return std::unexpected(std::format("could not write the file (error {})", static_cast<int>(saved.error())));
                    return {};
                };
                editor.register_asset_creator(GLT_MODULE_NAME, std::move(creator));
            }

            editor.register_menu_item(GLT_MODULE_NAME, "Terrain/Run self test", [this] {
                const self_test_report report = run_self_test(m_nodes);
                if (report.failed == 0)
                    LOG(info, "terrain self test: all {} check(s) passed ({:.0f} ms)", report.passed, report.milliseconds)
                else {
                    LOG(error, "terrain self test: {} failed, {} passed ({:.0f} ms)", report.failed, report.passed, report.milliseconds)
                    for (const auto& failure : report.failures)
                        LOG(error, "  {}", failure)
                }
            });

            editor.register_menu_item(GLT_MODULE_NAME, "Terrain/Node Canvas (test)", [] {
                if (GLT::editor::i_editor* current = GLT::editor::get())
                    current->open_window(GLT_MODULE_NAME, GLT::create_unique_ref<node_canvas_window>());
            });
        }

        node_registry                                           m_nodes{};
        std::unique_ptr<evaluator>                              m_evaluator{};
        std::unique_ptr<terrain_world_system>                   m_world_system{};
        GLT::event_bus::subscription_guard                      m_update{};
        terrain_graph_handler                                   m_handler{};
        ref<GLT::asset::i_asset_registry_plugin>                m_registry{};
        GLT::asset::type                                        m_graph_type{ GLT::asset::core_types::invalid };
    };

}

EXPORT_PLUGIN_CLASS(GLT::terrain::plugin, GLT::terrain::descriptor)
