
#pragma once

#include "undo_system/step.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::undo_system {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Linear undo history of one editor window (world viewport, an asset editor, ...).
    //
    //      [ step 0 ][ step 1 ][ step 2 ][ step 3 ]
    //                                  ^ cursor        undo() reverts step 2, redo() re-applies it
    //
    // push() drops everything right of the cursor (the redo branch) and appends. When the history is full, the oldest
    // step is dropped.
    //
    // Several pushes can be merged into ONE step with a group:
    //
    //      {
    //          undo_system::scoped_group group(stack, "Align 5 entities");
    //          stack.push(...);  stack.push(...);  ...
    //      }   // -> a single "Align 5 entities" entry; undo reverts all of them (in reverse order)
    class stack {
    public:

        explicit stack(const size_t max_steps = 256);


        // Records a step whose operation has ALREADY been applied. Ignored while undo/redo is running (so callbacks that
        // happen to call editor code can't corrupt the history)
        void push(step new_step);


        bool undo();
        bool redo();

        [[nodiscard]] bool can_undo() const noexcept;
        [[nodiscard]] bool can_redo() const noexcept;

        // Name of the step undo() / redo() would run, empty if none. For menus: "Undo " + undo_name()
        [[nodiscard]] std::string_view undo_name() const noexcept;
        [[nodiscard]] std::string_view redo_name() const noexcept;

        // Grouping (nestable, only the outermost name is used)
        void begin_group(std::string name);
        void end_group();

        void clear() noexcept;


        // Changes the history length; drops the oldest steps if the history is longer
        void set_max_steps(const size_t max_steps);

        [[nodiscard]] bool is_replaying() const noexcept;
        [[nodiscard]] size_t size() const noexcept;

        // Unsaved-changes tracking: mark_clean() after a save (or a fresh load); is_clean() is true while the applied steps
        // are exactly those of that moment. Undoing back to the saved state counts as clean again; a new step after an undo
        // past the saved state, or the saved state falling off the history, makes it unreachable (dirty until next save)
        void mark_clean() noexcept { m_clean_cursor = m_cursor; }
        [[nodiscard]] bool is_clean() const noexcept { return m_clean_cursor == m_cursor; }

    private:

        static constexpr size_t                 UNREACHABLE = static_cast<size_t>(-1);
        void drop_oldest(const size_t count);                                   // keeps m_cursor / m_clean_cursor consistent

        std::vector<step>                       m_steps{};
        size_t                                  m_cursor = 0;                   // number of steps that are currently applied
        size_t                                  m_clean_cursor = 0;             // m_cursor at the last mark_clean(), UNREACHABLE if lost
        size_t                                  m_max_steps = 256;
        bool                                    m_replaying = false;

        u32                                     m_group_depth = 0;
        std::string                             m_group_name{};
        std::vector<step>                       m_group_steps{};
    };


    // RAII helper for stack::begin_group / end_group
    class scoped_group {
    public:

        scoped_group(stack& target, std::string name) : m_stack(target) { m_stack.begin_group(std::move(name)); }
        ~scoped_group()                                                 { m_stack.end_group(); }

        DELETE_COPY_CONSTRUCTOR(scoped_group)

    private:

        stack&                                  m_stack;
    };

}
