
#include "util/pch.h"
#include "event_bus.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::event_bus {

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

    subscription_guard::subscription_guard(bus* b, handle id) noexcept
        : m_bus(b), m_id(id) {}


    subscription_guard::subscription_guard(subscription_guard&& other) noexcept
        : m_bus(other.m_bus), m_id(other.m_id) {

        other.m_bus = nullptr;
        other.m_id = INVALID_HANDLE;
    }


    subscription_guard::~subscription_guard() { reset(); }

    // CLASS PUBLIC ====================================================================================================

    subscription_guard& subscription_guard::operator=(subscription_guard&& other) noexcept {

        if (this != &other) {
            reset();
            m_bus = other.m_bus;
            m_id = other.m_id;
            other.m_bus = nullptr;
            other.m_id = INVALID_HANDLE;
        }
        return *this;
    }


    void subscription_guard::reset() noexcept {

        if (m_bus && m_id != INVALID_HANDLE)
            m_bus->unsubscribe(m_id);
        m_bus = nullptr;
        m_id = INVALID_HANDLE;
    }


    [[nodiscard]] handle subscription_guard::get() const noexcept { return m_id; }


    [[nodiscard]] bool subscription_guard::valid() const noexcept { return m_id != INVALID_HANDLE; }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
