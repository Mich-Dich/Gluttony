#pragma once


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

    template <typename T>
    FORCE_INLINE_R step make_value_step(undo_id id, void* obj, step::setter_fn setter, const T& before, const T& after, 
        bool can_combine, f32 now) {

        static_assert(std::is_trivially_copyable_v<T> && sizeof(T) <= 16, "Use the callback path for non-POD or > 16 byte values");
        step s;
        s.id = id; s.object = obj; s.setter = setter;
        s.size = (u8)sizeof(T);
        std::memcpy(s.before, &before, sizeof(T));
        std::memcpy(s.after,  &after,  sizeof(T));
        s.can_combine = can_combine;
        s.timestamp = now;
        return s;
    }

    template <typename Do, typename Undo>
    FORCE_INLINE_R step make_callback_step(undo_id id, Do&& d, Undo&& u, f32 now) {
        
        step s;
        s.id = id;
        s.do_fn   = std::forward<Do>(d);
        s.undo_fn = std::forward<Undo>(u);
        s.can_combine = false;   // callbacks never merge
        s.timestamp = now;
        return s;
    }

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
