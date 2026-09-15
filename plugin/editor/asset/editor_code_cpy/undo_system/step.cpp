
#include "util/pch.h"
#include "step.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::undo_system {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    bool step::is_compact() const noexcept  { return setter != nullptr; }


    void step::apply()  const               { is_compact() ? setter(object, after)  : (do_fn   ? do_fn()   : void()); }
    

    void step::revert() const               { is_compact() ? setter(object, before) : (undo_fn ? undo_fn() : void()); }


    void step::merge(const step& newer) {

        VALIDATE(is_compact() && newer.is_compact() && size == newer.size, return, "", "Failed to merge undo steps");
        std::memcpy(after, newer.after, size);
        timestamp = newer.timestamp;
    }

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    constexpr undo_id hash_undo_id(const char* s) noexcept {

        undo_id h = 1469598103934665603ull;
        for (; *s; ++s) { 
            
            h ^= (u8)*s; 
            h *= 1099511628211ull; 
        }
        return h;
    }


    constexpr undo_id mix_undo_id(undo_id h, u64 discriminator) noexcept {

        h ^= discriminator + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        return h;
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
