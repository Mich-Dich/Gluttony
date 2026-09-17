
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

    void stack::push(step undo_step) {

        if (undo_step.can_combine && m_cursor > 0) {
            step& top = m_steps[m_cursor - 1];
            if (top.is_compact() && undo_step.is_compact()
                && top.id == undo_step.id && top.can_combine) {

                top.merge(undo_step);
                return;
            }
        }

        m_size = m_cursor;
        if (m_size == max_steps) {
            std::move(m_steps.begin() + 1, m_steps.end(), m_steps.begin());
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
