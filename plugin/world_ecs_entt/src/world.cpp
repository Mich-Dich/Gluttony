
#include "util/pch.h"
#include "world.h"

#include <asset/world.h>
#include <asset/i_asset_registry.h>

#include "components.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // Serializes an asset handle as its registry virtual path, length-prefixed. A zero-length path encodes INVALID_HANDLE.
    //
    //   [path_length : u32][path_bytes : path_length bytes]
    void write_asset_handle(const GLT::ref<GLT::asset::i_asset_registry_plugin>& asset_registry, GLT::asset::handle handle, 
        std::vector<std::byte>& out_bytes);

    // Inverse of write_asset_handle. `offset` is advanced past the consumed bytes. A missing, truncated, or unresolvable
    // asset degrades to INVALID_HANDLE rather than aborting the whole entity load - the outliner still shows the component,
    // the renderer just skips it.
    GLT::asset::handle read_asset_handle(const GLT::ref<GLT::asset::i_asset_registry_plugin>& asset_registry, 
        std::span<const std::byte> in_bytes, size_t& offset);

    // position/rotation(euler,rad)/scale -> mat4. YXZ order, which matches what camera/controller use elsewhere in the engine.
    static glm::mat4 compose_transform(const GLT::world::world_ecs_entt::component::transform& t);

    // Rotation-only matrix from euler angles, matching compose_transform's YXZ order. Used by translate_local to rotate
    // a local-frame delta into the entity's frame without going through a full compose().
    static glm::mat3 rotation_matrix_from_euler(const glm::vec3& euler_rad);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    void write_asset_handle(const GLT::ref<GLT::asset::i_asset_registry_plugin>& asset_registry, GLT::asset::handle handle, 
        std::vector<std::byte>& out_bytes) {

        std::string asset_path;

        if (handle != INVALID_HANDLE && asset_registry)
            asset_path = asset_registry->info(handle).virtual_path.generic_string();

        const u32 path_length = static_cast<u32>(asset_path.size());
        const auto* length_bytes = reinterpret_cast<const std::byte*>(&path_length);
        out_bytes.insert(out_bytes.end(), length_bytes, length_bytes + sizeof(path_length));

        if (path_length != 0) {
            const auto* path_bytes = reinterpret_cast<const std::byte*>(asset_path.data());
            out_bytes.insert(out_bytes.end(), path_bytes, path_bytes + path_length);
        }
    }


    GLT::asset::handle read_asset_handle(const GLT::ref<GLT::asset::i_asset_registry_plugin>& asset_registry, 
        std::span<const std::byte> in_bytes, size_t& offset) {

        if (in_bytes.size() < offset + sizeof(u32))
            return INVALID_HANDLE;

        u32 path_length = 0;
        std::memcpy(&path_length, in_bytes.data() + offset, sizeof(path_length));
        offset += sizeof(path_length);

        if (path_length == 0)
            return INVALID_HANDLE;

        if (in_bytes.size() < offset + path_length)
            return INVALID_HANDLE;

        const std::filesystem::path asset_path{
            std::string(reinterpret_cast<const char*>(in_bytes.data() + offset), path_length)
        };
        offset += path_length;

        if (!asset_registry)
            return INVALID_HANDLE;

        // Fast path - the asset is already resident.
        if (const auto existing_handle = asset_registry->find(asset_path); existing_handle != INVALID_HANDLE)
            return existing_handle;

        // Slow path - trigger a load. Failure is non-fatal.
        if (auto load_result = asset_registry->load(asset_path); load_result)
            return *load_result;

        LOG(warn, "failed to resolve asset [{}] while loading a component", asset_path.generic_string());
        return INVALID_HANDLE;
    }


    static glm::mat4 compose_transform(const GLT::world::world_ecs_entt::component::transform& transform) {

        glm::mat4 matrix = glm::translate(glm::mat4(1.0f), transform.position);
        matrix = glm::rotate(matrix, transform.rotation.y, glm::vec3(0, 1, 0));
        matrix = glm::rotate(matrix, transform.rotation.x, glm::vec3(1, 0, 0));
        matrix = glm::rotate(matrix, transform.rotation.z, glm::vec3(0, 0, 1));
        matrix = glm::scale(matrix, transform.scale);
        return matrix;
    }


    static glm::mat3 rotation_matrix_from_euler(const glm::vec3& euler_rad) {

        glm::mat4 r = glm::rotate(glm::mat4(1.0f), euler_rad.y, glm::vec3(0, 1, 0));
        r = glm::rotate(r, euler_rad.x, glm::vec3(1, 0, 0));
        r = glm::rotate(r, euler_rad.z, glm::vec3(0, 0, 1));
        return glm::mat3(r);
    }

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

        m_codec.register_component<component::transform>("glt.transform");
        m_codec.register_component<component::no_inherit_transform>("glt.no_inherit_transform");
        m_codec.register_component<component::camera>("glt.camera");

        // custom components -------------------------------------------------------------------------------------------

        m_codec.register_custom_component<component::mesh>("glt.mesh_renderer",

            // Layout:
            //   [mesh_path_length     : u32][mesh_path     : bytes]   // length == 0 ⇒ INVALID_HANDLE
            //   [material_path_length : u32][material_path : bytes]   // length == 0 ⇒ INVALID_HANDLE
            //   [visible              : u8]

            [](const entt::registry& entity_registry, entt::entity entity_handle, std::vector<std::byte>& out_bytes) {

                const auto& mesh_component = entity_registry.get<component::mesh>(entity_handle);
                auto asset_registry = GLT::asset::registry::get_ref();

                write_asset_handle(asset_registry, mesh_component.mesh, out_bytes);
                write_asset_handle(asset_registry, mesh_component.material_override, out_bytes);

                out_bytes.push_back(mesh_component.visible ? std::byte{ 1 } : std::byte{ 0 });
            },

            [](entt::registry& entity_registry, entt::entity entity_handle, std::span<const std::byte> in_bytes) {

                auto asset_registry = GLT::asset::registry::get_ref();
                auto& mesh_component = entity_registry.emplace_or_replace<component::mesh>(entity_handle);

                size_t offset = 0;

                mesh_component.mesh = GLT::asset::asset_ref{ read_asset_handle(asset_registry, in_bytes, offset) };
                mesh_component.material_override = GLT::asset::asset_ref{ read_asset_handle(asset_registry, in_bytes, offset) };

                // Trailing byte, optional for forward compatibility
                mesh_component.visible = (in_bytes.size() > offset) ? (in_bytes[offset] != std::byte{ 0 }) : true;
            });


        m_codec.register_custom_component<component::audio_source>("glt.audio_source",

            // Layout:
            //   [clip_path_length : u32][clip_path : bytes]    // length == 0 ⇒ INVALID_HANDLE
            //   [source_config    : raw bytes]                 // memcpy of GLT::asset::audio::source_config
            //   [autoplay         : u8]

            [](const entt::registry& entity_registry, entt::entity entity_handle, std::vector<std::byte>& out_bytes) {  // save

                const auto& audio_component = entity_registry.get<component::audio_source>(entity_handle);
                auto asset_registry = GLT::asset::registry::get_ref();

                write_asset_handle(asset_registry, audio_component.clip, out_bytes);

                const auto* config_bytes = reinterpret_cast<const std::byte*>(&audio_component.config);
                out_bytes.insert(out_bytes.end(), config_bytes, config_bytes + sizeof(audio_component.config));

                out_bytes.push_back(audio_component.autoplay ? std::byte{ 1 } : std::byte{ 0 });
            },

            [](entt::registry& entity_registry, entt::entity entity_handle, std::span<const std::byte> in_bytes) {      // load

                auto asset_registry = GLT::asset::registry::get_ref();
                auto& audio_component = entity_registry.emplace_or_replace<component::audio_source>(entity_handle);

                size_t offset = 0;
                audio_component.clip = GLT::asset::asset_ref{ read_asset_handle(asset_registry, in_bytes, offset) };

                // Config was written in full at save time. If it's missing or truncated the blob came from a different
                // (or hand-edited) writer - leave the config at its default-constructed values rather than risk reading past the buffer.
                if (in_bytes.size() < offset + sizeof(audio_component.config))
                    return;

                std::memcpy(&audio_component.config, in_bytes.data() + offset, sizeof(audio_component.config));
                offset += sizeof(audio_component.config);

                // Trailing byte; optional for forward compatibility
                audio_component.autoplay = (in_bytes.size() > offset) ? (in_bytes[offset] != std::byte{ 0 }) : false;
            });


        // name_component and hierarchy own heap data, so they get explicit codecs.
        m_codec.register_custom_component<component::name>("glt.name",

            [](const entt::registry& r, entt::entity entity, std::vector<std::byte>& out) {

                const auto& n = r.get<component::name>(entity);
                const u32 len = static_cast<u32>(n.name.size());
                const auto* lp = reinterpret_cast<const std::byte*>(&len);
                out.insert(out.end(), lp, lp + sizeof(len));
                const auto* sp = reinterpret_cast<const std::byte*>(n.name.data());
                out.insert(out.end(), sp, sp + len);
            },

            [](entt::registry& r, entt::entity entity, std::span<const std::byte> data) {

                if (data.size() < sizeof(u32))
                    return;
                u32 len;
                std::memcpy(&len, data.data(), sizeof(len));
                if (data.size() < sizeof(u32) + len)
                    return;
                auto& n = r.emplace_or_replace<component::name>(entity);
                n.name.assign(reinterpret_cast<const char*>(data.data() + sizeof(u32)), len);
            });


        // hierarchy: parent pointer only. children lists are rebuilt after load by rebuild_hierarchy() so we don't duplicate
        // the tree in every blob
        m_codec.register_custom_component<component::hierarchy>("glt.hierarchy",

            [](const entt::registry& r, entt::entity entity, std::vector<std::byte>& out) {
                const auto& h = r.get<component::hierarchy>(entity);
                const auto* parent = reinterpret_cast<const std::byte*>(&h.parent);
                out.insert(out.end(), parent, parent + sizeof(h.parent));
            },

            [](entt::registry& r, entt::entity entity, std::span<const std::byte> data) {
                if (data.size() != sizeof(entity_id)) return;
                auto& h = r.emplace_or_replace<component::hierarchy>(entity);
                std::memcpy(&h.parent, data.data(), sizeof(entity_id));
            });


        // Editor-side registrations (the UI side). May be empty in a runtime build; components still work, they just don't show a panel
        component::register_all_component_descriptors(m_components);

        LOG_LOADED
    }


    void ecs_world_plugin::on_unload() {

        unload_world();
        m_asset_handler.on_unload();
        m_active_camera = INVALID_ENTITY;
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

        // Copy the region index into our mutable runtime vector. The asset's copy is treated as a template; we only ever mutate ours
        m_regions = world_asset->region_index;
        m_region_states.clear();
        for (const auto& region : m_regions)
            m_region_states.try_emplace(region.id);

        // Pick a default region for runtime-spawned entities. Prefer the first region that's flagged always_loaded
        // those are always active, so spawn() can safely put new entities there without checking stream state
        m_default_region_id = {};

        for (const auto& region : m_regions) {
            if (region.flags & 0x01) {

                m_default_region_id = region.id;
                break;
            }
        }
        if (m_default_region_id == 0 && !m_regions.empty())
            m_default_region_id = m_regions.front().id;

        m_active_camera = (world_asset->active_camera_index == 0xFFFFFFFFu)
            ? INVALID_ENTITY
            : entity_id{ world_asset->active_camera_index, world_asset->active_camera_generation };

        return {};
    }


    void ecs_world_plugin::unload_world() noexcept {

        for (auto& region : m_regions)
            if (region.is_active)
                deactivate(region);

        m_regions.clear();
        m_region_states.clear();
        m_world = {};
        m_default_region_id = {};
        m_registry.clear();
        m_slots.clear();
        m_free.clear();
    }


    GLT::asset::handle ecs_world_plugin::world_handle() const noexcept { return m_world; }


    std::expected<void, GLT::asset::load_error> ecs_world_plugin::save_world() {

        if (m_world == INVALID_HANDLE)
            return std::unexpected{ GLT::asset::load_error::not_found };

        auto registry = GLT::asset::registry::get_ref();

        // Regions first - if a region save fails, the world file on disk still points at the previous (consistent) region data.
        if (auto result = flush_regions(); !result)
            return result;

        sync_world_asset();
        return registry->save(m_world);
    }


    std::expected<void, GLT::asset::load_error> ecs_world_plugin::save_world_as(const std::filesystem::path& path) {

        if (path.empty())
            return std::unexpected{ GLT::asset::load_error::not_found };

        auto registry = GLT::asset::registry::get_ref();

        // fresh world: no handle yet
        if (m_world == INVALID_HANDLE) {

            // A world with no regions can't hold entities. Mint one before we register the world so the region is already
            // a live asset by the time we declare it as a dependency.
            if (m_regions.empty()) {

                const std::string region_name = path.stem().string() + "_region0";
                const std::filesystem::path region_path = path.parent_path() / (region_name + ".glt_region");

                auto region_asset = GLT::create_unique_ref<GLT::asset::region::region_asset>();
                region_asset->asset_type = GLT::asset::core_types::region;

                GLT::AABB huge{};
                huge.min = glm::vec3(-FLT_MAX);
                huge.max = glm::vec3( FLT_MAX);
                region_asset->bounds = huge;

                auto region_handle_res = registry->register_runtime(std::move(region_asset), region_path, region_name);
                if (!region_handle_res)
                    return std::unexpected{ region_handle_res.error() };

                const GLT::asset::handle region_handle = *region_handle_res;
                const GLT::UUID region_id = registry->info(region_handle).id;      // whatever register_runtime minted

                GLT::asset::region::region region{};
                region.id = region_id;
                region.asset = region_handle;
                region.bounds = huge;
                region.flags = 0x01;
                region.is_active = true;

                m_regions.push_back(region);
                m_region_states[region.id] = {};
                m_default_region_id = region.id;
            }
            auto world_asset = GLT::create_unique_ref<GLT::asset::world::world_asset>();
            world_asset->asset_type = GLT::asset::core_types::world;
            world_asset->region_index = m_regions;

            auto handle_res = registry->register_runtime(std::move(world_asset), path, path.stem().string());
            if (!handle_res)
                return std::unexpected{ handle_res.error() };

            m_world = *handle_res;

            // Serialize the region handles into the world's dependency table so reload can resolve them
            // Without this, deserialize_world leaves every region's `asset` at INVALID_HANDLE
            for (const auto& region : m_regions)
                if (region.asset != INVALID_HANDLE)
                    registry->add_dependency(m_world, region.asset);

            return save_world();
        }

        // existing handle, path differs: migrate the canonical path
        if (registry->info(m_world).virtual_path != path) {

            if (auto result = flush_regions(); !result)
                return result;

            sync_world_asset();
            return registry->save_as(m_world, path);
        }

        return save_world();
    }

    // entity lifecycle ------------------------------------------------------------------------------------------------

    entity_id ecs_world_plugin::spawn() {

        const entity_id id = alloc_slot();

        // Route into the default region so flush_regions() will pick this entity up on save
        // Orphaned entities (no default region yet) are still valid - they just won't persist
        if (m_default_region_id != GLT::UUID{}) {

            auto it = m_region_states.find(m_default_region_id);
            if (it != m_region_states.end())
                it->second.entities.push_back(id);
        }

        return id;
    }


    void ecs_world_plugin::despawn(entity_id id) noexcept { release_slot(id); }


    bool ecs_world_plugin::alive(entity_id id) const noexcept {

        if (id.index >= m_slots.size())
            return false;
        const slot& slot = m_slots[id.index];
        return slot.generation == id.generation
            && slot.handle != entt::null
            && m_registry.valid(slot.handle);
    }

    // transform -------------------------------------------------------------------------------------------------------

    glm::vec3 ecs_world_plugin::get_local_position(entity_id id) const {

        const entt::entity entity = entt_of(id);
        if (entity == entt::null)
            return glm::vec3(0.f);

        if (const auto* transform = m_registry.try_get<component::transform>(entity))
            return transform->position;

        return glm::vec3(0.f);
    }


    glm::vec3 ecs_world_plugin::get_local_rotation(entity_id id) const {

        const entt::entity entity = entt_of(id);
        if (entity == entt::null)
            return glm::vec3(0.f);

        if (const auto* transform = m_registry.try_get<component::transform>(entity))
            return transform->rotation;

        return glm::vec3(0.f);
    }


    void ecs_world_plugin::translate_local(entity_id id, const glm::vec3& local_delta) {

        if (!alive(id))
            return;

        auto& transform = m_registry.get_or_emplace<component::transform>(entt_of(id));
        transform.position += rotation_matrix_from_euler(transform.rotation) * local_delta;
    }


    void ecs_world_plugin::rotate_local(entity_id id, const glm::vec3& euler_delta) {

        if (!alive(id))
            return;

        auto& transform = m_registry.get_or_emplace<component::transform>(entt_of(id));
        transform.rotation += euler_delta;
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

        // The only engine-driven work here is streaming. Game systems run outside the plugin (the concrete world's own update path)
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

            // Orphans count as roots so a corrupted parent link doesn't make an entity unreachable in the outliner
            const component::hierarchy* h = hierarchy_of(id);
            if (!h || !h->parent.is_valid() || !alive(h->parent))
                roots.push_back(id);
        }
        return roots;
    }


    std::vector<entity_id> ecs_world_plugin::children_of(entity_id id) const {

        if (const component::hierarchy* h = hierarchy_of(id))
            return h->children;
        return {};
    }


    std::string_view ecs_world_plugin::entity_name(entity_id id) const {

        if (const auto* n = m_registry.try_get<component::name>(entt_of(id)))
            return n->name;
        return {};
    }


    bool ecs_world_plugin::has_children(entity_id id) const noexcept {

        if (const component::hierarchy* h = hierarchy_of(id))
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

        if (const component::hierarchy* h = hierarchy_of(id))
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

    // i_world_scene ---------------------------------------------------------------------------------------------------

    void ecs_world_plugin::gather_scene(std::vector<GLT::asset::mesh::instance>& out) const {

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
            inst.material_override = mesh_comp.material_override;
            inst.transform = world_transform_of(id);
            out.push_back(inst);
        }
    }


    bool ecs_world_plugin::get_camera_view(GLT::world::camera_snapshot& out) const {

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


    void ecs_world_plugin::set_active_camera(entity_id id) { m_active_camera = id; }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    entity_id ecs_world_plugin::entity_id_for(entt::entity entity) const noexcept {

        for (u32 i = 0; i < m_slots.size(); ++i)
            if (m_slots[i].handle == entity)
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

        if (slot.handle == entt::null) {                   // Slot is free: honour the file'slot id verbatim

            slot.generation = preferred.generation ? preferred.generation : 1;
            slot.handle = m_registry.create();
            return { { preferred.index, slot.generation }, slot.handle };
        }

        // Slot in use: fall back to a fresh id. Cross-region references that pointed at `preferred` will dangle - that's the caller's problem
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

                auto* h = m_registry.try_get<component::hierarchy>(entt_of(child));
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


    component::hierarchy* ecs_world_plugin::hierarchy_of(entity_id id) noexcept {

        const entt::entity e = entt_of(id);
        if (e == entt::null)
            return nullptr;
        return m_registry.try_get<component::hierarchy>(e);
    }


    const component::hierarchy* ecs_world_plugin::hierarchy_of(entity_id id) const noexcept {

        const entt::entity e = entt_of(id);
        if (e == entt::null)
            return nullptr;
        return m_registry.try_get<component::hierarchy>(e);
    }


    std::expected<void, GLT::asset::load_error> ecs_world_plugin::flush_regions() {

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


    glm::mat4 ecs_world_plugin::world_transform_of(entity_id id) const noexcept {

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


    void ecs_world_plugin::sync_world_asset() {

        auto registry = GLT::asset::registry::get_ref();
        auto* wa = registry->data_as<GLT::asset::world::world_asset>(m_world);
        if (!wa)
            return;

        wa->region_index = m_regions;
        wa->active_camera_index = m_active_camera.index;
        wa->active_camera_generation = m_active_camera.generation;
    }

}
