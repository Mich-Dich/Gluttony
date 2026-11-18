#pragma once

#include <procedural/construct/registry.h>
#include <procedural/construct/evaluate.h>

// The plant pack: plants as a stack of stages. Engine-independent (tested by plant_self_test).
//
// Stage types (every random choice comes from the construct seed + the stage's own seed, so "New seed" re-grows all):
//
//   plant.trunk          Trunk            root stage: stems from the ground (height, lean, flare, radius profile, wobble)
//   plant.drawn          Drawn branches   branch splines the user placed (guides with role plant.branch); snapping forks
//   plant.branches       Branches         grows on any branch stage: distribution along the parent (count / density, range,
//                                         spiral / alternate / opposite / whorled / random), angles, length profile, radius
//                                         ratio, gravity, light seeking, wobble. Stacked for twigs, sub-twigs, ...
//   plant.leaves         Leaves           leaf cards along / at the tips of a branch stage
//   plant.leaf_planes    Leaf planes      leaf cards scattered over planes the user placed (guides with role plant.leaves)
//
// Elements: plant.branch (curve, radius per point), plant.leaf_cluster (the cards of one branch / plane).
// Realizers: plant.branch (tube, material "bark"), plant.leaf_cluster (cards, material "leaf"). Outputs: "bark", "leaves".
//
// For the Construct editor the pack also registers its guide roles (Branch spline, Leaf plane), "add guide" templates,
// presets, an upgrade function (constructs from before the stages) and its self test. It has no editor code.



namespace GLT::construct::plant {

    // CONSTANTS =======================================================================================================

    inline constexpr std::string_view                   PACK_NAME = "plant";

    // Stage type names
    inline constexpr std::string_view                   STAGE_TRUNK = "plant.trunk";
    inline constexpr std::string_view                   STAGE_DRAWN = "plant.drawn";
    inline constexpr std::string_view                   STAGE_BRANCHES = "plant.branches";
    inline constexpr std::string_view                   STAGE_LEAVES = "plant.leaves";
    inline constexpr std::string_view                   STAGE_LEAF_PLANES = "plant.leaf_planes";

    // FUNCTION DECLARATION ============================================================================================

    [[nodiscard]] role_id role_branch();
    [[nodiscard]] role_id role_leaves();
    [[nodiscard]] kind_id kind_branch();
    [[nodiscard]] kind_id kind_leaf_cluster();

    // Attribute ids of the plant schema
    namespace tags {
        [[nodiscard]] attribute_id level();             // 0: trunk / drawn branch, 1: branch on it, ...
        [[nodiscard]] attribute_id sides();             // tube sides at 5 cm radius (thinner tubes use fewer)
        [[nodiscard]] attribute_id caps();              // close the tube's tip
        [[nodiscard]] attribute_id two_sided();         // leaf cards: back faces too
        // Per guide overrides (the stage's settings apply when a guide does not set them)
        [[nodiscard]] attribute_id leaf_density();
        [[nodiscard]] attribute_id leaf_size();
        [[nodiscard]] attribute_id taper();             // drawn branches without a radius per point
    }

    void register_pack(i_registry& registry, std::string_view owner);

    // The presets the pack registers (Broadleaf tree, Conifer, Bush, Hand drawn)
    [[nodiscard]] std::vector<preset_desc> presets();

    // The broadleaf tree preset: trunk, branches, twigs, leaves (no guides: everything follows the seed)
    [[nodiscard]] construct_data make_default_tree();

    // A stage of [type] with the pack's suggested settings for [level] (branches: 1 on the trunk, 2 twigs, ...)
    [[nodiscard]] generator_instance make_stage(std::string_view type, u32 id, std::vector<u32> inputs, u32 level = 1);

    // Old plant constructs (guides only, no stages): adds Drawn branches, Twigs, Leaves and Leaf planes stages.
    // Returns true when it changed [data]
    bool upgrade(construct_data& data);

}
