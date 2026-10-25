
#pragma once

#include "platform/type.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::game {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Stable, portable session handle. Same index/generation scheme as entity_id —
    // a stale id is detectable because the slot's generation was bumped on reuse.
    struct session_id {
        u32                                             index = 0;
        u32                                             generation = 0;

        constexpr bool is_valid() const noexcept { return generation != 0; }
        constexpr bool operator==(const session_id&) const noexcept = default;
        constexpr auto operator<=>(const session_id&) const noexcept = default;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
