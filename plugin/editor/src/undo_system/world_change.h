
#pragma once

#include <world/entity.h>

#include "undo_system/stack.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {
    class i_world;
}

namespace GLT::undo_system {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // What a set of entities looked like at one moment. "Did not exist" is a valid state too, that's how spawn and
    // delete become undoable: undo of a spawn restores a state in which the entity is not alive.
    struct world_state {

        std::vector<world::entity_id>           ids{};          // every entity this state talks about
        std::vector<world::entity_id>           alive{};        // the subset that existed
        std::vector<std::byte>                  blob{};         // i_world::snapshot_entities(alive)

        [[nodiscard]] bool operator==(const world_state& other) const noexcept { return alive == other.alive && blob == other.blob; }
    };


    // Called after a world step was undone / redone with the state that is now active (used for re-selecting)
    using on_applied_fn = std::function<void(const world_state&)>;

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    [[nodiscard]] world_state capture(const world::i_world& world, std::span<const world::entity_id> ids);


    // Makes the world match [state]: entities that didn't exist are despawned, the others are restored
    void apply(world::i_world& world, const world_state& state);


    [[nodiscard]] step make_world_step(std::string name, world::i_world& world, world_state before, world_state after,
        on_applied_fn on_applied = {});

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Records a one-shot world operation (spawn, delete, add component, toggle visibility, ...):
    //
    //      undo_system::world_change change(world, ids_that_will_change);    // snapshot "before"
    //      ... modify the world ...
    //      change.created(new_id);                                             // for entities that didn't exist before
    //      change.commit(stack, "Delete Cube");                                // snapshot "after", push the step
    //
    // commit() does nothing if the world didn't actually change.
    class world_change {
    public:

        world_change(world::i_world& world, std::span<const world::entity_id> ids);


        void created(world::entity_id id);


        bool commit(stack& history, std::string name, on_applied_fn on_applied = {});

    private:

        world::i_world&                         m_world;
        world_state                             m_before{};
    };


    // Records continuous editing (property panels, gizmos) as ONE step per interaction, without knowing which widget
    // changed what. Every frame:
    //
    //      tracker.begin(world, selection);                    // snapshot before drawing
    //      ... draw widgets that may modify the entities ...
    //      tracker.end("Move", still_dragging, user_input);    // snapshot after drawing
    //
    // A change opens an edit; it stays open while [interacting] is true (slider held, gizmo dragged, text field focused)
    // and becomes a single undo step as soon as the interaction ends. Changes on frames without user input are not the
    // user's doing (e.g. a component normalising itself while drawn) and are not recorded.
    class world_edit_tracker {
    public:

        explicit world_edit_tracker(stack& history) : m_history(history) {}


        void begin(world::i_world* world, std::span<const world::entity_id> ids);


        void end(std::string_view name, const bool interacting, const bool user_input);


        // Overrides the step name of the edit that is open / opens this frame ("Add Mesh" instead of "Edit Properties")
        void set_name(std::string name);


        // Closes an open edit right now, e.g. before a structural operation or an undo
        void commit();


        // Drops the open edit without recording it (world changed, history cleared)
        void reset();


        on_applied_fn                           on_applied{};

    private:

        stack&                                  m_history;
        world::i_world*                         m_world = nullptr;
        std::vector<world::entity_id>           m_ids{};
        world_state                             m_frame_before{};               // state at begin() of this frame
        world_state                             m_before{};                     // state when the open edit started
        world_state                             m_latest{};                     // state at the last end()
        std::string                             m_name{};
        std::string                             m_name_override{};
        bool                                    m_open = false;
    };

}
