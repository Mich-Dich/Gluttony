
#pragma once

#include "util/pch.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // @brief Free-function signature a subscriber can use instead of a std::function
    // @tparam T The event type the handler will receive
    template<typename T>
    using event_function = bool(*)(T&);

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Base class for every event that flows through the event bus
    //
    // Subclasses carry their own payload and provide a debug-friendly [to_string()]. The [handled] flag lets a subscriber
    // signal that the event has been consumed - the bus stops dispatching once it's set
    class event {
    public:

        virtual ~event() = default;

        DEFAULT_GETTER_SETTER_C(bool,       handled)

        // @brief Human-readable description of this event, used in logs and tooling
        // @return A formatted string describing the event
        [[nodiscard]] virtual std::string to_string() const = 0;

    private:

        bool                                m_handled = false;
    };

}
