
#include "util/pch.h"
#include "world_change.h"

#include <world/i_world.h>


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

    // FUNCTION IMPLEMENTATION =========================================================================================

    world_state capture(const world::i_world& world, std::span<const world::entity_id> ids) {

        world_state state;
        state.ids.reserve(ids.size());
        for (const auto id : ids)
            if (id.is_valid() && std::find(state.ids.begin(), state.ids.end(), id) == state.ids.end())
                state.ids.push_back(id);

        for (const auto id : state.ids)
            if (world.alive(id))
                state.alive.push_back(id);

        state.blob = world.snapshot_entities(state.alive);
        return state;
    }


    void apply(world::i_world& world, const world_state& state) {

        // Children first, so a parent is never despawned while its children still point at it
        for (auto it = state.ids.rbegin(); it != state.ids.rend(); ++it) {
            const auto id = *it;
            const bool should_exist = std::find(state.alive.begin(), state.alive.end(), id) != state.alive.end();
            if (!should_exist && world.alive(id))
                world.despawn(id);
        }

        if (!state.blob.empty())
            (void)world.restore_entities(state.blob);
    }


    step make_world_step(std::string name, world::i_world& world, world_state before, world_state after, on_applied_fn on_applied) {

        // Both states are needed by both callbacks -> share instead of copying the blobs twice
        auto states = std::make_shared<std::pair<world_state, world_state>>(std::move(before), std::move(after));
        world::i_world* target = &world;

        step result;
        result.name = std::move(name);
        result.undo = [target, states, on_applied]() {
            apply(*target, states->first);
            if (on_applied)
                on_applied(states->first);
        };
        result.redo = [target, states, on_applied]() {
            apply(*target, states->second);
            if (on_applied)
                on_applied(states->second);
        };
        return result;
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // ---- world_change -----------------------------------------------------------------------------------------------

    world_change::world_change(world::i_world& world, std::span<const world::entity_id> ids)
        : m_world(world), m_before(capture(world, ids)) {}


    void world_change::created(world::entity_id id) {

        if (id.is_valid() && std::find(m_before.ids.begin(), m_before.ids.end(), id) == m_before.ids.end())
            m_before.ids.push_back(id);                 // part of the state, but not in [alive] -> "did not exist"
    }


    bool world_change::commit(stack& history, std::string name, on_applied_fn on_applied) {

        world_state after = capture(m_world, m_before.ids);
        if (after == m_before)
            return false;

        history.push(make_world_step(std::move(name), m_world, std::move(m_before), std::move(after), std::move(on_applied)));
        return true;
    }

    // ---- world_edit_tracker -----------------------------------------------------------------------------------------

    void world_edit_tracker::begin(world::i_world* world, std::span<const world::entity_id> ids) {

        const bool same_target = world == m_world
            && ids.size() == m_ids.size()
            && std::equal(ids.begin(), ids.end(), m_ids.begin());

        if (!same_target) {
            commit();                                   // selection changed mid-edit: close the old edit first
            m_world = world;
            m_ids.assign(ids.begin(), ids.end());
        }

        if (!m_world || m_ids.empty()) {
            m_frame_before = {};
            return;
        }

        if (!m_open)                                    // while an edit is open its "before" is already known
            m_frame_before = capture(*m_world, m_ids);
    }


    void world_edit_tracker::end(std::string_view name, const bool interacting, const bool user_input) {

        if (!m_world || m_ids.empty()) {
            m_name_override.clear();
            return;
        }

        world_state now = capture(*m_world, m_ids);

        // Widgets write components through raw pointers. The world caches render data of fixed entities, so tell it what
        // changed (on any frame the state differs, user input or not). Compared against the state before this frame
        if (!(now == (m_open ? m_latest : m_frame_before)))
            for (const world::entity_id id : m_ids)
                m_world->mark_changed(id);

        if (!m_open) {

            if (now == m_frame_before || !user_input) {
                m_name_override.clear();
                return;                                 // nothing changed, or not changed by the user
            }

            m_open = true;
            m_before = std::move(m_frame_before);
            m_name = std::string(name);
        }

        if (!m_name_override.empty())
            m_name = std::move(m_name_override);
        m_name_override.clear();

        m_latest = std::move(now);

        if (!interacting)
            commit();
    }


    void world_edit_tracker::set_name(std::string name)  { m_name_override = std::move(name); }


    void world_edit_tracker::commit() {

        if (!m_open)
            return;

        m_open = false;
        if (m_world && !(m_latest == m_before))
            m_history.push(make_world_step(std::move(m_name), *m_world, std::move(m_before), std::move(m_latest), on_applied));

        m_before = {};
        m_latest = {};
        m_name.clear();
    }


    void world_edit_tracker::reset() {

        m_open = false;
        m_world = nullptr;
        m_ids.clear();
        m_frame_before = {};
        m_before = {};
        m_latest = {};
        m_name.clear();
        m_name_override.clear();
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
