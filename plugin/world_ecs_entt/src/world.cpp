
#include "util/pch.h"
#include "world.h"

#include <asset/world.h>
#include <plugin_system/i_asset_registry_plugin.h>

#include "components.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {

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

    ecs_world_plugin::ecs_world_plugin()  = default;


    ecs_world_plugin::~ecs_world_plugin() = default;

    // CLASS PUBLIC ====================================================================================================

    // i_plugin --------------------------------------------------------------------------------------------------------

    void ecs_world_plugin::on_load() {

        // Register the asset handler for world + region - everything below depends on the registry being able to reach
        m_asset_handler.on_load();

        // POD components ----------------------------------------------------------------------------------------------

        m_codec.register_component<transform>              ("glt.transform");
        m_codec.register_component<mesh_renderer>          ("glt.mesh_renderer");
        m_codec.register_component<audio_source>           ("glt.audio_source");
        m_codec.register_component<no_inherit_transform>   ("glt.no_inherit_transform");

        // custom components -------------------------------------------------------------------------------------------
        
        // name_component and hierarchy own heap data, so they get explicit codecs.
        m_codec.register_custom_component<name_component>("glt.name",

            [](const entt::registry& r, entt::entity e, std::vector<std::byte>& out) {
                const auto& n = r.get<name_component>(e);
                const u32 len = static_cast<u32>(n.name.size());
                const auto* lp = reinterpret_cast<const std::byte*>(&len);
                out.insert(out.end(), lp, lp + sizeof(len));
                const auto* sp = reinterpret_cast<const std::byte*>(n.name.data());
                out.insert(out.end(), sp, sp + len);
            },

            [](entt::registry& r, entt::entity e, std::span<const std::byte> data) {
                if (data.size() < sizeof(u32)) return;
                u32 len;
                std::memcpy(&len, data.data(), sizeof(len));
                if (data.size() < sizeof(u32) + len) return;
                auto& n = r.emplace_or_replace<name_component>(e);
                n.name.assign(reinterpret_cast<const char*>(data.data() + sizeof(u32)), len);
            });


        // hierarchy: parent pointer only. children lists are rebuilt after load by rebuild_hierarchy() so we don't duplicate
        // the tree in every blob.
        m_codec.register_custom_component<hierarchy>("glt.hierarchy",

            [](const entt::registry& r, entt::entity e, std::vector<std::byte>& out) {
                const auto& h = r.get<hierarchy>(e);
                const auto* p = reinterpret_cast<const std::byte*>(&h.parent);
                out.insert(out.end(), p, p + sizeof(h.parent));
            },

            [](entt::registry& r, entt::entity e, std::span<const std::byte> data) {
                if (data.size() != sizeof(entity_id)) return;
                auto& h = r.emplace_or_replace<hierarchy>(e);
                std::memcpy(&h.parent, data.data(), sizeof(entity_id));
            });


        // Editor-side registrations (the UI side). May be empty in a runtime build; components still work, they just don't show a panel.
        register_all_component_descriptors(m_components);

        LOG_LOADED
    }


    void ecs_world_plugin::on_unload() {

        unload_world();
        m_asset_handler.on_unload();
        LOG_UNLOADED
    }

    // world lifecycle -------------------------------------------------------------------------------------------------

    std::expected<void, GLT::asset::load_error> ecs_world_plugin::load_world(GLT::asset::handle world) {

        if (m_world == world)
            return {};        // already loaded
        unload_world();

        auto registry = GLT::asset::registry::get_ref();
        const auto* world_asset = registry->data_as<GLT::asset::world::world_asset>(world);
        if (!world_asset)
            return std::unexpected(GLT::asset::load_error::corrupt_header);

        m_world = world;

        // Copy the region index into our mutable runtime vector. The asset's copy is treated as a template; we only ever mutate ours.
        m_regions = world_asset->region_index;
        m_region_states.clear();
        for (const auto& r : m_regions)
            m_region_states.try_emplace(r.id);

        return {};
    }


    void ecs_world_plugin::unload_world() noexcept {

        for (auto& region : m_regions)
            if (region.is_active)
                deactivate(region);

        m_regions.clear();
        m_region_states.clear();
        m_world = {};

        m_registry.clear();
        m_slots.clear();
        m_free.clear();
    }


    GLT::asset::handle ecs_world_plugin::world_handle() const noexcept { return m_world; }


    std::expected<void, GLT::asset::load_error> ecs_world_plugin::save_world() {

        auto registry = GLT::asset::registry::get_ref();

        // Scratch reused across regions. Grows once, then allocates nothing.
        std::vector<entt::entity> live;

        for (auto& r : m_regions) {
            if (!r.is_active)
                continue;

            auto* region_asset = registry->data_as<GLT::asset::region::region_asset>(r.asset);
            if (!region_asset)
                continue;

            auto& state = m_region_states[r.id];

            // Translate our stable entity_ids back to the ECS's native handles.  Any id whose generation doesn't match its slot has been
            // recycled since the region was activated — those entities are gone and must not be serialized.
            live.clear();
            live.reserve(state.entities.size());

            for (const entity_id id : state.entities) {
                if (id.index >= m_slots.size())
                    continue;

                const slot& s = m_slots[id.index];
                if (s.generation != id.generation || s.handle == entt::null)
                    continue;

                live.push_back(s.handle);
            }

            region_asset->entity_codec = CODEC_ENTT_V1;

            m_codec.save(m_registry, std::span<const entt::entity>(live), [this](entt::entity e) { return entity_id_for(e); }, 
                region_asset->entity_data);

            if (auto res = registry->save(r.asset); !res)
                return res;
        }

        if (m_world != GLT::asset::handle{}) {
            if (auto res = registry->save(m_world); !res)
                return res;
        }

        return {};
    }

    // entity lifecycle ------------------------------------------------------------------------------------------------

    entity_id ecs_world_plugin::spawn() { return alloc_slot(); }


    void ecs_world_plugin::despawn(entity_id id) noexcept { release_slot(id); }


    bool ecs_world_plugin::alive(entity_id id) const noexcept {

        if (id.index >= m_slots.size())
            return false;
        const slot& slot = m_slots[id.index];
        return slot.generation == id.generation
            && slot.handle != entt::null
            && m_registry.valid(slot.handle);
    }

    // region queries --------------------------------------------------------------------------------------------------

    std::span<const GLT::asset::region::region> ecs_world_plugin::regions() const noexcept { return m_regions; }


    const GLT::asset::region::region* ecs_world_plugin::region_at(const glm::vec3& point) const noexcept {

        const GLT::asset::region::region* best = nullptr;
        f32 best_volume = std::numeric_limits<f32>::max();

        for (const auto& region : m_regions) {
            if (!region.bounds.contains(point))
                continue;
            const glm::vec3 e = region.bounds.max - region.bounds.min;
            const f32 vol = e.x * e.y * e.z;
            if (vol < best_volume) { best = &region; best_volume = vol; }
        }
        return best;
    }


    bool ecs_world_plugin::is_region_active(GLT::UUID id) const noexcept {

        for (const auto& region : m_regions)
            if (region.id == id)
                return region.is_active;
        return false;
    }


    void ecs_world_plugin::set_region_active(GLT::UUID id, bool active) {

        auto* region = find_region(id);
        if (!region)
            return;

        if (active && !region->is_active)
            activate(*region);

        else if (!active && region->is_active)
            deactivate(*region);
    }

    // streaming -------------------------------------------------------------------------------------------------------

    void ecs_world_plugin::set_streaming_anchor(const glm::vec3& position) { m_streaming_anchor = position; }


    [[nodiscard]] glm::vec3 ecs_world_plugin::get_streaming_anchor() const noexcept { return m_streaming_anchor; }


    void ecs_world_plugin::set_streaming_radius(const f32 radius) { m_streaming_radius = radius > 0.f ? radius : 0.f; };


    [[nodiscard]] f32 ecs_world_plugin::get_streaming_radius() const noexcept { return m_streaming_radius; }

    // update ----------------------------------------------------------------------------------------------------------

    void ecs_world_plugin::update(f32 /*delta_time*/) {

        // The only engine-driven work here is streaming. Game systems run outside the plugin (the concrete world's own update path).
        stream_pass();
    }

    // hierarchy -------------------------------------------------------------------------------------------------------

    std::vector<entity_id> ecs_world_plugin::root_entities() const {

        std::vector<entity_id> roots;
        roots.reserve(m_slots.size());

        for (u32 i = 0; i < m_slots.size(); ++i) {

            if (m_slots[i].handle == entt::null)
                continue;

            const entity_id id{ i, m_slots[i].generation };

            // Orphans count as roots so a corrupted parent link doesn't make
            // an entity unreachable in the outliner.
            const hierarchy* h = hierarchy_of(id);
            if (!h || !h->parent.is_valid() || !alive(h->parent))
                roots.push_back(id);
        }
        return roots;
    }


    std::vector<entity_id> ecs_world_plugin::children_of(entity_id id) const {

        if (const hierarchy* h = hierarchy_of(id))
            return h->children;
        return {};
    }


    std::string_view ecs_world_plugin::entity_name(entity_id id) const {

        if (const auto* n = m_registry.try_get<name_component>(entt_of(id)))
            return n->name;
        return {};
    }


    bool ecs_world_plugin::has_children(entity_id id) const noexcept {

        if (const hierarchy* h = hierarchy_of(id))
            return !h->children.empty();
        return false;
    }


    void ecs_world_plugin::set_parent(entity_id child, entity_id new_parent) {

        if (!alive(child))
            return;

        if (new_parent == child)
            return;   // self-parent

        if (new_parent.is_valid() && !alive(new_parent))
            return;   // dead target

        // Cycle guard: refuse if new_parent is a descendant of child.
        for (entity_id c = new_parent; c.is_valid(); c = parent_of(c))
            if (c == child)
                return;

        entt::entity ent = entt_of(child);
        hierarchy& h = m_registry.get_or_emplace<hierarchy>(ent);

        // Detach from old parent's child list.
        if (h.parent.is_valid()) {
            if (auto* old = hierarchy_of(h.parent)) {
                auto& children_vec = old->children;
                children_vec.erase(std::remove(children_vec.begin(), children_vec.end(), child), children_vec.end());
            }
        }

        // Attach to new parent.
        if (new_parent.is_valid()) {
            auto& p = m_registry.get_or_emplace<hierarchy>(entt_of(new_parent));
            if (std::find(p.children.begin(), p.children.end(), child) == p.children.end())
                p.children.push_back(child);
            h.parent = new_parent;
        } else {
            h.parent = INVALID_ENTITY;
        }
    }

    // builder API -----------------------------------------------------------------------------------------------------

    entity_builder ecs_world_plugin::make_entity(std::string_view name) {

        const entity_id id = spawn();
        entity_builder b{ *this, id };
        if (!name.empty())
            b.named(std::string(name));
        return b;
    }


    entity_builder ecs_world_plugin::edit(entity_id id) { return entity_builder{ *this, id }; }

    // hierarchy queries -----------------------------------------------------------------------------------------------

    entity_id ecs_world_plugin::parent_of(entity_id id) const noexcept {

        if (const hierarchy* h = hierarchy_of(id))
            return h->parent;
        return INVALID_ENTITY;
    }

    // ECS internals (used by entity_builder) --------------------------------------------------------------------------

    entt::entity ecs_world_plugin::entt_of(entity_id id) const noexcept {

        if (id.index >= m_slots.size())
            return entt::null;

        const slot& s = m_slots[id.index];
        if (s.generation != id.generation)
            return entt::null;

        return s.handle;
    }


    entity_id ecs_world_plugin::id_of(entt::entity e) const noexcept {

        for (u32 i = 0; i < m_slots.size(); ++i)
            if (m_slots[i].handle == e)
                return { i, m_slots[i].generation };
        return INVALID_ENTITY;
    }
        
    // i_world_inspector -----------------------------------------------------------------------------------------------

    std::span<const GLT::world::component_descriptor> ecs_world_plugin::descriptors() const noexcept { return m_components.all(); }


    std::vector<const GLT::world::component_descriptor*> ecs_world_plugin::components_on(entity_id id) const { return m_components.on(id); }


    bool ecs_world_plugin::add_component(entity_id id, u64 hash) { return m_components.add(id, hash); }


    bool ecs_world_plugin::remove_component(entity_id id, u64 hash) { return m_components.remove(id, hash); }


    void ecs_world_plugin::copy_components(entity_id from, entity_id to) { m_components.copy(from, to); }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    entity_id ecs_world_plugin::entity_id_for(entt::entity e) const noexcept {

        for (u32 i = 0; i < m_slots.size(); ++i)
            if (m_slots[i].handle == e)
                return { i, m_slots[i].generation };
        return INVALID_ENTITY;
    }

    // slot management -------------------------------------------------------------------------------------------------

    entity_id ecs_world_plugin::alloc_slot() {

        if (!m_free.empty()) {
            const u32 idx = m_free.back();
            m_free.pop_back();
            slot& slot = m_slots[idx];
            slot.generation = (slot.generation + 1) ? slot.generation + 1 : 1;   // skip 0
            slot.handle = m_registry.create();
            return { idx, slot.generation };
        }

        m_slots.push_back({ .handle = m_registry.create(), .generation = 1 });
        return { static_cast<u32>(m_slots.size() - 1), 1 };
    }


    void ecs_world_plugin::release_slot(entity_id id) {

        if (id.index >= m_slots.size())
            return;
        slot& slot = m_slots[id.index];
        if (slot.generation != id.generation)
            return;   // stale handle

        if (slot.handle != entt::null)
            m_registry.destroy(slot.handle);

        slot.handle = entt::null;
        slot.generation = (slot.generation + 1) ? slot.generation + 1 : 1;
        m_free.push_back(id.index);
    }


    std::pair<entity_id, entt::entity> ecs_world_plugin::allocate_for_load(entity_id preferred) {

        if (preferred.index >= m_slots.size())
            m_slots.resize(preferred.index + 1);

        slot& slot = m_slots[preferred.index];

        if (slot.handle == entt::null) {                   // Slot is free: honour the file'slot id verbatim.

            slot.generation = preferred.generation ? preferred.generation : 1;
            slot.handle = m_registry.create();
            return { { preferred.index, slot.generation }, slot.handle };
        }

        // Slot in use: fall back to a fresh id. Cross-region references that pointed at `preferred` will dangle - that's the caller's problem.
        const entity_id fresh = alloc_slot();
        return { fresh, m_slots[fresh.index].handle };
    }

    // streaming -------------------------------------------------------------------------------------------------------

    bool ecs_world_plugin::should_be_active(const GLT::asset::region::region& region) const noexcept {

        if (region.flags & 0x01)
            return true;        // always_loaded
        auto b = region.bounds;
        b.min -= glm::vec3(m_streaming_radius);
        b.max += glm::vec3(m_streaming_radius);
        return b.contains(m_streaming_anchor);
    }


    void ecs_world_plugin::stream_pass() {

        for (auto& region : m_regions) {
            const bool want = should_be_active(region);
            if (want && !region.is_active)
                activate(region);
            else if (!want && region.is_active)
                deactivate(region);
        }
    }


    void ecs_world_plugin::activate(GLT::asset::region::region& region) {

        auto registry = GLT::asset::registry::get_ref();

        // We treat region.asset as already resolved (see notes at bottom).
        VALIDATE(region.asset != GLT::asset::handle{}, region.is_active = true; return, "", "region asset handle is unresolved; skipping entity load")
        // don't retry every frame

        const auto* region_asset = registry->data_as<GLT::asset::region::region_asset>(region.asset);
        VALIDATE(region_asset, region.is_active = true; return, "", "handle is not a region_asset")

        auto& state = m_region_states[region.id];
        state.entities.clear();

        if (region_asset->entity_codec == CODEC_ENTT_V1 && !region_asset->entity_data.empty()) {
            state.entities = m_codec.load(
                m_registry,
                std::span<const std::byte>(region_asset->entity_data),
                [this](entity_id saved) { return allocate_for_load(saved); });
        }

        // The entity blob stores parent pointers only. Rebuild the children lists so queries and the editor's outliner see a consistent tree.
        // Cheap: O(loaded entities) per activation, only when the region actually contains hierarchy components.
        if (state.entities.empty() == false) {

            for (const entity_id child : state.entities) {

                auto* h = m_registry.try_get<hierarchy>(entt_of(child));
                if (!h || !h->parent.is_valid())
                    continue;

                if (auto* p = hierarchy_of(h->parent)) {
                    if (std::find(p->children.begin(), p->children.end(), child) == p->children.end())
                        p->children.push_back(child);
                }
            }
        }

        region.is_active = true;
    }


    void ecs_world_plugin::deactivate(GLT::asset::region::region& region) {

        auto& state = m_region_states[region.id];
        for (const entity_id id : state.entities)
            release_slot(id);
        state.entities.clear();
        region.is_active = false;
    }


    GLT::asset::region::region* ecs_world_plugin::find_region(GLT::UUID id) noexcept {

        for (auto& region : m_regions)
            if (region.id == id)
                return &region;
        return nullptr;
    }


    hierarchy* ecs_world_plugin::hierarchy_of(entity_id id) noexcept {

        const entt::entity e = entt_of(id);
        if (e == entt::null)
            return nullptr;
        return m_registry.try_get<hierarchy>(e);
    }


    const hierarchy* ecs_world_plugin::hierarchy_of(entity_id id) const noexcept {

        const entt::entity e = entt_of(id);
        if (e == entt::null)
            return nullptr;
        return m_registry.try_get<hierarchy>(e);
    }

}
