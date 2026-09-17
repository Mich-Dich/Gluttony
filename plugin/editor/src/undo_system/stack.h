
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

    class stack {
    public:

        DEFAULT_CONSTRUCTORS(stack);

        static constexpr size_t                 max_steps = 256;

        void push(step undo_step);

        bool undo();
        bool redo();

        bool can_undo() const noexcept;
        bool can_redo() const noexcept;
        void clear() noexcept;

    private:

        std::array<step, max_steps>             m_steps{};
        size_t                                  m_size   = 0;
        size_t                                  m_cursor = 0;

    };
}
