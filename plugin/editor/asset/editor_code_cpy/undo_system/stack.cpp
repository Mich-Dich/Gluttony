
#include "util/pch.h"
#include "stack.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::undo_system {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    void stack::push(step undo_step, f32 now) {

        if (undo_step.can_combine && m_cursor > 0) {                 // Try to merge with the top of the stack.
            step& top = m_steps[m_cursor - 1];
            if (top.is_compact() && undo_step.is_compact() 
                && top.id == undo_step.id && top.can_combine) {

                top.merge(undo_step);
                return;
            }
        }

        m_size = m_cursor;                                      // Drop the redo tail — the user did something new.

        if (m_size == max_steps) {                              // Ring-buffer behaviour: if full, drop the oldest.
            std::memmove(m_steps.data(), m_steps.data() + 1, (max_steps - 1) * sizeof(step));
            m_steps[max_steps - 1] = std::move(undo_step);
        } else
            m_steps[m_size++] = std::move(undo_step);

        m_cursor = m_size;
    }


    bool stack::undo() { 
        
        if (m_cursor == 0)
            return false; 
        
        m_steps[--m_cursor].revert(); 
        return true; 
    }


    bool stack::redo() {

        if (m_cursor == m_size) 
            return false; 
            
        m_steps[m_cursor++].apply();  
        return true; 
    }


    bool stack::can_undo() const noexcept       { return m_cursor > 0; }


    bool stack::can_redo() const noexcept       { return m_cursor < m_size; }


    void stack::clear()        noexcept         { m_size = m_cursor = 0; }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
