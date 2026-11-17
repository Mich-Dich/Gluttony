
#include "util/pch.h"
#include "terrain_streamer.h"

#include <world/i_world.h>

#include "chunk_mesh.h"



namespace GLT::terrain {

    namespace {

        constexpr u32                                   MAX_JOBS = 4;           // chunk evaluations in flight per world
        constexpr size_t                                MAX_CHUNKS = 3000;      // selection cap (absurd settings)
        constexpr f32                                   UV_SCALE = 1.0f / 32.0f;
        constexpr const char*                           SURFACE_TYPE = "terrain.surface";

        [[nodiscard]] u64 chunk_key(u64 geometry, u32 level, i64 ix, i64 iy) {
            return hash_combine(hash_combine(hash_combine(geometry, level), static_cast<u64>(ix)), static_cast<u64>(iy));
        }

        struct rect {
            f64 x0, y0, x1, y1;
            [[nodiscard]] bool overlaps(const rect& o) const noexcept { return x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1; }
            [[nodiscard]] f64 distance(glm::dvec2 p) const noexcept {
                const f64 dx = std::max({ x0 - p.x, 0.0, p.x - x1 });
                const f64 dy = std::max({ y0 - p.y, 0.0, p.y - y1 });
                return std::sqrt(dx * dx + dy * dy);
            }
        };

    }

    // CLASS IMPLEMENTATION ============================================================================================

    terrain_streamer::terrain_streamer(evaluator& eval, GLT::world::i_world& world) : m_eval(eval), m_world(world) {}


    terrain_streamer::~terrain_streamer() { clear(false); }


    void terrain_streamer::clear(bool despawn) {

        for (auto& [key, c] : m_chunks)
            release(c, despawn);
        m_chunks.clear();
        m_stats = {};
    }

    // CLASS PRIVATE ===================================================================================================

    void terrain_streamer::release(chunk& c, bool despawn) {

        if (c.cancel)
            c.cancel->store(true);                      // the job references only the evaluator: cancel, don't wait
        if (despawn && c.entity.is_valid() && m_world.alive(c.entity))
            m_world.despawn(c.entity);
        c.entity = {};
        if (c.mesh != GLT::render::INVALID_PROCEDURAL_MESH) {
            GLT::render::procedural_meshes().destroy(c.mesh);
            c.mesh = GLT::render::INVALID_PROCEDURAL_MESH;
        }
    }


    void terrain_streamer::select(const terrain_component& s, glm::dvec2 anchor, std::vector<chunk*>& out) {

        const u32 quads = s.chunk_quads;
        const u32 top = s.lod_levels - 1;
        const f64 base = static_cast<f64>(s.chunk_size);
        const f64 base_spacing = base / static_cast<f64>(quads);
        const f64 view = static_cast<f64>(s.view_distance);
        const u64 geometry = hash_combine(std::bit_cast<u32>(s.chunk_size), quads);

        auto visit = [&](auto&& self, u32 level, i64 ix, i64 iy) -> void {
            if (out.size() >= MAX_CHUNKS)
                return;
            const f64 size = std::ldexp(base, static_cast<i32>(level));
            const rect area{ static_cast<f64>(ix) * size, static_cast<f64>(iy) * size, static_cast<f64>(ix + 1) * size, static_cast<f64>(iy + 1) * size };
            const f64 d = area.distance(anchor);
            if (d > view)
                return;
            if (level > 0 && d < static_cast<f64>(s.detail) * size) {
                for (i64 cy = 0; cy < 2; ++cy)
                    for (i64 cx = 0; cx < 2; ++cx)
                        self(self, level - 1, ix * 2 + cx, iy * 2 + cy);
                return;
            }

            auto [it, inserted] = m_chunks.try_emplace(chunk_key(geometry, level, ix, iy));
            chunk& c = it->second;
            if (inserted) {
                c.level = level;
                c.ix = ix;
                c.iy = iy;
                c.spacing = std::ldexp(base_spacing, static_cast<i32>(level));
            }
            c.desired = true;
            out.push_back(&c);
        };

        const f64 top_size = std::ldexp(base, static_cast<i32>(top));
        const i64 x0 = static_cast<i64>(std::floor((anchor.x - view) / top_size));
        const i64 x1 = static_cast<i64>(std::floor((anchor.x + view) / top_size));
        const i64 y0 = static_cast<i64>(std::floor((anchor.y - view) / top_size));
        const i64 y1 = static_cast<i64>(std::floor((anchor.y + view) / top_size));
        for (i64 y = y0; y <= y1; ++y)
            for (i64 x = x0; x <= x1; ++x)
                visit(visit, top, x, y);
    }


    void terrain_streamer::collect(chunk& c, u32 quads, f32 skirt) {

        if (!c.job.valid() || c.job.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;

        eval_outcome outcome = c.job.get();
        c.cancel.reset();
        if (!outcome) {
            if (outcome.error() != "cancelled") {
                c.failed_key = c.pending_key;
                m_stats.error = outcome.error();
            }
            c.pending_key = 0;
            return;
        }

        if (outcome->data) {
            region vertices{};
            vertices.x0 = c.ix * static_cast<i64>(quads);
            vertices.y0 = c.iy * static_cast<i64>(quads);
            vertices.width = quads + 1;
            vertices.height = quads + 1;
            vertices.spacing = c.spacing;
            chunk_mesh_result built = build_chunk_mesh(*outcome->data, vertices,
                { .skirt_depth = skirt * static_cast<f32>(c.spacing), .uv_scale = UV_SCALE });

            auto& store = GLT::render::procedural_meshes();
            if (c.mesh == GLT::render::INVALID_PROCEDURAL_MESH)
                c.mesh = store.create({ .update_class = GLT::render::mesh_update_class::occasional,
                    .detail = GLT::render::procedural_detail::none, .debug_name = "terrain chunk" });
            if (auto updated = store.update(c.mesh, std::move(built.data)); !updated)
                LOG(warn, "terrain: chunk mesh update failed: {}", updated.error().message)

            if (!c.entity.is_valid() || !m_world.alive(c.entity)) {
                c.entity = m_world.spawn_transient();
                m_world.set_procedural_mesh(c.entity, c.mesh);
            }
        }
        c.shown_key = c.pending_key;
        c.pending_key = 0;
    }

    // CLASS PUBLIC ====================================================================================================

    void terrain_streamer::update(const terrain_component& s, std::shared_ptr<const terrain_graph> graph, u64 graph_revision) {

        m_stats.chunks = 0;
        m_stats.ready = 0;

        // What is generated: the graph's Surface output
        u32 surface = INVALID_NODE_ID;
        if (graph)
            for (const auto& node : graph->nodes)
                if (node.type == SURFACE_TYPE) {
                    surface = node.id;
                    break;
                }
        if (surface == INVALID_NODE_ID) {
            m_stats.error = graph ? "the terrain graph has no Surface output" : "no terrain graph";
            clear(true);
            return;
        }

        const u32 quads = s.chunk_quads;
        const f32 skirt = 4.0f;                         // skirt depth in samples of the chunk's level
        const u64 state = hash_combine(hash_combine(graph_revision, surface), shape_hash(s));
        const glm::vec3 anchor3 = m_world.get_streaming_anchor();
        const glm::dvec2 anchor(anchor3.x, anchor3.z);

        // ---- the selection ---------------------------------------------------------------------------------------------
        for (auto& [key, c] : m_chunks)
            c.desired = false;
        std::vector<chunk*> window;
        select(s, anchor, window);

        // Holes first, then nearest
        auto area_of = [quads](const chunk& c) {
            const f64 size = c.size(quads);
            return rect{ static_cast<f64>(c.ix) * size, static_cast<f64>(c.iy) * size, static_cast<f64>(c.ix + 1) * size, static_cast<f64>(c.iy + 1) * size };
        };
        std::sort(window.begin(), window.end(), [&](const chunk* a, const chunk* b) {
            const bool a_hole = a->mesh == GLT::render::INVALID_PROCEDURAL_MESH;
            const bool b_hole = b->mesh == GLT::render::INVALID_PROCEDURAL_MESH;
            if (a_hole != b_hole)
                return a_hole;
            return area_of(*a).distance(anchor) < area_of(*b).distance(anchor);
        });

        // ---- finished / obsolete jobs ------------------------------------------------------------------------------------
        u32 running = 0;
        for (auto& [key, c] : m_chunks) {
            collect(c, quads, skirt);
            if (c.job.valid() && !c.desired && c.cancel)
                c.cancel->store(true);
            running += c.job.valid() ? 1u : 0u;
        }

        // ---- new jobs ------------------------------------------------------------------------------------------------------
        const std::unordered_map<std::string, f32> parameters = override_map(s);
        for (chunk* c : window) {
            const u64 key = std::max<u64>(hash_combine(state, chunk_key(0, c->level, c->ix, c->iy)), 1);
            if (key == c->shown_key) {
                ++m_stats.ready;
                continue;
            }
            if (c->job.valid() || key == c->failed_key || running >= MAX_JOBS)
                continue;

            eval_request request{};
            request.graph = graph;
            request.node = surface;
            request.pin = "height";
            request.seed = s.seed;
            request.parameters = parameters;
            request.area.x0 = c->ix * static_cast<i64>(quads) - 1;     // one more sample around: normals at the edges
            request.area.y0 = c->iy * static_cast<i64>(quads) - 1;
            request.area.width = quads + 3;
            request.area.height = quads + 3;
            request.area.spacing = c->spacing;

            c->cancel = std::make_shared<std::atomic<bool>>(false);
            c->job = m_eval.evaluate_async(std::move(request), c->cancel);
            c->pending_key = key;
            ++running;
        }

        // ---- chunks that left the selection: gone once the selection covers their area -------------------------------
        for (auto it = m_chunks.begin(); it != m_chunks.end(); ) {
            chunk& old = it->second;
            if (old.desired) {
                ++it;
                continue;
            }
            bool still_needed = false;
            if (old.mesh != GLT::render::INVALID_PROCEDURAL_MESH) {
                const rect area = area_of(old);
                for (const chunk* c : window)
                    if (c->mesh == GLT::render::INVALID_PROCEDURAL_MESH && area.overlaps(area_of(*c))) {
                        still_needed = true;
                        break;
                    }
            }
            if (still_needed) {
                ++it;
                continue;
            }
            release(old, true);
            it = m_chunks.erase(it);
        }

        m_stats.chunks = static_cast<u32>(window.size());
        m_stats.running = running;
        m_stats.meshes = 0;
        for (const auto& [key, c] : m_chunks)
            m_stats.meshes += c.mesh != GLT::render::INVALID_PROCEDURAL_MESH ? 1u : 0u;
        if (m_stats.ready == m_stats.chunks && running == 0)
            m_stats.error.clear();
    }

}
