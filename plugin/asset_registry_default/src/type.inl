#pragma once


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::registry_default {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // Handle packing: low 32 bits = slot index, high 32 = generation.
    // Bumping generation on free makes stale handles detectable.
    FORCE_INLINE_R constexpr u64 make_handle(u32 idx, u32 gen) noexcept { return (static_cast<u64>(gen) << 32) | static_cast<u64>(idx); }

    FORCE_INLINE_R constexpr u32 handle_index(u64 h) noexcept { return static_cast<u32>(h); }

    FORCE_INLINE_R constexpr u32 handle_generation(u64 h) noexcept { return static_cast<u32>(h >> 32); }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
