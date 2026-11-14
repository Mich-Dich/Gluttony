
#pragma once

#include "world/entity.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics {
    struct contact_event;
}

namespace GLT::script {
    class script_context;
}

namespace GLT::script {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    using context_flags = u32;


    namespace context_flag {
        inline constexpr context_flags none                     = 0u;
        inline constexpr context_flags was_reloaded             = BIT(0);
        inline constexpr context_flags from_save                = BIT(1);
        inline constexpr context_flags woke_from_sleep          = BIT(2);
        inline constexpr context_flags is_start                 = BIT(3);
        inline constexpr context_flags is_update                = BIT(4);
        inline constexpr context_flags is_contact               = BIT(5);
        inline constexpr context_flags is_stop                  = BIT(6);
        inline constexpr context_flags plugin_reserved_base     = BIT(16);
    }

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    inline constexpr bool has_flag(context_flags set, context_flags f) noexcept { return (set & f) != 0u; }


    inline constexpr bool was_restored(context_flags set) noexcept { return has_flag(set, context_flag::was_reloaded | context_flag::from_save); }

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class i_script {
    public:

        virtual ~i_script() = default;


        virtual void on_start(script_context& /*ctx*/) {}


        virtual void on_update(script_context& /*ctx*/, f32 /*dt*/) {}


        // Declared now, wired up in Phase 4. Kept on the interface so user
        // scripts can implement it without a recompile once Phase 4 lands.
        virtual void on_contact(script_context& /*ctx*/, GLT::world::entity_id /*other*/, const GLT::physics::contact_event& /*contact*/) {}


        virtual void on_stop(script_context& /*ctx*/) {}

    };

}
