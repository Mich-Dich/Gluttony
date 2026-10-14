
#include "util/pch.h"
#include "world.h"

#include <glm/gtc/type_ptr.hpp>
#include <cmath>

#include <application.h>
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


    // Quaternion for the same rotation as rotation_matrix_from_euler(): Ry * Rx * Rz (intrinsic YXZ).
    // Because the quaternion product mirrors matrix composition, this is qy * qx * qz.
    static glm::quat quat_from_euler_yxz(const glm::vec3& euler_rad);


    // Inverse of quat_from_euler_yxz(). Extracts YXZ euler angles from a (unit) quaternion.
    //
    // With M = Ry * Rx * Rz, the relevant entries are:
    //     M[1][2] = -sx        M[2][0] = sy*cx        M[0][2] = sy*cx
    //     M[1][0] = cx*sz      M[2][2] = cy*cx        M[0][0] = cy*cz + sy*sx*sz
    //     M[1][1] = cx*cz
    //
    // glm::mat3 m stores column-major, so m[col][row] == M[row][col].
    static glm::vec3 euler_yxz_from_quat(const glm::quat& q);

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


    static glm::quat quat_from_euler_yxz(const glm::vec3& euler_rad) {

        const f32 hx = euler_rad.x * 0.5f;
        const f32 hy = euler_rad.y * 0.5f;
        const f32 hz = euler_rad.z * 0.5f;

        const glm::quat qx{ std::cos(hx), std::sin(hx), 0.f,          0.f };
        const glm::quat qy{ std::cos(hy), 0.f,           std::sin(hy), 0.f };
        const glm::quat qz{ std::cos(hz), 0.f,           0.f,          std::sin(hz) };

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
            // Gimbal lock: y and z become coupled. Fold everything into y and zero z.
            euler.y = std::atan2(-m[0][2], m[0][0]);
            euler.z = 0.f;
        }
        return euler;
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    ecs_world_plugin::ecs_world_plugin() = default;


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
        m_codec.register_component<component::light>("glt.light");

        // custom components -------------------------------------------------------------------------------------------

        m_codec.register_custom_component<component::mesh>("glt.mesh_renderer",

            [](const entt::registry& entity_registry, entt::entity entity_handle, std::vector<std::byte>& out_bytes) {

                const auto& mesh_component = entity_registry.get<component::mesh>(entity_handle);
                auto asset_registry = GLT::asset::registry::get_ref();

                write_asset_handle(asset_registry, mesh_component.mesh, out_bytes);

                const u32 count = static_cast<u32>(mesh_component.material_overrides.size());
                const auto* comp_bytes = reinterpret_cast<const std::byte*>(&count);
                out_bytes.insert(out_bytes.end(), comp_bytes, comp_bytes + sizeof(count));

                for (const auto& ov : mesh_component.material_overrides)
                    write_asset_handle(asset_registry, ov, out_bytes);

                out_bytes.push_back(mesh_component.visible ? std::byte{1} : std::byte{0});
            },

            [](entt::registry& entity_registry, entt::entity entity_handle, std::span<const std::byte> in_bytes) {

                auto asset_registry = GLT::asset::registry::get_ref();
                auto& mesh_component = entity_registry.emplace_or_replace<component::mesh>(entity_handle);

                size_t offset = 0;
                mesh_component.mesh = GLT::asset::asset_ref{ read_asset_handle(asset_registry, in_bytes, offset) };

                if (in_bytes.size() < offset + sizeof(u32))
                    return;
                u32 count = 0;
                std::memcpy(&count, in_bytes.data() + offset, sizeof(count));
                offset += sizeof(count);

                mesh_component.material_overrides.clear();
                mesh_component.material_overrides.reserve(count);
                for (u32 i = 0; i < count; ++i)
                    mesh_component.material_overrides.emplace_back(
                        GLT::asset::asset_ref{ read_asset_handle(asset_registry, in_bytes, offset) });

                mesh_component.visible = (in_bytes.size() > offset) ? (in_bytes[offset] != std::byte{0}) : true;

                // Defence in depth: if the asset is already resident, verify the count.
                // (If it isn't loaded yet we can't validate here — do it on first submit.)
                if (const auto* asset = asset_registry->data_as<GLT::asset::mesh::mesh_asset>(mesh_component.mesh)) {
                    if (!mesh_component.overrides_match(asset->submesh_count())) {
                        LOG(warn, "mesh component override count {} doesn't match submesh count {} - clearing",
                            mesh_component.material_overrides.size(), asset->submesh_count());
                        mesh_component.material_overrides.clear();
                    }
                }
                
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

        m_region_cell_size = world_asset->region_cell_size;                     // pick up the world's grid
        VALIDATE(m_region_cell_size.x > 0.f && m_region_cell_size.y > 0.f && m_region_cell_size.z > 0.f,
            m_region_cell_size = glm::vec3(256.f), "", "Corrupt file holds invalid region cell size, resetting to default of [256.f]");

        m_streaming_radius = world_asset->streaming_radius;
        VALIDATE(m_streaming_radius >= 0.f, m_streaming_radius = 512.f, 
            "", "corrupt file has invalid streaming radius, resetting to [512.f]");

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

                        region.asset = h;
                        registry->retain(h);
                        LOG(info, "recovered region {:#x} via UUID lookup", static_cast<unsigned long long>(region.id));
                        continue;
                    }

                    LOG(warn, "UUID {:#x} is a {} asset - skipping", static_cast<unsigned long long>(region.id), 
                        GLT::asset::type_to_string(inf.asset_type));
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

        // Region-state map keys may have shifted if any UUID was reconciled
        {
            std::unordered_map<GLT::UUID, region_state> fresh;
            fresh.reserve(m_regions.size());
            for (const auto& region : m_regions)
                fresh.try_emplace(region.id);
            m_region_states = std::move(fresh);
        }

        // World must always have at least one region
        if (m_regions.empty())
            (void)ensure_region_for_cell(glm::ivec3(0));

        m_active_camera = (world_asset->active_camera_index == 0xFFFFFFFFu) ? INVALID_ENTITY 
            : entity_id{ world_asset->active_camera_index, world_asset->active_camera_generation };

        return {};
    }


    void ecs_world_plugin::unload_world() noexcept {

        for (auto& region : m_regions)
            if (region.is_active)
                deactivate(region);

        if (auto registry = GLT::asset::registry::get_ref()) {
            for (const auto& region : m_regions)
                if (region.asset != INVALID_HANDLE)
                    registry->release(region.asset);
        }

        m_regions.clear();
        m_region_states.clear();
        m_world = {};
        m_registry.clear();
        m_slots.clear();
        m_free.clear();
    }


    GLT::asset::handle ecs_world_plugin::world_handle() const noexcept { return m_world; }


    std::expected<void, GLT::asset::load_error> ecs_world_plugin::save_world() {

        if (m_world == INVALID_HANDLE)
            return std::unexpected{ GLT::asset::load_error::not_found };

        auto registry = GLT::asset::registry::get_ref();

        reassign_all_entities();
        prune_empty_regions(); 
        if (auto result = flush_regions(); !result)
            return result;

        sync_world_asset();
        return registry->save(m_world);
    }


    std::expected<void, GLT::asset::load_error> ecs_world_plugin::save_world_as(const std::filesystem::path& path) {

        if (path.empty())
            return std::unexpected{ GLT::asset::load_error::not_found };

        auto registry = GLT::asset::registry::get_ref();

        if (m_world == INVALID_HANDLE) {    // fresh world -------------------------------------------------------------

            // Register the world first so ensure_region_for_cell() has a path to anchor against
            auto world_asset = GLT::create_unique_ref<GLT::asset::world::world_asset>();
            world_asset->asset_type = GLT::asset::core_types::world;
            world_asset->region_cell_size = m_region_cell_size;

            auto handle_res = registry->register_runtime(std::move(world_asset), path, path.stem().string());
            if (!handle_res)
                return std::unexpected{ handle_res.error() };

            m_world = *handle_res;

            // Guarantee the core region exists before anything tries to spawn into it
            if (m_regions.empty())
                (void)ensure_region_for_cell(glm::ivec3(0));

            return save_world();
        }

        // existing handle, path differs: migrate the canonical path
        if (registry->info(m_world).virtual_path != path) {

            reassign_all_entities();
            prune_empty_regions();
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

        // Give the entity a transform up front so its position is well-defined for cell assignment
        // Safe: emplace is idempotent if a descriptor already added one.
        m_registry.emplace_or_replace<component::transform>(entt_of(id));
        assign_entity_to_cell(id);

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

        // World position changed -> which cell is this entity in now?
        assign_entity_to_cell(id);
    }


    void ecs_world_plugin::rotate_local(entity_id id, const glm::vec3& euler_delta) {

        if (!alive(id))
            return;

        auto& transform = m_registry.get_or_emplace<component::transform>(entt_of(id));
        transform.rotation += euler_delta;
        // Rotation doesn't move the pivot; no re-bin needed
    }


    glm::quat ecs_world_plugin::get_local_rotation_q(entity_id id) const {

        const entt::entity entity = entt_of(id);
        if (entity == entt::null)
            return glm::quat{ 1.f, 0.f, 0.f, 0.f };    // identity

        if (const auto* transform = m_registry.try_get<component::transform>(entity))
            return quat_from_euler_yxz(transform->rotation);

        return glm::quat{ 1.f, 0.f, 0.f, 0.f };
    }


    void ecs_world_plugin::set_local_rotation_q(entity_id id, const glm::quat& rotation) {

        if (!alive(id))
            return;

        auto& transform = m_registry.get_or_emplace<component::transform>(entt_of(id));
        transform.rotation = euler_yxz_from_quat(rotation);

        // Rotation doesn't move the pivot - no re-bin needed (same rationale as rotate_local)
    }


    glm::vec3 ecs_world_plugin::get_local_scale(entity_id id) const {

        const entt::entity entity = entt_of(id);
        if (entity == entt::null)
            return glm::vec3(1.f);    // identity

        if (const auto* transform = m_registry.try_get<component::transform>(entity))
            return transform->scale;

        return glm::vec3(1.f);
    }


    void ecs_world_plugin::set_local_scale(entity_id id, const glm::vec3& scale) {

        if (!alive(id))
            return;

        auto& transform = m_registry.get_or_emplace<component::transform>(entt_of(id));
        transform.scale = scale;

        // Scale doesn't move the pivot - no re-bin needed
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

    // region grid ------------------------------------------------------------------------------------

    glm::vec3 ecs_world_plugin::get_region_cell_size() const noexcept { return m_region_cell_size; }


    void ecs_world_plugin::set_region_cell_size(const glm::vec3& size) {

        if (size.x <= 0.f || size.y <= 0.f || size.z <= 0.f) {
            LOG(warn, "set_region_cell_size ignored: non-positive component ({}, {}, {})", size.x, size.y, size.z);
            return;
        }

        m_region_cell_size = size;
        reassign_all_entities();
    }


    void ecs_world_plugin::reassign_all_entities() {

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


    void ecs_world_plugin::gather_entity_positions(std::vector<GLT::world::entity_position>& out) const {

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
            inst.material_overrides.assign(mesh_comp.material_overrides.begin(), mesh_comp.material_overrides.end());
            inst.transform = world_transform_of(id);
            out.push_back(inst);
        }
    }


    void ecs_world_plugin::gather_lights(std::vector<GLT::world::light_snapshot>& out) const {

        out.clear();
        auto view = m_registry.view<const component::light, const component::transform>();   // transform is required, so view constrains both
        out.reserve(view.size_hint());

        for (auto entity : view) {

            const auto& light = view.get<const component::light>(entity);
            const entity_id id = id_of(entity);
            if (!id.is_valid())
                continue;

            const glm::mat4 world = world_transform_of(id);

            GLT::world::light_snapshot s{};
            s.position = glm::vec3(world[3]);                                   // translation column
            s.direction = -glm::normalize(glm::vec3(world[2]));                  // -Z is forward, engine convention
            s.color = light.color;
            s.intensity = light.intensity;
            s.radius = light.radius;
            s.inner_cone_angle = light.inner_cone_angle;
            s.outer_cone_angle = light.outer_cone_angle;
            s.type = light.type;

            out.push_back(s);
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

        if (region.flags & 0x01)            // TODO: replace with GLT::asset::region::flag::always_loaded
            return true;                    // always_loaded

        auto bounds = region.bounds;
        bounds.min -= glm::vec3(m_streaming_radius);
        bounds.max += glm::vec3(m_streaming_radius);
        return bounds.contains(m_streaming_anchor);
    }


    void ecs_world_plugin::stream_pass() {

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


    void ecs_world_plugin::prune_empty_regions() {

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
        auto* world_asset = registry->data_as<GLT::asset::world::world_asset>(m_world);
        if (!world_asset)
            return;

        world_asset->region_index = m_regions;
        world_asset->region_cell_size = m_region_cell_size;
        world_asset->streaming_radius = m_streaming_radius;
        world_asset->active_camera_index = m_active_camera.index;
        world_asset->active_camera_generation = m_active_camera.generation;
    }


    std::filesystem::path ecs_world_plugin::region_path_for_cell(const glm::ivec3& cell) const {

        auto registry = GLT::asset::registry::get_ref();
        if (!registry || m_world == INVALID_HANDLE)
            return {};

        const std::filesystem::path world_path = registry->info(m_world).virtual_path;
        if (world_path.empty())
            return {};

        const std::string stem = world_path.stem().string();
        const std::string base = stem + "_r_" + std::to_string(cell.x) + "_" + std::to_string(cell.y) + "_" + std::to_string(cell.z);
        return world_path.parent_path() / (base + ".glt_region");
    }


    GLT::asset::handle ecs_world_plugin::resolve_region_handle(const glm::ivec3& cell, GLT::UUID expected_id) {

        auto registry = GLT::asset::registry::get_ref();
        if (!registry || m_world == INVALID_HANDLE)
            return INVALID_HANDLE;

        // already loaded, by UUID -------------------------------------------------------------------------------------

        if (expected_id != 0) {
            if (auto h = registry->find_by_id(expected_id); h != INVALID_HANDLE) {

                // Guard: verify the UUID actually names a region. A hand-edited
                // world file could point at a mesh's UUID, and adopting that as
                // a region would corrupt the plugin's state.
                const auto& inf = registry->info(h);
                if (inf.asset_type == GLT::asset::core_types::region) {
                    registry->retain(h);
                    return h;
                }

                LOG(warn, "UUID {:#x} is a {} asset, not a region - refusing to adopt",
                    static_cast<unsigned long long>(expected_id),
                    GLT::asset::type_to_string(inf.asset_type));
            }
        }

        // ath-based lookup / load -------------------------------------------------------------------------------------

        const std::filesystem::path path = region_path_for_cell(cell);
        if (path.empty())
            return INVALID_HANDLE;

        if (auto h = registry->find(path); h != INVALID_HANDLE) {
            registry->retain(h);
            return h;
        }

        std::error_code ec{};
        if (GLT::vfs::exists(PROJECT_CONTENT_DIR / path, ec) && !ec) {

            if (auto loaded = registry->load(path); loaded) {
                registry->retain(*loaded);
                return *loaded;
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
        if (!handle_res) {
            LOG(warn, "failed to register region [{}]", path.generic_string());
            return INVALID_HANDLE;
        }

        registry->retain(*handle_res);
        return *handle_res;
    }

    // region grid -----------------------------------------------------------------------------------------------------

    glm::ivec3 ecs_world_plugin::cell_coord_of(const glm::vec3& world_pos) const noexcept {

        const glm::vec3 half = m_region_cell_size * 0.5f;
        const glm::vec3 cell = (world_pos + half) / m_region_cell_size;

        return glm::ivec3{
            static_cast<int>(std::floor(cell.x)),
            static_cast<int>(std::floor(cell.y)),
            static_cast<int>(std::floor(cell.z)),
        };
    }


    GLT::AABB ecs_world_plugin::cell_bounds(const glm::ivec3& cell) const noexcept {

        GLT::AABB bounds{};
        const glm::vec3 half = m_region_cell_size * 0.5f;
        bounds.min = glm::vec3(cell) * m_region_cell_size - half;
        bounds.max = bounds.min + m_region_cell_size;
        return bounds;
    }


    GLT::UUID ecs_world_plugin::ensure_region_for_cell(const glm::ivec3& cell) {

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


    void ecs_world_plugin::assign_entity_to_cell(entity_id id) {

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
