
#include "util/pch.h"
#include "terrain_world_system.h"

#include <application.h>
#include <asset/i_asset_registry.h>
#include <game/game_layer.h>
#include <game/game_session.h>
#include <world/i_world.h>

#include "asset/terrain_graph_asset.h"
#include "chunk_mesh.h"
#include "terrain_component.h"



namespace GLT::terrain {

    namespace {

        constexpr const char*                           SURFACE_TYPE = "terrain.surface";
        constexpr const char*                           BAKED_ROOT_NAME = "Terrain (baked)";
        constexpr u32                                   MAX_BAKED_CHUNKS = 4096;

        [[nodiscard]] const terrain_graph_asset* graph_asset_of(const terrain_component& c) {
            auto* registry = GLT::asset::registry::get_ptr();
            if (!registry || c.graph.get() == INVALID_HANDLE)
                return nullptr;
            return dynamic_cast<const terrain_graph_asset*>(registry->data(c.graph.get()));
        }

        [[nodiscard]] u32 surface_of(const terrain_graph& graph) {
            for (const auto& node : graph.nodes)
                if (node.type == SURFACE_TYPE)
                    return node.id;
            return INVALID_NODE_ID;
        }

        [[nodiscard]] GLT::world::entity_id find_baked_root(GLT::world::i_world& world) {
            for (const GLT::world::entity_id id : world.root_entities())
                if (world.entity_name(id) == BAKED_ROOT_NAME)
                    return id;
            return {};
        }

    }

    // CLASS IMPLEMENTATION ============================================================================================

    terrain_world_system::terrain_world_system(evaluator& eval) : m_eval(eval) {}


    terrain_world_system::~terrain_world_system() {

        // shutdown() despawns in live worlds; whatever is left only releases its meshes
        for (auto& [world, entry] : m_worlds)
            entry.streamer->clear(false);
        m_worlds.clear();
    }


    std::vector<GLT::world::i_world*> terrain_world_system::live_worlds() const {

        std::vector<GLT::world::i_world*> out;
        if (const auto* layer = GLT::application::get().get_game_layer_ptr())
            for (GLT::game::game_session* session : layer->get_sessions())
                if (session)
                    if (GLT::world::i_world* world = session->get_world())
                        out.push_back(world);
        return out;
    }


    void terrain_world_system::update() {

        const std::vector<GLT::world::i_world*> worlds = live_worlds();

        // Worlds that are gone (or replaced by another world at the same address): their entities went with them
        for (auto it = m_worlds.begin(); it != m_worlds.end(); ) {
            const bool alive = std::find(worlds.begin(), worlds.end(), it->first) != worlds.end()
                && it->first->instance_id() == it->second.instance_id;
            if (alive) {
                ++it;
                continue;
            }
            it->second.streamer->clear(false);
            it = m_worlds.erase(it);
        }
        std::erase_if(m_bake_status, [&](const auto& entry) { return std::find(worlds.begin(), worlds.end(), entry.first) == worlds.end(); });

        for (GLT::world::i_world* world : worlds) {

            const terrain_component* c = terrain_of(*world);
            const terrain_graph_asset* asset = c ? graph_asset_of(*c) : nullptr;
            const bool live = c && c->enabled && c->mode == terrain_mode::live && asset;

            auto found = m_worlds.find(world);
            if (!live) {
                if (found != m_worlds.end()) {
                    found->second.streamer->clear(true);
                    m_worlds.erase(found);
                }
                continue;
            }

            if (found == m_worlds.end()) {
                world_entry entry{};
                entry.streamer = std::make_unique<terrain_streamer>(m_eval, *world);
                entry.instance_id = world->instance_id();
                found = m_worlds.emplace(world, std::move(entry)).first;
            }
            found->second.streamer->update(*c, asset->graph(), asset->revision());
        }
    }


    void terrain_world_system::shutdown() {

        const std::vector<GLT::world::i_world*> worlds = live_worlds();
        for (auto& [world, entry] : m_worlds) {
            const bool alive = std::find(worlds.begin(), worlds.end(), world) != worlds.end() && world->instance_id() == entry.instance_id;
            entry.streamer->clear(alive);
        }
        m_worlds.clear();
        m_bake_status.clear();
    }


    std::string terrain_world_system::status(GLT::world::i_world& world) const {

        if (auto it = m_worlds.find(&world); it != m_worlds.end()) {
            const auto& s = it->second.streamer->last_stats();
            std::string text = std::format("live: {} / {} chunks ready, {} evaluating", s.ready, s.chunks, s.running);
            if (!s.error.empty())
                text += "  |  " + s.error;
            return text;
        }
        if (auto it = m_bake_status.find(&world); it != m_bake_status.end())
            return it->second;
        if (const terrain_component* c = terrain_of(world)) {
            if (!graph_asset_of(*c))
                return "no terrain graph";
            if (!c->enabled)
                return "disabled";
        }
        return {};
    }


    void terrain_world_system::clear_bake(GLT::world::i_world& world) {

        // The root's children (chunks in loaded regions), plus chunks that came back as orphans (their region was streamed
        // out while the root went). Chunks in regions that are not loaded right now are not reached
        u32 removed = 0;
        const GLT::world::entity_id root = find_baked_root(world);
        if (root.is_valid()) {
            for (const GLT::world::entity_id child : world.children_of(root)) {
                world.despawn(child);
                ++removed;
            }
            world.despawn(root);
        }
        for (const GLT::world::entity_id id : world.root_entities())
            if (world.entity_name(id).starts_with("chunk_L") && !world.has_children(id)) {
                world.despawn(id);
                ++removed;
            }
        if (removed == 0 && !root.is_valid())
            return;
        m_bake_status[&world] = std::format("bake cleared ({} chunk entities removed; the mesh files stay in the bake folder)", removed);
    }


    void terrain_world_system::bake(GLT::world::i_world& world) {

        const terrain_component* c = terrain_of(world);
        const terrain_graph_asset* asset = c ? graph_asset_of(*c) : nullptr;
        if (!asset) {
            m_bake_status[&world] = "bake: no terrain graph";
            return;
        }
        const std::shared_ptr<const terrain_graph> graph = asset->graph();
        const u32 surface = surface_of(*graph);
        if (surface == INVALID_NODE_ID) {
            m_bake_status[&world] = "bake: the terrain graph has no Surface output";
            return;
        }

        // The chunks of the bake level covering the extent
        const u32 quads = c->chunk_quads;
        const u32 level = std::min(c->bake_level, 8u);
        const f64 spacing = std::ldexp(static_cast<f64>(c->chunk_size) / quads, static_cast<i32>(level));
        const f64 size = spacing * quads;
        const i64 x0 = static_cast<i64>(std::floor(c->bake_min.x / size));
        const i64 y0 = static_cast<i64>(std::floor(c->bake_min.y / size));
        const i64 x1 = std::max(x0 + 1, static_cast<i64>(std::ceil(c->bake_max.x / size)));
        const i64 y1 = std::max(y0 + 1, static_cast<i64>(std::ceil(c->bake_max.y / size)));
        const u64 count = static_cast<u64>(x1 - x0) * static_cast<u64>(y1 - y0);
        if (count > MAX_BAKED_CHUNKS) {
            m_bake_status[&world] = std::format("bake: {} chunks is too many (at most {}): use a coarser bake level or a smaller extent", count, MAX_BAKED_CHUNKS);
            return;
        }

        clear_bake(world);

        // A folder per bake: re-baking never collides with the meshes of an earlier bake
        std::string world_name = "world";
        if (auto* registry = GLT::asset::registry::get_ptr(); registry && world.asset_handle() != INVALID_HANDLE)
            world_name = registry->info(world.asset_handle()).virtual_path.stem().string();
        const u64 stamp = static_cast<u64>(std::chrono::system_clock::now().time_since_epoch() / std::chrono::seconds(1));
        const std::filesystem::path folder = std::filesystem::path(c->bake_folder.empty() ? "terrain" : c->bake_folder) / std::format("{}_{}", world_name, stamp);

        const auto started = std::chrono::steady_clock::now();
        const std::unordered_map<std::string, f32> parameters = override_map(*c);
        auto& store = GLT::render::procedural_meshes();

        const GLT::world::entity_id root = world.spawn();
        world.set_entity_name(root, BAKED_ROOT_NAME);

        u32 baked = 0, failed = 0;
        for (i64 iy = y0; iy < y1; ++iy)
            for (i64 ix = x0; ix < x1; ++ix) {

                region vertices{};
                vertices.x0 = ix * static_cast<i64>(quads);
                vertices.y0 = iy * static_cast<i64>(quads);
                vertices.width = quads + 1;
                vertices.height = quads + 1;
                vertices.spacing = spacing;

                eval_request request{};
                request.graph = graph;
                request.node = surface;
                request.pin = "height";
                request.seed = c->seed;
                request.parameters = parameters;
                request.area = vertices.expanded(1);

                const eval_outcome outcome = m_eval.evaluate(request);
                if (!outcome || !outcome->data) {
                    ++failed;
                    if (!outcome)
                        LOG(warn, "terrain bake: chunk ({}, {}) failed: {}", ix, iy, outcome.error())
                    continue;
                }

                // Vertices relative to the chunk's corner: the entity sits there, so the world puts it in the right region
                chunk_mesh_result built = build_chunk_mesh(*outcome->data, vertices);
                const glm::dvec2 corner = vertices.position(vertices.x0, vertices.y0);
                const glm::vec3 offset(static_cast<f32>(corner.x), 0.0f, static_cast<f32>(corner.y));
                for (auto& v : built.data.vertices)
                    v.position -= offset;

                const std::string name = std::format("chunk_L{}_{}_{}", level, ix, iy);
                const GLT::render::procedural_mesh_id temporary = store.create({ .update_class = GLT::render::mesh_update_class::immutable,
                    .detail = GLT::render::procedural_detail::none, .debug_name = name });
                std::expected<GLT::asset::asset_ref, GLT::asset::load_error> mesh = std::unexpected(GLT::asset::load_error::not_found);
                if (store.update(temporary, std::move(built.data)))
                    mesh = store.bake(temporary, folder / name, name);
                store.destroy(temporary);
                if (!mesh) {
                    ++failed;
                    LOG(warn, "terrain bake: writing chunk mesh [{}] failed (error {})", (folder / name).generic_string(), static_cast<int>(mesh.error()))
                    continue;
                }

                const GLT::world::entity_id entity = world.spawn();
                world.set_entity_name(entity, name);
                world.set_parent(entity, root);
                world.translate_local(entity, offset);
                world.set_mesh(entity, *mesh);
                ++baked;
            }

        const f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - started).count();
        m_bake_status[&world] = std::format("baked {} chunk(s) into {} in {:.0f} ms{}", baked, folder.generic_string(), ms,
            failed ? std::format(", {} failed (see log)", failed) : std::string());
        LOG(info, "terrain: {}", m_bake_status[&world])
    }

}
