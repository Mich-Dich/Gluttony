
#pragma once

#include "body_cache.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics::jolt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Buffers contact events during step() and hands them out via drain(). Buffered, not callback-based, per §Contact
    // events. Masks are read live from body_cache, so enable_contact_events() takes effect immediately.
    class contact_listener final : public JPH::ContactListener {
    public:

        static constexpr size_t                         kEventBufferCapacity = 8192;

        explicit contact_listener(const body_cache* bodies) noexcept;


        // Per-step housekeeping. Bumps the step counter so persist events dedupe correctly when a body pair has more
        // than one active manifold
        void begin_step() noexcept;


        // JPH::ContactListener ---------------------------------------------------------------------------------------

        void OnContactAdded (const JPH::Body& b1, const JPH::Body& b2, const JPH::ContactManifold& m, JPH::ContactSettings& s) override;


        void OnContactPersisted(const JPH::Body& b1, const JPH::Body& b2, const JPH::ContactManifold& m, JPH::ContactSettings& s) override;


        void OnContactRemoved(const JPH::SubShapeIDPair& pair) override;

        // Public interface -------------------------------------------------------------------------------------------

        // Called by plugin::destroy_body() before the body is removed. Emits phase_end for every active pair involving
        // h, subject to the opt-in mask, so callers tracking per-pair state do not leak
        void destroy_body(body_handle h);


        // Returns the buffered events and clears the buffer. Span is valid until the next call to step() or drain()
        [[nodiscard]] std::span<const contact_event> drain() noexcept;

        // Diagnostics -------------------------------------------------------------------------------------------------

        [[nodiscard]] u32 dropped_count() const noexcept { return m_dropped; }


        [[nodiscard]] u32 active_pair_count() const noexcept { return static_cast<u32>(m_pairs.size()); }


        // Wipes all state. Called from plugin::shutdown() before body_cache::clear()
        void clear() noexcept;

    private:

        struct pair_state {
            body_handle     a{ INVALID_BODY };
            body_handle     b{ INVALID_BODY };
            u32             manifold_count{ 0 };
            u32             last_persist_step{ 0 };
        };

        static u64                 pair_key(JPH::BodyID a, JPH::BodyID b) noexcept;
        contact_event_mask         mask_of(body_handle h) const noexcept;
        bool                       include_phase(body_handle a, body_handle b, contact_event_mask phase) const noexcept;
        void                       emit(const contact_event& e);
        void                       fill_entities(contact_event& e, body_handle a, body_handle b) const noexcept;

        std::unordered_map<u64, pair_state>             m_pairs;
        std::vector<contact_event>                      m_events;
        const body_cache*                               m_bodies{ nullptr };
        u32                                             m_current_step{ 0 };
        u32                                             m_dropped{ 0 };
    };

}
