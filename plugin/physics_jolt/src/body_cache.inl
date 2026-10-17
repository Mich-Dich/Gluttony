#pragma once


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics::jolt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    FORCE_INLINE_R u64 pack_body_handle(body_handle h) noexcept {

        return (static_cast<u64>(h.index) << 32) | static_cast<u64>(h.generation);
    }


    FORCE_INLINE_R body_handle unpack_body_handle(u64 v) noexcept {

        return body_handle{ static_cast<u32>(v >> 32), static_cast<u32>(v) };
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    FORCE_INLINE_R body_handle body_cache::insert(body_entry entry) { return m_table.insert(std::move(entry)); }


    FORCE_INLINE void body_cache::remove(body_handle h) noexcept { m_table.remove(h); }


    FORCE_INLINE_R body_entry* body_cache::get(body_handle h) noexcept { return m_table.get(h); }


    FORCE_INLINE_R const body_entry* body_cache::get(body_handle h) const noexcept { return m_table.get(h); }


    FORCE_INLINE void body_cache::clear() noexcept { m_table = handle_table<body_handle, body_entry>{}; }


    template<typename F>
    void body_cache::for_each(F&& f) const { m_table.for_each(std::forward<F>(f)); }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
