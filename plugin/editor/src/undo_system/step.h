
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::undo_system {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // One undoable user operation ("Move Cube", "Delete 3 entities", ...).
    //
    // A step is recorded AFTER the operation already happened, so it only has to know how to go back (undo) and how to
    // do it again (redo). Both callbacks must be repeatable: undo, redo, undo, ... is a valid sequence.
    //
    // Prefer storing *states* (before / after) over storing *deltas*: "set position to X" survives being replayed in any
    // order, "move by +1" does not.
    struct step {

        std::string                             name;           // shown in the Edit menu as "Undo <name>"
        std::function<void()>                   undo;           // restore the state before the operation
        std::function<void()>                   redo;           // restore the state after the operation

        [[nodiscard]] bool valid() const noexcept;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // A step that switches a single value between [before] and [after], for window-local state (settings structs,
    // asset editor fields, ...). [target] must stay valid as long as the stack that holds the step - true for anything
    // owned by the same window as the stack.
    // [on_change] (optional) runs after every undo / redo, e.g. to mark an asset dirty or re-upload something.
    template <typename T>
    [[nodiscard]] step make_value_step(std::string name, T* target, T before, T after, std::function<void()> on_change = {});

    // CLASS DECLARATION ===============================================================================================

}

#include "step.inl"
