
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

    stack::stack(const size_t max_steps)
        : m_max_steps(std::max<size_t>(1, max_steps)) {}


    void stack::push(step new_step) {

        if (m_replaying) {
            LOG(warn, "undo_system: ignored push of [{}] while undo/redo is running", new_step.name);
            return;
        }

        VALIDATE(new_step.valid(), return, "", "undo_system: step [{}] has no undo or redo callback", (new_step.name))

        if (m_group_depth > 0) {
            m_group_steps.push_back(std::move(new_step));
            return;
        }

        if (m_clean_cursor != UNREACHABLE && m_clean_cursor > m_cursor)
            m_clean_cursor = UNREACHABLE;                       // the saved state lived in the redo branch dropped below
        m_steps.resize(m_cursor);                               // a new operation drops the redo branch
        m_steps.push_back(std::move(new_step));
        m_cursor = m_steps.size();

        if (m_steps.size() > m_max_steps)
            drop_oldest(m_steps.size() - m_max_steps);
    }


    bool stack::undo() {

        if (!can_undo() || m_replaying)
            return false;

        m_replaying = true;
        m_steps[--m_cursor].undo();
        m_replaying = false;
        return true;
    }


    bool stack::redo() {

        if (!can_redo() || m_replaying)
            return false;

        m_replaying = true;
        m_steps[m_cursor++].redo();
        m_replaying = false;
        return true;
    }


    bool stack::can_undo() const noexcept               { return m_cursor > 0 && m_group_depth == 0; }


    bool stack::can_redo() const noexcept               { return m_cursor < m_steps.size() && m_group_depth == 0; }


    std::string_view stack::undo_name() const noexcept  { return can_undo() ? std::string_view(m_steps[m_cursor - 1].name) : std::string_view{}; }


    std::string_view stack::redo_name() const noexcept  { return can_redo() ? std::string_view(m_steps[m_cursor].name) : std::string_view{}; }


    void stack::begin_group(std::string name) {

        if (m_group_depth++ == 0) {
            m_group_name = std::move(name);
            m_group_steps.clear();
        }
    }


    void stack::end_group() {

        VALIDATE(m_group_depth > 0, return, "", "undo_system: end_group() without begin_group()")

        if (--m_group_depth > 0)
            return;

        if (m_group_steps.empty())
            return;

        if (m_group_steps.size() == 1) {
            step single = std::move(m_group_steps.front());
            single.name = std::move(m_group_name);
            m_group_steps.clear();
            push(std::move(single));
            return;
        }

        // Shared so the two callbacks don't each copy every child step
        auto children = std::make_shared<std::vector<step>>(std::move(m_group_steps));
        m_group_steps.clear();

        step group;
        group.name = std::move(m_group_name);
        group.undo = [children]() {
            for (auto it = children->rbegin(); it != children->rend(); ++it)
                it->undo();
        };
        group.redo = [children]() {
            for (auto& child : *children)
                child.redo();
        };
        push(std::move(group));
    }


    void stack::clear() noexcept {

        m_clean_cursor = is_clean() ? 0 : UNREACHABLE;         // forgetting the history does not save the world
        m_steps.clear();
        m_cursor = 0;
        m_group_depth = 0;
        m_group_steps.clear();
    }


    void stack::set_max_steps(const size_t max_steps) {

        m_max_steps = std::max<size_t>(1, max_steps);
        if (m_steps.size() <= m_max_steps)
            return;

        drop_oldest(m_steps.size() - m_max_steps);
    }


    bool stack::is_replaying() const noexcept           { return m_replaying; }


    size_t stack::size() const noexcept                 { return m_steps.size(); }


    void stack::drop_oldest(const size_t count) {

        const size_t drop = std::min(count, m_steps.size());
        m_steps.erase(m_steps.begin(), m_steps.begin() + static_cast<std::ptrdiff_t>(drop));
        m_cursor = m_cursor > drop ? m_cursor - drop : 0;
        if (m_clean_cursor != UNREACHABLE)
            m_clean_cursor = m_clean_cursor >= drop ? m_clean_cursor - drop : UNREACHABLE;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
