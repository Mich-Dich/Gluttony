
#include "util/pch.h"
#include "util/jolt_pch.h"
#include "contact_listener.h"



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

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    contact_listener::contact_listener(const body_cache* bodies) noexcept : m_bodies(bodies) {

        m_events.reserve(kEventBufferCapacity);
    }

    // CLASS PUBLIC ====================================================================================================

    void contact_listener::begin_step() noexcept { ++m_current_step; }


    void contact_listener::OnContactAdded(const JPH::Body& b1, const JPH::Body& b2, const JPH::ContactManifold& m, JPH::ContactSettings&) {

        const body_handle ha = unpack_body_handle(b1.GetUserData());
        const body_handle hb = unpack_body_handle(b2.GetUserData());
        if (!ha.is_valid() || !hb.is_valid())
            return;

        const u64 key = pair_key(b1.GetID(), b2.GetID());
        auto it = m_pairs.find(key);

        if (it != m_pairs.end()) {
            // Another manifold formed between the same two bodies — begin was already emitted
            ++it->second.manifold_count;
            return;
        }

        pair_state s;
        s.a = ha;
        s.b = hb;
        s.manifold_count = 1;
        s.last_persist_step = 0;

        if (include_phase(ha, hb, contact_event_mask::begin)) {
            contact_event ev{};
            ev.a = ha;
            ev.b = hb;
            fill_entities(ev, ha, hb);

            ev.point = from_jolt(m.GetWorldSpaceContactPointOn1(0));
            ev.normal = from_jolt(m.mWorldSpaceNormal);
            ev.penetration = m.mPenetrationDepth;
            ev.phase = contact_phase::phase_begin;
            emit(ev);
        }
        m_pairs.emplace(key, s);
    }


    void contact_listener::OnContactPersisted(const JPH::Body& b1, const JPH::Body& b2, const JPH::ContactManifold&, JPH::ContactSettings&) {

        const body_handle ha = unpack_body_handle(b1.GetUserData());
        const body_handle hb = unpack_body_handle(b2.GetUserData());
        if (!ha.is_valid() || !hb.is_valid())
            return;

        const u64 key = pair_key(b1.GetID(), b2.GetID());
        auto it = m_pairs.find(key);

        if (it == m_pairs.end()) {
            // Defensive: a persist fired without a matching added. Track and emit begin with zero contact data, so the
            // pair is bookkept correctly for phase_end on destroy
            pair_state s;
            s.a = ha;
            s.b = hb;
            s.manifold_count = 1;
            s.last_persist_step = 0;
            it = m_pairs.emplace(key, s).first;
        }

        // Dedupe: one phase_persist per body pair per step, regardless of manifold count
        if (it->second.last_persist_step == m_current_step)
            return;
        it->second.last_persist_step = m_current_step;

        if (!include_phase(ha, hb, contact_event_mask::persist))
            return;

        contact_event ev{};
        ev.a = ha;
        ev.b = hb;
        fill_entities(ev, ha, hb);
        ev.phase = contact_phase::phase_persist;
        emit(ev);
    }


    void contact_listener::OnContactRemoved(const JPH::SubShapeIDPair& pair) {

        const u64 key = pair_key(pair.GetBody1ID(), pair.GetBody2ID());
        auto it = m_pairs.find(key);
        if (it == m_pairs.end())
            return;   // already handled by destroy_body(), or pair was never tracked

        if (--it->second.manifold_count > 0)
            return;

        const body_handle ha = it->second.a;
        const body_handle hb = it->second.b;

        if (include_phase(ha, hb, contact_event_mask::end)) {
            contact_event ev{};
            ev.a = ha;
            ev.b = hb;
            fill_entities(ev, ha, hb);
            ev.phase = contact_phase::phase_end;
            emit(ev);
        }

        m_pairs.erase(it);
    }


    void contact_listener::destroy_body(body_handle h) {

        for (auto it = m_pairs.begin(); it != m_pairs.end(); ) {
            if (it->second.a == h || it->second.b == h) {

                const body_handle ha = it->second.a;
                const body_handle hb = it->second.b;

                if (include_phase(ha, hb, contact_event_mask::end)) {
                    contact_event ev{};
                    ev.a = ha;
                    ev.b = hb;
                    fill_entities(ev, ha, hb);
                    ev.phase = contact_phase::phase_end;
                    emit(ev);
                }

                it = m_pairs.erase(it);
            } else {
                ++it;
            }
        }
    }


    std::span<const contact_event> contact_listener::drain() noexcept {

        auto result = std::span<const contact_event>(m_events.data(), m_events.size());
        m_events.clear();
        return result;
    }


    void contact_listener::clear() noexcept {

        m_pairs.clear();
        m_events.clear();
        m_current_step = 0;
        m_dropped = 0;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    u64 contact_listener::pair_key(JPH::BodyID a, JPH::BodyID b) noexcept {

        u32 ia = a.GetIndexAndSequenceNumber();
        u32 ib = b.GetIndexAndSequenceNumber();
        if (ia > ib)
            std::swap(ia, ib);
        return (static_cast<u64>(ia) << 32) | ib;
    }


    contact_event_mask contact_listener::mask_of(body_handle h) const noexcept {

        if (!m_bodies)
            return contact_event_mask::none;
        const body_entry* e = m_bodies->get(h);
        return e ? e->contact_mask : contact_event_mask::none;
    }


    bool contact_listener::include_phase(body_handle a, body_handle b, contact_event_mask phase) const noexcept {

        const u8 mask = static_cast<u8>(mask_of(a)) | static_cast<u8>(mask_of(b));
        return (mask & static_cast<u8>(phase)) != 0;
    }


    void contact_listener::fill_entities(contact_event& e, body_handle a, body_handle b) const noexcept {

        if (!m_bodies)
            return;
        if (const auto* ea = m_bodies->get(a)) e.entity_a = ea->entity;
        if (const auto* eb = m_bodies->get(b)) e.entity_b = eb->entity;
    }


    void contact_listener::emit(const contact_event& e) {

        if (m_events.size() >= kEventBufferCapacity) {
            ++m_dropped;
            return;
        }
        m_events.push_back(e);
    }

}
