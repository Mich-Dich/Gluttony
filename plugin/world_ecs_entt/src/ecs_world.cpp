#include "util/pch.h"
#include "ecs_world.h"
#include "ecs_world_plugin.h"

#include <glm/gtc/type_ptr.hpp>
#include <cmath>

#include <application.h>
#include <asset/world.h>
#include <asset/i_asset_registry.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    static glm::mat4 compose_transform(const component::transform& t);


    static glm::mat3 rotation_matrix_from_euler(const glm::vec3& euler_rad);


    static glm::quat quat_from_euler_yxz(const glm::vec3& euler_rad);


    static glm::vec3 euler_yxz_from_quat(const glm::quat& q);

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // position/rotation(euler,rad)/scale -> mat4. YXZ order.
    static glm::mat4 compose_transform(const component::transform& t) {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), t.position);
        m = glm::rotate(m, t.rotation.y, glm::vec3(0, 1, 0));
        m = glm::rotate(m, t.rotation.x, glm::vec3(1, 0, 0));
        m = glm::rotate(m, t.rotation.z, glm::vec3(0, 0, 1));
        m = glm::scale(m, t.scale);
        return m;
    }


    static glm::mat3 rotation_matrix_from_euler(const glm::vec3& euler_rad) {

        glm::mat4 r = glm::rotate(glm::mat4(1.0f), euler_rad.y, glm::vec3(0, 1, 0));
        r = glm::rotate(r, euler_rad.x, glm::vec3(1, 0, 0));
        r = glm::rotate(r, euler_rad.z, glm::vec3(0, 0, 1));
        return glm::mat3(r);
    }


    static glm::quat quat_from_euler_yxz(const glm::vec3& euler_rad) {

        const f32 hx = euler_rad.x * 0.5f;
        const f32 hy = euler_rad.y * 0.5f;
        const f32 hz = euler_rad.z * 0.5f;
        const glm::quat qx{ std::cos(hx), std::sin(hx), 0.f,        0.f };
        const glm::quat qy{ std::cos(hy), 0.f,         std::sin(hy), 0.f };
        const glm::quat qz{ std::cos(hz), 0.f,         0.f,         std::sin(hz) };
        return qy * qx * qz;
    }


    static glm::vec3 euler_yxz_from_quat(const glm::quat& q) {

        const glm::quat n = glm::normalize(q);
        const glm::mat3 m = glm::mat3_cast(n);
        glm::vec3 euler{ 0.f };
        euler.x = std::asin(glm::clamp(-m[2][1], -1.f, 1.f));
        const f32 cx = std::cos(euler.x);
        if (std::abs(cx) > 1e-6f) {
            euler.y = std::atan2(m[0][2], m[2][2]);
            euler.z = std::atan2(m[0][1], m[1][1]);
        } else {
            euler.y = std::atan2(-m[0][2], m[0][0]);
            euler.z = 0.f;
        }
        return euler;
    }

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    ecs_world::ecs_world(ecs_world_plugin& plugin, asset::asset_ref asset)
        : m_plugin(plugin)
        , m_codec(plugin.codec())
        , m_components(*this)
        , m_asset(std::move(asset)) {

        component::register_all_component_descriptors(m_components);
    }


    ecs_world::~ecs_world() {

        // Same release loop as old unload_world().
        if (auto registry = asset::registry::get_ref()) {
            for (const auto& region : m_regions)
                if (region.asset != INVALID_HANDLE)
                    registry->release(region.asset);
        }
        // m_registry / m_slots / etc. unwind via their own destructors.
    }

    // CLASS PUBLIC ====================================================================================================

    std::expected<void, asset::load_error> ecs_world::init() { return init_from_world_asset(); }


    std::expected<void, asset::load_error> ecs_world::init_from_world_asset() {

        auto registry = asset::registry::get_ref();
        const auto* world_asset = registry->data_as<asset::world::world_asset>(m_asset.get());
        if (!world_asset)
            return std::unexpected{ asset::load_error::corrupt_header };

        m_region_cell_size = world_asset->region_cell_size;
        VALIDATE(m_region_cell_size.x > 0.f && m_region_cell_size.y > 0.f && m_region_cell_size.z > 0.f,
            m_region_cell_size = glm::vec3(256.f), "", "Corrupt file holds invalid region cell size");

        m_streaming_radius = world_asset->streaming_radius;
        VALIDATE(m_streaming_radius >= 0.f, m_streaming_radius = 512.f, "", "corrupt file has invalid streaming radius");

        m_regions = world_asset->region_index;
        m_region_states.clear();
        for (const auto& r : m_regions)
            m_region_states.try_emplace(r.id);


        // resolve + retain --------------------------------------------------------------------------------------------

        // After this loop, every entry in m_regions holds a plugin-owned reference. The plugin retains on load/create
        // and releases on unload - uniform, and it stops us from releasing handles we never held
        for (auto& region : m_regions) {

            // Already resolved by the world's dep chain. Take our own reference
            if (region.asset != INVALID_HANDLE) {
                registry->retain(region.asset);
                continue;
            }

            const glm::ivec3 cell = cell_coord_of((region.bounds.min + region.bounds.max) * 0.5f);

            // Try UUID first - the region may be loaded under a path the world's dep table no longer reflects
            if (region.id != 0) {
                if (auto h = registry->find_by_id(region.id); h != INVALID_HANDLE) {

                    const auto& inf = registry->info(h);
                    if (inf.asset_type == GLT::asset::core_types::region) {

                        region.asset = GLT::asset::asset_ref{ h };
                        LOG(info, "recovered region {:#x} via UUID lookup", static_cast<unsigned long long>(region.id));
                        continue;
                    }

                    LOG(warn, "UUID {:#x} is a {} asset - skipping",
                        static_cast<unsigned long long>(region.id), GLT::asset::type_to_string(inf.asset_type));
                }
            }

            // Path-based recovery: file exists on disk, or mint a fresh one. resolve_region_handle already retains on success
            const GLT::asset::handle h = resolve_region_handle(cell, region.id);
            VALIDATE(h != INVALID_HANDLE, continue, "", "region for cell ({},{},{}) unresolvable - skipping", (cell.x, cell.y, cell.z));

            region.asset = h;

            const GLT::UUID actual_id = registry->info(h).id;
            VALIDATE(actual_id == region.id, region.id = actual_id,
                "", "region file at cell ({},{},{}) has UUID {:#x} but world declared {:#x} - reconciling",
                (cell.x, cell.y, cell.z, static_cast<unsigned long long>(actual_id), static_cast<unsigned long long>(region.id)))
        }

        // rebuild region_states map if any UUID was reconciled
        {
            std::unordered_map<UUID, region_state> fresh;
            fresh.reserve(m_regions.size());
            for (const auto& r : m_regions) fresh.try_emplace(r.id);
            m_region_states = std::move(fresh);
        }

        if (m_regions.empty())
            (void)ensure_region_for_cell(glm::ivec3(0));

        m_active_camera = (world_asset->active_camera_index == 0xFFFFFFFFu) ? INVALID_ENTITY
            : entity_id{ world_asset->active_camera_index, world_asset->active_camera_generation };

        return {};
    }

    // i_world persistence ---------------------------------------------------------------------------------------------

    std::expected<void, asset::load_error> ecs_world::save() {

        if (!m_asset.valid())
            return std::unexpected{ asset::load_error::not_found };

        auto registry = asset::registry::get_ref();

        reassign_all_entities();
        prune_empty_regions();
        if (auto r = flush_regions(); !r)
            return r;

        sync_world_asset();
        return registry->save(m_asset.get());
    }


    std::expected<void, asset::load_error> ecs_world::save_as(const std::filesystem::path& path) {

        if (path.empty())
            return std::unexpected{ asset::load_error::not_found };

        auto registry = asset::registry::get_ref();

        if (!m_asset.valid()) {

            // Mint a fresh world asset at `path`.
            auto world_asset = create_unique_ref<asset::world::world_asset>();
            world_asset->asset_type = asset::core_types::world;
            world_asset->region_cell_size = m_region_cell_size;

            auto handle_res = registry->register_runtime(std::move(world_asset), path, path.stem().string());
            if (!handle_res)
                return std::unexpected{ handle_res.error() };
            m_asset = std::move(*handle_res);

            if (m_regions.empty())
                (void)ensure_region_for_cell(glm::ivec3(0));

            return save();
        }

        if (registry->info(m_asset.get()).virtual_path != path) {

            reassign_all_entities();
            prune_empty_regions();
            if (auto r = flush_regions(); !r)
                return r;

            sync_world_asset();
            return registry->save_as(m_asset.get(), path);
        }

        return save();
    }

    // entity lifecycle ------------------------------------------------------------------------------------------------

    entity_id ecs_world::spawn() {

        const entity_id id = alloc_slot();

        // Give the entity a transform up front so its position is well-defined for cell assignment
        // Safe: emplace is idempotent if a descriptor already added one.
        m_registry.emplace_or_replace<component::transform>(entt_of(id));
        assign_entity_to_cell(id);

        return id;
    }


    void ecs_world::despawn(entity_id id) noexcept { release_slot(id); }


    bool ecs_world::alive(entity_id id) const noexcept {

        if (id.index >= m_slots.size())
            return false;
        const slot& slot = m_slots[id.index];
        return slot.generation == id.generation
            && slot.handle != entt::null
            && m_registry.valid(slot.handle);
    }

    // transform -------------------------------------------------------------------------------------------------------

    glm::vec3 ecs_world::get_local_position(entity_id id) const {

        const entt::entity entity = entt_of(id);
        if (entity == entt::null)
            return glm::vec3(0.f);

        if (const auto* transform = m_registry.try_get<component::transform>(entity))
            return transform->position;

        return glm::vec3(0.f);
    }


    glm::vec3 ecs_world::get_local_rotation(entity_id id) const {

        const entt::entity entity = entt_of(id);
        if (entity == entt::null)
            return glm::vec3(0.f);

        if (const auto* transform = m_registry.try_get<component::transform>(entity))
            return transform->rotation;

        return glm::vec3(0.f);
    }


    void ecs_world::translate_local(entity_id id, const glm::vec3& local_delta) {

        if (!alive(id))
            return;

        auto& transform = m_registry.get_or_emplace<component::transform>(entt_of(id));
        transform.position += rotation_matrix_from_euler(transform.rotation) * local_delta;

        // World position changed -> which cell is this entity in now?
        assign_entity_to_cell(id);
    }


    void ecs_world::rotate_local(entity_id id, const glm::vec3& euler_delta) {

        if (!alive(id))
            return;

        auto& transform = m_registry.get_or_emplace<component::transform>(entt_of(id));
        transform.rotation += euler_delta;
        // Rotation doesn't move the pivot; no re-bin needed
    }


    glm::quat ecs_world::get_local_rotation_q(entity_id id) const {

        const entt::entity entity = entt_of(id);
        if (entity == entt::null)
            return glm::quat{ 1.f, 0.f, 0.f, 0.f };    // identity

        if (const auto* transform = m_registry.try_get<component::transform>(entity))
            return quat_from_euler_yxz(transform->rotation);

        return glm::quat{ 1.f, 0.f, 0.f, 0.f };
    }


    void ecs_world::set_local_rotation_q(entity_id id, const glm::quat& rotation) {

        if (!alive(id))
            return;

        auto& transform = m_registry.get_or_emplace<component::transform>(entt_of(id));
        transform.rotation = euler_yxz_from_quat(rotation);

        // Rotation doesn't move the pivot - no re-bin needed (same rationale as rotate_local)
    }


    glm::vec3 ecs_world::get_local_scale(entity_id id) const {

        const entt::entity entity = entt_of(id);
        if (entity == entt::null)
            return glm::vec3(1.f);    // identity

        if (const auto* transform = m_registry.try_get<component::transform>(entity))
            return transform->scale;

        return glm::vec3(1.f);
    }


    void ecs_world::set_local_scale(entity_id id, const glm::vec3& scale) {

        if (!alive(id))
            return;

        auto& transform = m_registry.get_or_emplace<component::transform>(entt_of(id));
        transform.scale = scale;

        // Scale doesn't move the pivot - no re-bin needed
    }

    // region queries --------------------------------------------------------------------------------------------------

    std::span<const GLT::asset::region::region> ecs_world::regions() const noexcept { return m_regions; }


    const GLT::asset::region::region* ecs_world::region_at(const glm::vec3& point) const noexcept {

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


    bool ecs_world::is_region_active(GLT::UUID id) const noexcept {

        for (const auto& region : m_regions)
            if (region.id == id)
                return region.is_active;
        return false;
    }


    void ecs_world::set_region_active(GLT::UUID id, bool active) {

        auto* region = find_region(id);
        if (!region)
            return;

        if (active && !region->is_active)
            activate(*region);

        else if (!active && region->is_active)
            deactivate(*region);
    }

    // streaming -------------------------------------------------------------------------------------------------------

    void ecs_world::set_streaming_anchor(const glm::vec3& position) { m_streaming_anchor = position; }


    [[nodiscard]] glm::vec3 ecs_world::get_streaming_anchor() const noexcept { return m_streaming_anchor; }


    void ecs_world::set_streaming_radius(const f32 radius) { m_streaming_radius = radius > 0.f ? radius : 0.f; };


    [[nodiscard]] f32 ecs_world::get_streaming_radius() const noexcept { return m_streaming_radius; }

    // update ----------------------------------------------------------------------------------------------------------

    void ecs_world::update(f32 /*delta_time*/) {

        // The only engine-driven work here is streaming. Game systems run outside the plugin (the concrete world's own update path)
        stream_pass();
    }

    // region grid ------------------------------------------------------------------------------------

    glm::vec3 ecs_world::get_region_cell_size() const noexcept { return m_region_cell_size; }


    void ecs_world::set_region_cell_size(const glm::vec3& size) {

        if (size.x <= 0.f || size.y <= 0.f || size.z <= 0.f) {
            LOG(warn, "set_region_cell_size ignored: non-positive component ({}, {}, {})", size.x, size.y, size.z);
            return;
        }

        m_region_cell_size = size;
        reassign_all_entities();
    }


    void ecs_world::reassign_all_entities() {

        // Regions that are currently inactive have an empty state list already
        // (deactivate() clears them), so this only affects what's resident right now.
        for (auto& [rid, state] : m_region_states)
            state.entities.clear();

        for (u32 index = 0; index < m_slots.size(); ++index) {

            const slot& slot = m_slots[index];
            if (slot.handle == entt::null)
                continue;

            const entity_id id{ index, slot.generation };
            const glm::vec3 world_pos = glm::vec3(world_transform_of(id)[3]);
            const glm::ivec3 cell = cell_coord_of(world_pos);

            const GLT::UUID region_id = ensure_region_for_cell(cell);
            if (region_id)
                m_region_states[region_id].entities.push_back(id);
        }
    }

    // hierarchy -------------------------------------------------------------------------------------------------------

    std::vector<entity_id> ecs_world::root_entities() const {

        std::vector<entity_id> roots;
        roots.reserve(m_slots.size());

        for (u32 i = 0; i < m_slots.size(); ++i) {

            if (m_slots[i].handle == entt::null)
                continue;

            const entity_id id{ i, m_slots[i].generation };

            // Orphans count as roots so a corrupted parent link doesn't make an entity unreachable in the outliner
            const component::hierarchy* h = hierarchy_of(id);
            if (!h || !h->parent.is_valid() || !alive(h->parent))
                roots.push_back(id);
        }
        return roots;
    }


    std::vector<entity_id> ecs_world::children_of(entity_id id) const {

        if (const component::hierarchy* h = hierarchy_of(id))
            return h->children;
        return {};
    }


    std::string_view ecs_world::entity_name(entity_id id) const {

        if (const auto* n = m_registry.try_get<component::name>(entt_of(id)))
            return n->name;
        return {};
    }


    bool ecs_world::has_children(entity_id id) const noexcept {

        if (const component::hierarchy* h = hierarchy_of(id))
            return !h->children.empty();
        return false;
    }


    void ecs_world::set_parent(entity_id child, entity_id new_parent) {

        if (!alive(child))
            return;

        if (new_parent == child)
            return;   // self-parent

        if (new_parent.is_valid() && !alive(new_parent))
            return;   // dead target

        // Cycle guard: refuse if new_parent is a descendant of child
        for (entity_id c = new_parent; c.is_valid(); c = parent_of(c))
            if (c == child)
                return;

        entt::entity ent = entt_of(child);
        component::hierarchy& h = m_registry.get_or_emplace<component::hierarchy>(ent);

        // Detach from old parent's child list
        if (h.parent.is_valid()) {
            if (auto* old = hierarchy_of(h.parent)) {
                auto& children_vec = old->children;
                children_vec.erase(std::remove(children_vec.begin(), children_vec.end(), child), children_vec.end());
            }
        }

        // Attach to new parent
        if (new_parent.is_valid()) {
            auto& p = m_registry.get_or_emplace<component::hierarchy>(entt_of(new_parent));
            if (std::find(p.children.begin(), p.children.end(), child) == p.children.end())
                p.children.push_back(child);
            h.parent = new_parent;
        } else {
            h.parent = INVALID_ENTITY;
        }
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    entity_builder ecs_world::make_entity(std::string_view name) {

        const entity_id id = spawn();
        entity_builder b{ *this, id };
        if (!name.empty())
            b.named(std::string(name));
        return b;
    }


    entity_builder ecs_world::edit(entity_id id) { return entity_builder{ *this, id }; }

    // hierarchy queries -----------------------------------------------------------------------------------------------

    entity_id ecs_world::parent_of(entity_id id) const noexcept {

        if (const component::hierarchy* h = hierarchy_of(id))
            return h->parent;
        return INVALID_ENTITY;
    }

    // ECS internals (used by entity_builder) --------------------------------------------------------------------------

    entt::entity ecs_world::entt_of(entity_id id) const noexcept {

        if (id.index >= m_slots.size())
            return entt::null;

        const slot& s = m_slots[id.index];
        if (s.generation != id.generation)
            return entt::null;

        return s.handle;
    }


    entity_id ecs_world::id_of(entt::entity e) const noexcept {

        for (u32 i = 0; i < m_slots.size(); ++i)
            if (m_slots[i].handle == e)
                return { i, m_slots[i].generation };
        return INVALID_ENTITY;
    }

    // i_world_inspector -----------------------------------------------------------------------------------------------

    std::span<const component_descriptor> ecs_world::descriptors() const noexcept { return m_components.all(); }


    std::vector<const component_descriptor*> ecs_world::components_on(entity_id id) const { return m_components.on(id); }


    bool ecs_world::add_component(entity_id id, u64 hash) { return m_components.add(id, hash); }


    bool ecs_world::remove_component(entity_id id, u64 hash) { return m_components.remove(id, hash); }


    void ecs_world::copy_components(entity_id from, entity_id to) { m_components.copy(from, to); }

    // i_world_scene ---------------------------------------------------------------------------------------------------

    void ecs_world::gather_scene(std::vector<GLT::asset::mesh::instance>& out) const {

        out.clear();
        auto view = m_registry.view<const component::mesh, const component::transform>();    // entt iterates both component storages once
        out.reserve(view.size_hint());

        for (auto entity : view) {

            const auto& mesh_comp = view.get<const component::mesh>(entity);
            if (!mesh_comp.visible || mesh_comp.mesh == INVALID_HANDLE)
                continue;

            // Resolve the stable entity_id so we can walk the hierarchy
            const entity_id id = id_of(entity);
            if (!id.is_valid())
                continue;

            GLT::asset::mesh::instance inst{};
            inst.mesh = mesh_comp.mesh;
            inst.material_overrides.assign(mesh_comp.material_overrides.begin(), mesh_comp.material_overrides.end());
            inst.transform = world_transform_of(id);
            inst.entity = id;
            out.push_back(inst);
        }
    }


    void ecs_world::gather_lights(std::vector<GLT::world::light_snapshot>& out) const {

        out.clear();
        auto view = m_registry.view<const component::light, const component::transform>();   // transform is required, so view constrains both
        out.reserve(view.size_hint());

        for (auto entity : view) {

            const auto& light = view.get<const component::light>(entity);
            const entity_id id = id_of(entity);
            if (!id.is_valid())
                continue;

            const glm::mat4 world = world_transform_of(id);

            GLT::world::light_snapshot snapshot{};
            snapshot.position = glm::vec3(world[3]);                                   // translation column
            snapshot.direction = -glm::normalize(glm::vec3(world[2]));                  // -Z is forward, engine convention
            snapshot.color = light.color;
            snapshot.intensity = light.intensity;
            snapshot.radius = light.radius;
            snapshot.inner_cone_angle = light.inner_cone_angle;
            snapshot.outer_cone_angle = light.outer_cone_angle;
            snapshot.type = light.type;
            snapshot.entity = id;

            out.push_back(snapshot);
        }
    }


    bool ecs_world::get_camera_view(GLT::world::camera_snapshot& out) const {

        if (!m_active_camera.is_valid())
            return false;

        const entt::entity entity = entt_of(m_active_camera);
        if (entity == entt::null)
            return false;

        const auto* c = m_registry.try_get<GLT::world::world_ecs_entt::component::camera>(entity);
        if (!c)
            return false;

        // The camera's world transform is inherited through the hierarchy just like any other entity
        // a camera parented to a moving platform moves with it.
        const glm::mat4 world = world_transform_of(m_active_camera);

        // // scale on a camera is meaningless, normalize it
        // world[0] = glm::vec4(glm::normalize(glm::vec3(world[0])), 0.f);
        // world[1] = glm::vec4(glm::normalize(glm::vec3(world[1])), 0.f);
        // world[2] = glm::vec4(glm::normalize(glm::vec3(world[2])), 0.f);

        out.view = glm::inverse(world);
        out.position = glm::vec3(world[3]);       // the translation column
        out.fov = c->fov;
        out.near_plane = c->near_plane;
        out.far_plane = c->far_plane;
        return true;
    }


    bool ecs_world::get_camera_view(world::entity_id camera, world::camera_snapshot& out) {

        if (!camera.is_valid())
            return false;

        const entt::entity entity = entt_of(camera);
        if (entity == entt::null)
            return false;

        const auto* c = m_registry.try_get<GLT::world::world_ecs_entt::component::camera>(entity);
        if (!c)
            return false;

        // The camera's world transform is inherited through the hierarchy just like any other entity
        // a camera parented to a moving platform moves with it.
        const glm::mat4 world = world_transform_of(camera);

        // // scale on a camera is meaningless, normalize it
        // world[0] = glm::vec4(glm::normalize(glm::vec3(world[0])), 0.f);
        // world[1] = glm::vec4(glm::normalize(glm::vec3(world[1])), 0.f);
        // world[2] = glm::vec4(glm::normalize(glm::vec3(world[2])), 0.f);

        out.view = glm::inverse(world);
        out.position = glm::vec3(world[3]);       // the translation column
        out.fov = c->fov;
        out.near_plane = c->near_plane;
        out.far_plane = c->far_plane;
        return true;
    }


    void ecs_world::set_active_camera(entity_id id) { m_active_camera = id; }


    void ecs_world::gather_entity_positions(std::vector<GLT::world::entity_position>& out) const {

        out.clear();
        out.reserve(m_slots.size());                 // upper bound: one transform per slot

        auto view = m_registry.view<const component::transform>();

        for (auto entity : view) {

            const entity_id id = id_of(entity);
            if (!id.is_valid())
                continue;

            GLT::world::entity_position p{};
            p.id = id;
            p.world_position = glm::vec3(world_transform_of(id)[3]);
            out.push_back(p);
        }
    }

    // slot management -------------------------------------------------------------------------------------------------

    entity_id ecs_world::alloc_slot() {

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


    void ecs_world::release_slot(entity_id id) {

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


    std::pair<entity_id, entt::entity> ecs_world::allocate_for_load(entity_id preferred) {

        if (preferred.index >= m_slots.size())
            m_slots.resize(preferred.index + 1);

        slot& slot = m_slots[preferred.index];

        if (slot.handle == entt::null) {                   // Slot is free: honour the file'slot id verbatim

            slot.generation = preferred.generation ? preferred.generation : 1;
            slot.handle = m_registry.create();
            return { { preferred.index, slot.generation }, slot.handle };
        }

        // Slot in use: fall back to a fresh id. Cross-region references that pointed at `preferred` will dangle - that's the caller's problem
        const entity_id fresh = alloc_slot();
        return { fresh, m_slots[fresh.index].handle };
    }


    entity_id ecs_world::entity_id_for(entt::entity entity) const noexcept {

        for (u32 i = 0; i < m_slots.size(); ++i)
            if (m_slots[i].handle == entity)
                return { i, m_slots[i].generation };
        return INVALID_ENTITY;
    }

    // streaming -------------------------------------------------------------------------------------------------------

    bool ecs_world::should_be_active(const GLT::asset::region::region& region) const noexcept {

        if (region.flags & 0x01)            // TODO: replace with GLT::asset::region::flag::always_loaded
            return true;                    // always_loaded

        auto bounds = region.bounds;
        bounds.min -= glm::vec3(m_streaming_radius);
        bounds.max += glm::vec3(m_streaming_radius);
        return bounds.contains(m_streaming_anchor);
    }


    void ecs_world::stream_pass() {

        // Pass 1: activate everything that should be hot. Loading is the slow half, so getting it in first means pass
        // 2 never tears down a region whose neighbors aren't ready yet
        for (auto& region : m_regions)
            if (should_be_active(region) && !region.is_active)
                activate(region);

        // Pass 2: deactivate the ones that shouldn't be. Entities in the just-activated regions are already live, 
        // so anything moving across a boundary has a valid destination by the time we release the source
        for (auto& region : m_regions)
            if (!should_be_active(region) && region.is_active)
                deactivate(region);
    }


    void ecs_world::activate(GLT::asset::region::region& region) {

        auto* registry = GLT::asset::registry::get_ptr();

        // We treat region.asset as already resolved (see notes at bottom)
        VALIDATE(region.asset != INVALID_HANDLE, region.is_active = true; return, 
            "", "region asset handle is unresolved; skipping entity load")

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

        // The entity blob stores parent pointers only. Rebuild the children lists so queries and the editor's outliner see a consistent tree
        // Cheap: O(loaded entities) per activation, only when the region actually contains hierarchy components
        if (!state.entities.empty()) {

            for (const entity_id child : state.entities) {

                auto* hierarchy = m_registry.try_get<component::hierarchy>(entt_of(child));
                if (!hierarchy || !hierarchy->parent.is_valid())
                    continue;

                if (auto* parent = hierarchy_of(hierarchy->parent)) {
                    if (std::find(parent->children.begin(), parent->children.end(), child) == parent->children.end())
                        parent->children.push_back(child);
                }
            }
        }

        region.is_active = true;
    }


    void ecs_world::deactivate(GLT::asset::region::region& region) {

        auto& state = m_region_states[region.id];
        for (const entity_id id : state.entities)
            release_slot(id);
        state.entities.clear();
        region.is_active = false;
    }


    GLT::asset::region::region* ecs_world::find_region(GLT::UUID id) noexcept {

        for (auto& region : m_regions)
            if (region.id == id)
                return &region;
        return nullptr;
    }


    component::hierarchy* ecs_world::hierarchy_of(entity_id id) noexcept {

        const entt::entity e = entt_of(id);
        if (e == entt::null)
            return nullptr;
        return m_registry.try_get<component::hierarchy>(e);
    }


    const component::hierarchy* ecs_world::hierarchy_of(entity_id id) const noexcept {

        const entt::entity e = entt_of(id);
        if (e == entt::null)
            return nullptr;
        return m_registry.try_get<component::hierarchy>(e);
    }


    std::expected<void, GLT::asset::load_error> ecs_world::flush_regions() {

        auto registry = GLT::asset::registry::get_ref();

        for (auto& region : m_regions) {

            if (!region.is_active)
                continue;

            if (region.asset == INVALID_HANDLE)
                continue;

            auto* region_asset = registry->data_as<GLT::asset::region::region_asset>(region.asset);
            if (!region_asset)
                continue;

            auto& state = m_region_states[region.id];

            // Translate stable entity_ids to transient entt::entity handles. Skip any that already died 
            // (despawned without going through the region's entity list - shouldn't happen, but the check is cheap)
            std::vector<entt::entity> entities;
            entities.reserve(state.entities.size());
            for (const entity_id id : state.entities) {

                const entt::entity entity = entt_of(id);
                if (entity != entt::null)
                    entities.push_back(entity);
            }

            region_asset->entity_codec = CODEC_ENTT_V1;
            m_codec.save(m_registry, entities, [this](entt::entity entity) { return entity_id_for(entity); }, region_asset->entity_data);

            if (auto result = registry->save(region.asset); !result)
                return result;
        }

        return {};
    }


    void ecs_world::prune_empty_regions() {

        auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return;

        std::vector<GLT::UUID> to_remove;
        to_remove.reserve(m_regions.size());

        for (const auto& region : m_regions) {

            // Core region: kept unconditionally so the world always has at least one region and always has a spawn
            // target at the origin
            if (region.flags & 0x01)
                continue;

            // Inactive regions may still hold entities on disk that weren't loaded into the slot table this session
            // Skip them - saving will re-emit their old data, and the next session's activation will reload them
            if (!region.is_active)
                continue;

            // state.entities was just refreshed by reassign_all_entities(), so an empty list here means no live entity
            // is currently in this cell
            const auto it = m_region_states.find(region.id);
            if (it != m_region_states.end() && !it->second.entities.empty())
                continue;

            to_remove.push_back(region.id);
        }

        // Don't let the world end up with zero regions. Drop the last candidate so one empty region survives; the core
        // region usually covers this, but hand-edited worlds might not have one
        if (!to_remove.empty() && to_remove.size() == m_regions.size())
            to_remove.pop_back();

        if (to_remove.empty())
            return;

        for (GLT::UUID id : to_remove) {

            const auto it = std::find_if(m_regions.begin(), m_regions.end(),
                [id](const GLT::asset::region::region& r) { return r.id == id; });

            if (it == m_regions.end())
                continue;

            // Use the registry's canonical path rather than recomputing from the cell. If the world was ever Save-As'd,
            // older regions may live under a different stem than the current world file
            if (it->asset != INVALID_HANDLE) {

                const std::filesystem::path region_path = registry->info(it->asset).virtual_path;

                if (!region_path.empty()) {

                    std::error_code ec{};
                    std::filesystem::remove(PROJECT_CONTENT_DIR / region_path, ec);
                    if (ec)
                        LOG(warn, "failed to delete region file [{}]: {}", region_path.generic_string(), ec.message());
                }

                registry->release(it->asset);
            }

            m_region_states.erase(id);
            m_regions.erase(it);
        }

        LOG(info, "pruned {} empty region(s)", to_remove.size());
    }


    glm::mat4 ecs_world::world_transform_of(entity_id id) const noexcept {

        const entt::entity entity = entt_of(id);
        if (entity == entt::null)
            return glm::mat4(1.0f);

        const auto* transform = m_registry.try_get<component::transform>(entity);
        glm::mat4 matrix = transform ? compose_transform(*transform) : glm::mat4(1.0f);

        // Walk up the hierarchy. [no_inherit_transform] cuts the chain
        if (m_registry.all_of<component::no_inherit_transform>(entity))
            return matrix;

        for (entity_id parent = parent_of(id); parent.is_valid(); parent = parent_of(parent)) {

            const entt::entity parent_entity = entt_of(parent);
            if (parent_entity == entt::null)
                break;

            const auto* parent_transform = m_registry.try_get<component::transform>(parent_entity);
            if (parent_transform)
                matrix = compose_transform(*parent_transform) * matrix;
        }
        return matrix;
    }


    void ecs_world::sync_world_asset() {

        auto registry = GLT::asset::registry::get_ref();
        auto* world_asset = registry->data_as<GLT::asset::world::world_asset>(m_asset.get());
        if (!world_asset)
            return;

        world_asset->region_index = m_regions;
        world_asset->region_cell_size = m_region_cell_size;
        world_asset->streaming_radius = m_streaming_radius;
        world_asset->active_camera_index = m_active_camera.index;
        world_asset->active_camera_generation = m_active_camera.generation;
    }


    std::filesystem::path ecs_world::region_path_for_cell(const glm::ivec3& cell) const {

        auto registry = GLT::asset::registry::get_ref();
        if (!registry || !m_asset.valid())
            return {};

        const std::filesystem::path world_path = registry->info(m_asset.get()).virtual_path;
        if (world_path.empty())
            return {};

        const std::string stem = world_path.stem().string();
        const std::string base = stem + "_r_" + std::to_string(cell.x) + "_" + std::to_string(cell.y) + "_" + std::to_string(cell.z);
        return world_path.parent_path() / (base + ".glt_region");
    }


    GLT::asset::handle ecs_world::resolve_region_handle(const glm::ivec3& cell, GLT::UUID expected_id) {

        auto* registry = GLT::asset::registry::get_ptr();
        if (!registry || !m_asset.valid())
            return INVALID_HANDLE;

        // load()/register_runtime() now hand back an owning asset_ref (their own +1 refcount). This function's contract,
        // however, is: return a raw handle with +1 refcount owned by the caller (who releases it later, e.g. unload_world)
        //
        // Bridge the two by bumping the refcount once more before the asset_ref dies:
        //     ref_count entering hand_off:  N   (asset_ref's own +1)
        //     retain(h):                    N+1 (caller's +1)
        //     ref destructs at end of fn:   N
        // Net: exactly +1 for the caller, regardless of N.
        auto hand_off = [registry](GLT::asset::asset_ref ref) -> GLT::asset::handle {
            if (!ref.valid())
                return INVALID_HANDLE;
            const GLT::asset::handle h = ref.get();
            registry->retain(h);           // +1 owned by caller
            return h;
        };                                 // ref destructs → releases its own +1

        // already loaded, by UUID -------------------------------------------------------------------------------------

        if (expected_id != 0) {
            if (auto h = registry->find_by_id(expected_id); h != INVALID_HANDLE) {

                // Guard: verify the UUID actually names a region. A hand-edited world file could point at a mesh's UUID,
                // and adopting that as a region would corrupt the plugin's state
                const auto& inf = registry->info(h);
                if (inf.asset_type == GLT::asset::core_types::region) {
                    // find_by_id returns a borrowed handle → bump once for the caller
                    registry->retain(h);
                    return h;
                }

                LOG(warn, "UUID {:#x} is a {} asset, not a region - refusing to adopt",
                    static_cast<unsigned long long>(expected_id), GLT::asset::type_to_string(inf.asset_type));
            }
        }

        // path-based lookup / load ------------------------------------------------------------------------------------

        const std::filesystem::path path = region_path_for_cell(cell);
        if (path.empty())
            return INVALID_HANDLE;

        if (auto h = registry->find(path); h != INVALID_HANDLE) {
            // find() returns a borrowed handle → bump once for the caller
            registry->retain(h);
            return h;
        }

        std::error_code ec{};
        if (GLT::vfs::exists(PROJECT_CONTENT_DIR / path, ec) && !ec) {

            if (auto loaded = registry->load(path); loaded) {
                // `loaded` is owning (ref_count == 1 owned by the asset_ref inside the expected). hand_off adds +1 for the
                // caller; the asset_ref's +1 is released when it destructs at the end of hand_off
                return hand_off(std::move(*loaded));
            }

            LOG(warn, "region file [{}] exists but failed to load - falling back to a fresh region",
                path.generic_string());
        }

        // mint a fresh region -----------------------------------------------------------------------------------------

        auto region_asset = GLT::create_unique_ref<GLT::asset::region::region_asset>();
        region_asset->asset_type = GLT::asset::core_types::region;
        region_asset->bounds = cell_bounds(cell);

        const std::string stem = path.stem().string();
        auto handle_res = registry->register_runtime(std::move(region_asset), path, stem, expected_id);
        VALIDATE(handle_res, return INVALID_HANDLE, "", "failed to register region [{}]", (path.generic_string()))

        return hand_off(std::move(*handle_res));
    }

    // region grid -----------------------------------------------------------------------------------------------------

    glm::ivec3 ecs_world::cell_coord_of(const glm::vec3& world_pos) const noexcept {

        const glm::vec3 half = m_region_cell_size * 0.5f;
        const glm::vec3 cell = (world_pos + half) / m_region_cell_size;

        return glm::ivec3{
            static_cast<int>(std::floor(cell.x)),
            static_cast<int>(std::floor(cell.y)),
            static_cast<int>(std::floor(cell.z)),
        };
    }


    GLT::AABB ecs_world::cell_bounds(const glm::ivec3& cell) const noexcept {

        GLT::AABB bounds{};
        const glm::vec3 half = m_region_cell_size * 0.5f;
        bounds.min = glm::vec3(cell) * m_region_cell_size - half;
        bounds.max = bounds.min + m_region_cell_size;
        return bounds;
    }


    GLT::UUID ecs_world::ensure_region_for_cell(const glm::ivec3& cell) {

        // Already have an in-memory entry for this cell?
        for (const auto& region : m_regions) {
            const glm::ivec3 region_cell = cell_coord_of((region.bounds.min + region.bounds.max) * 0.5f);
            if (region_cell == cell)
                return region.id;
        }

        auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return {};

        const GLT::asset::handle handle = resolve_region_handle(cell, 0);
        if (handle == INVALID_HANDLE)
            return {};

        GLT::asset::region::region region{};
        region.id = registry->info(handle).id;
        region.asset = handle;
        region.bounds = cell_bounds(cell);
        region.is_active = true;
        region.flags = (cell.x == 0 && cell.y == 0 && cell.z == 0) ? 0x01 : 0; 

        m_regions.push_back(region);
        m_region_states[region.id] = {};

        return region.id;
    }


    void ecs_world::assign_entity_to_cell(entity_id id) {

        if (!alive(id))
            return;

        const glm::vec3 wp = glm::vec3(world_transform_of(id)[3]);
        const glm::ivec3 cell = cell_coord_of(wp);
        const GLT::UUID target = ensure_region_for_cell(cell);
        if (target == 0)
            return;

        // Where is this entity right now?
        GLT::UUID current{};
        for (auto& [rid, state] : m_region_states) {
            if (std::find(state.entities.begin(), state.entities.end(), id) != state.entities.end()) {
                current = rid;
                break;
            }
        }

        if (current == target)
            return;

        if (current != 0) {
            if (auto it = m_region_states.find(current); it != m_region_states.end()) {
                auto& v = it->second.entities;
                v.erase(std::remove(v.begin(), v.end(), id), v.end());
            }
        }

        m_region_states[target].entities.push_back(id);
    }

}
