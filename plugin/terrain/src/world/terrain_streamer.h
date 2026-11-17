
#pragma once

#include <render/procedural_mesh.h>
#include <world/entity.h>

#include "graph/evaluator.h"
#include "terrain_component.h"



namespace GLT::world {
    class i_world;
}

namespace GLT::terrain {

    // @brief Live terrain of one world: chunks around the streaming anchor, generated from the world's terrain component
    //
    // Chunk selection is a quadtree over the ground plane: the coarsest level covers the view distance; a chunk splits
    // into four while the anchor is closer than [detail] x its size, down to level 0 ([chunk_size]). Every chunk has the
    // same number of quads, so detail falls off with distance. A chunk sits on the sample lattice of its level (spacing
    // = chunk_size / chunk_quads x 2^level): neighbours of one level meet exactly, skirts hide the cracks between levels.
    //
    // Chunks are evaluated on the thread pool (missing ones first, nearest first, a few at a time), meshed into
    // procedural meshes and drawn by transient entities (not saved, in no region, not in the outliner). Chunks that left
    // the selection stay until the chunks covering their area have meshes, so moving never opens holes. Graph / seed /
    // parameter changes re-evaluate every chunk; a chunk keeps its old mesh until the new one is there
    class terrain_streamer {
    public:

        struct stats {
            u32                                         chunks = 0;             // selected
            u32                                         ready = 0;              // of those: showing the current state
            u32                                         running = 0;            // evaluations in flight
            u32                                         meshes = 0;             // chunks with a mesh (incl. ones leaving)
            std::string                                 error{};
        };

        terrain_streamer(evaluator& eval, GLT::world::i_world& world);
        ~terrain_streamer();

        terrain_streamer(const terrain_streamer&) = delete;
        terrain_streamer& operator=(const terrain_streamer&) = delete;

        // One frame. [graph] / [graph_revision]: the terrain graph as it is now
        void update(const terrain_component& settings, std::shared_ptr<const terrain_graph> graph, u64 graph_revision);

        // Drops every chunk. [despawn]: false when the world is already gone (only the meshes are released)
        void clear(bool despawn);

        [[nodiscard]] const stats& last_stats() const noexcept { return m_stats; }

    private:

        struct chunk {
            u32                                         level = 0;
            i64                                         ix = 0;
            i64                                         iy = 0;
            f64                                         spacing = 1.0;          // sample spacing of this chunk
            bool                                        desired = false;

            GLT::render::procedural_mesh_id             mesh = GLT::render::INVALID_PROCEDURAL_MESH;
            GLT::world::entity_id                       entity{};
            u64                                         shown_key = 0;
            u64                                         pending_key = 0;
            u64                                         failed_key = 0;
            std::future<eval_outcome>                   job{};
            std::shared_ptr<std::atomic<bool>>          cancel{};

            [[nodiscard]] f64 size(u32 quads) const noexcept { return spacing * quads; }
        };

        void select(const terrain_component& settings, glm::dvec2 anchor, std::vector<chunk*>& out);
        void collect(chunk& c, u32 quads, f32 skirt);
        void release(chunk& c, bool despawn);

        evaluator&                                      m_eval;
        GLT::world::i_world&                            m_world;
        std::unordered_map<u64, chunk>                  m_chunks{};             // by (geometry, level, ix, iy)
        stats                                           m_stats{};
    };

}
