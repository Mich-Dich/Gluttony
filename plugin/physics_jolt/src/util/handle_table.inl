
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

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // Index + generation table. Index 0 is reserved as always-invalid; generation starts at 1 on first allocate and increments
    // on every subsequent allocate of the same slot, so a stale Handle never matches a live one

    template<typename Handle, typename Payload>
    handle_table<Handle, Payload>::handle_table() { m_slots.push_back({}); }


    template<typename Handle, typename Payload>
    [[nodiscard]] Handle handle_table<Handle, Payload>::insert(Payload load) {

        u32 idx;
        if (m_free_head != INVALID_INDEX) {
            idx = m_free_head;
            m_free_head = m_slots[idx].next_free;
        } else {
            idx = static_cast<u32>(m_slots.size());
            m_slots.emplace_back();
        }
        slot& s = m_slots[idx];
        if (++s.generation == 0)
            s.generation = 1;   // skip 0; contract §Handles

        s.payload = std::move(load);
        s.live = true;
        ++m_live_count;
        return Handle{ idx, s.generation };
    }


    template<typename Handle, typename Payload>
    [[nodiscard]] Payload* handle_table<Handle, Payload>::get(Handle h) {

        if (h.index == INVALID_INDEX || h.index >= m_slots.size())
            return nullptr;

        slot& s = m_slots[h.index];
        if (!s.live || s.generation != h.generation)
            return nullptr;
        return &s.payload;
    }


    template<typename Handle, typename Payload>
    [[nodiscard]] const Payload* handle_table<Handle, Payload>::get(Handle h) const { return const_cast<handle_table*>(this)->get(h); }


    template<typename Handle, typename Payload>
    [[nodiscard]] bool handle_table<Handle, Payload>::contains(Handle h) const { return get(h) != nullptr; }


    template<typename Handle, typename Payload>
    void handle_table<Handle, Payload>::remove(Handle h) {

        if (h.index == INVALID_INDEX || h.index >= m_slots.size())
            return;
        slot& s = m_slots[h.index];
        if (!s.live || s.generation != h.generation)
            return;
        s.live = false;
        s.payload = Payload{};
        s.next_free = m_free_head;
        m_free_head = h.index;
        m_live_count--;
    }


    template<typename Handle, typename Payload>
    [[nodiscard]] u32 handle_table<Handle, Payload>::live_count() const noexcept { return m_live_count; }


    template<typename Handle, typename Payload>
    template<typename F>
    void handle_table<Handle, Payload>::for_each(F&& f) const {

        for (u32 index = 1; index < m_slots.size(); index++) {
            if (m_slots[index].live)
                f(Handle{ index, m_slots[index].generation }, m_slots[index].payload);
        }
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
