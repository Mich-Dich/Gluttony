
#pragma once

#include <asset/asset_ref.h>
#include <asset/type.h>
#include <world/entity.h>



namespace GLT::world {
    class i_world;
}

namespace GLT::terrain {

    enum class terrain_mode : u8 {
        live = 0,                                       // streamed around the streaming anchor, never saved
        baked = 1,                                      // chunk meshes baked into assets, ordinary saved entities
    };


    // @brief World component (on the world entity): which graph makes this world's terrain and how it gets into the world
    //
    // Saved with the world (stable, versioned byte layout, see terrain_component.cpp). The graph is stored as its content
    // path. Values are read every frame by the terrain world system; any change makes the streamer re-evaluate
    struct terrain_component {

        GLT::asset::asset_ref                           graph{};                // .glt_terrain_graph
        u64                                             seed = 1;
        std::vector<std::pair<std::string, f32>>        overrides{};            // exposed parameters that differ from the graph's defaults
        terrain_mode                                    mode = terrain_mode::live;
        bool                                            enabled = true;

        // chunks (live and bake)
        f32                                             chunk_size = 128.0f;    // world units of the finest chunks
        u32                                             chunk_quads = 32;       // quads per chunk side (every level)
        u32                                             lod_levels = 6;         // unused since 0.3 (levels follow the view distance, see detail_levels()); kept in files
        f32                                             view_distance = 30000.0f;   // to the horizon: far chunks are huge and coarse, so this is cheap
        f32                                             detail = 3.0f;          // a chunk splits while the anchor is closer than detail x its size

        // bake
        glm::vec2                                       bake_min{ -1024.0f };   // world x / z
        glm::vec2                                       bake_max{ 1024.0f };
        u32                                             bake_level = 0;         // chunk level baked (0 = finest)
        std::string                                     bake_folder{ "terrain" };   // content folder of the baked meshes

        // runtime: "Regenerate" bumps it (not saved)
        u64                                             regenerate = 0;
    };

    inline constexpr std::string_view                   TERRAIN_COMPONENT_NAME = "terrain.world_terrain";

    // What the inspector offers besides the values (implemented by the terrain world system)
    struct terrain_component_actions {
        std::function<void(GLT::world::i_world&)>       bake;
        std::function<void(GLT::world::i_world&)>       clear_bake;
        std::function<std::string(GLT::world::i_world&)> status;                // one line, shown in the inspector
    };

    // Plugin on_load / on_unload. [graph_type]: asset type of terrain graphs (the picker's filter)
    void register_terrain_component(GLT::asset::type graph_type, terrain_component_actions actions);
    void unregister_terrain_component();

    // The terrain component of [world] (on its world entity), null if it has none
    [[nodiscard]] terrain_component* terrain_of(GLT::world::i_world& world);

    // ... adding it first if the world has none (null if the world has no world entity / the type is not registered)
    [[nodiscard]] terrain_component* ensure_terrain(GLT::world::i_world& world);

    // Detail levels the view distance needs: the coarsest chunks are big enough that a handful cover the view distance
    // (each level doubles chunk size and sample spacing, so a far horizon costs a few levels, not many chunks)
    [[nodiscard]] u32 detail_levels(const terrain_component& component);

    // Everything that changes the terrain's shape (graph identity excluded: the caller adds the graph revision)
    [[nodiscard]] u64 shape_hash(const terrain_component& component);

    // Exposed parameter overrides as the evaluator takes them
    [[nodiscard]] std::unordered_map<std::string, f32> override_map(const terrain_component& component);

}
