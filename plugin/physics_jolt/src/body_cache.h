
#pragma once

#include "util/handle_table.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics::jolt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Per-body bookkeeping. Everything the plugin needs on the engine side of the Jolt BodyID. BodyID itself lives in a compact
    // side table so the contract's POD boundary types are untouched
    struct body_entry {
        shape_handle                                shape{ INVALID_SHAPE };
        world::entity_id                            entity{ world::INVALID_ENTITY };
        u64                                         user_tag{ 0 };
        JPH::BodyID                                 body_id{};
        f32                                         resolved_mass{ 0.f };
        contact_event_mask                          contact_mask{ contact_event_mask::none };
        u8                                          _pad[7]{};
    };
    static_assert(sizeof(body_entry) == 40, "body_entry layout drifted");

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // body_handle packed into Jolt's mUserData u64. Packed once at create_body time, unpacked inside query and contact callbacks,
    // which only have a BodyID to work with. Index 0 is reserved-invalid, so a live handle's packed representation is never 0 
    // (generation wraps to 1, never 0)
    FORCE_INLINE_R u64 pack_body_handle(body_handle h) noexcept;


    FORCE_INLINE_R body_handle unpack_body_handle(u64 v) noexcept;

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class body_cache {
    public:

        FORCE_INLINE_R body_handle insert(body_entry entry);


        FORCE_INLINE void remove(body_handle h) noexcept;


        FORCE_INLINE_R body_entry* get(body_handle h) noexcept;


        FORCE_INLINE_R const body_entry* get(body_handle h) const noexcept;


        FORCE_INLINE_R u32 size() const noexcept { return m_table.live_count(); }


        FORCE_INLINE void clear() noexcept;


        template<typename F>
        void for_each(F&& f) const;

    private:

        handle_table<body_handle, body_entry> m_table;
    };

}

#include "body_cache.inl"
