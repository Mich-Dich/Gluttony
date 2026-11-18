
#pragma once

#include <world/entity.h>

#include "graph/evaluator.h"
#include "terrain_streamer.h"



namespace GLT::world {
    class i_world;
}

namespace GLT::terrain {

    // @brief The terrain of every live world (editor world, play sessions)
    //
    // Once per frame (update event) it looks at every game session's world: a world whose terrain component is enabled,
    // live and has a graph gets a terrain_streamer that follows the world's streaming anchor; others have none. Bake /
    // clear bake (inspector buttons) work on one world: bake evaluates the bake extent at the bake level, writes one
    // static mesh asset per chunk and spawns ordinary (saved) entities for them under a "Terrain (baked)" entity
    class terrain_world_system {
    public:

        explicit terrain_world_system(evaluator& eval);
        ~terrain_world_system();

        terrain_world_system(const terrain_world_system&) = delete;
        terrain_world_system& operator=(const terrain_world_system&) = delete;

        void update();

        // Plugin unload: every generated chunk entity is removed from the worlds that are still alive
        void shutdown();

        void bake(GLT::world::i_world& world);
        void clear_bake(GLT::world::i_world& world);
        [[nodiscard]] std::string status(GLT::world::i_world& world) const;

    private:

        struct world_entry {
            std::unique_ptr<terrain_streamer>           streamer{};
            u64                                         instance_id = 0;        // identity check: a new world at the same address
        };

        [[nodiscard]] std::vector<GLT::world::i_world*> live_worlds() const;

        evaluator&                                      m_eval;
        std::unordered_map<GLT::world::i_world*, world_entry> m_worlds{};
        std::unordered_map<GLT::world::i_world*, std::string> m_bake_status{};
    };

}
