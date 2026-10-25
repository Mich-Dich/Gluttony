
#include "util/pch.h"
#include "game_layer.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::game {

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

    game_layer::game_layer() : layer("game_layer") {

        // Primary session is eager: the editor needs a world to attach its viewport renderer to before any Play action happens
        if (auto* s = create_session())
            m_primary_id = s->id();
    }


    // Slot storage releases every session. Renderers were already torn down by whoever created them; game_session never owned them
    game_layer::~game_layer() { }

    // CLASS PUBLIC ====================================================================================================

    game_session* game_layer::create_session() {

        u32 index{};

        if (!m_free_slots.empty()) {

            index = m_free_slots.back();
            m_free_slots.pop_back();
            ++m_slots[index].generation;                // stale handles become invalid

        } else {

            index = static_cast<u32>(m_slots.size());
            m_slots.emplace_back();
            m_slots.back().generation = 1;
        }

        const session_id id{ index, m_slots[index].generation };
        m_slots[index].session = create_unique_ref<game_session>(id);
        return m_slots[index].session.get();
    }


    void game_layer::destroy_session(session_id id) {

        auto* s = find_slot(id);
        if (!s)
            return;

        // Never destroy the primary: "Stop" must not delete the editor's world
        if (id == m_primary_id) {
            LOG(warn, "Refusing to destroy primary session")
            return;
        }

        s->session.reset();
        m_free_slots.push_back(id.index);
        // Generation is bumped on reuse, so any session_id still pointing at this slot compares unequal and find_slot returns nullptr
    }


    game_session* game_layer::get_session(session_id id) const noexcept {

        const auto* s = find_slot(id);
        return s ? s->session.get() : nullptr;
    }


    game_session* game_layer::primary_session() const noexcept {

        const auto* s = find_slot(m_primary_id);
        return s ? s->session.get() : nullptr;
    }


    std::vector<game_session*> game_layer::sessions() const {

        std::vector<game_session*> out;
        out.reserve(m_slots.size());
        for (const auto& s : m_slots)
            if (s.session)
                out.push_back(s.session.get());
        return out;
    }


    void game_layer::update(f32 dt) {

        // Primary first, then sim sessions in slot order. Deterministic and cheap — the slot table usually has 1..4 live entries
        for (auto& s : m_slots)
            if (s.session)
                s.session->update(dt);
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    game_layer::slot* game_layer::find_slot(session_id id) noexcept {

        if (id.index >= m_slots.size())
            return nullptr;

        auto& s = m_slots[id.index];
        if (s.generation != id.generation || !s.session)
            return nullptr;

        return &s;
    }


    const game_layer::slot* game_layer::find_slot(session_id id) const noexcept {

        if (id.index >= m_slots.size())
            return nullptr;

        const auto& s = m_slots[id.index];
        if (s.generation != id.generation || !s.session)
            return nullptr;

        return &s;
    }

}
