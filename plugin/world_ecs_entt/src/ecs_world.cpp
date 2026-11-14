#include "util/pch.h"
#include <debug/trace.h>
#include "ecs_world.h"
#include "ecs_world_plugin.h"

#include <glm/gtc/type_ptr.hpp>
#include <cmath>

#include <application.h>
#include <asset/world.h>
#include <asset/i_asset_registry.h>
#include <world/entity_preset.h> 



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {

    // CONSTANTS =======================================================================================================

    // Minimum scale magnitude any shape component can reach. Chosen safely above Jolt's internal
    // IsValidScale epsilon (which is typically 1.0e-4 or 1.0e-6 depending on the build). Going below
    // this in a ScaledShape leads to asserts in SphereShape / CapsuleShape / CylinderShape
    static constexpr f32                            MIN_SCALE = 1.0e-3f;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    static glm::mat4 compose_transform(const component::transform& t);


    static glm::mat3 rotation_matrix_from_euler(const glm::vec3& euler_rad);


    static glm::quat quat_from_euler_yxz(const glm::vec3& euler_rad);


    static glm::vec3 euler_yxz_from_quat(const glm::quat& q);


    bool is_identity_scale(const glm::vec3& s);


    bool is_uniform_scale(const glm::vec3& s);

    // --- convex-hull approximations for curved primitives under non-uniform scale -------------------------------------

    // Jolt's SphereShape / CapsuleShape / CylinderShape reject non-uniform scale in IsValidScale, and
    // ScaledShape forwards the scale straight through to the inner shape's GetWorldSpaceBounds. So
    // ScaledShape{SphereShape} under (2,1,1) asserts at body creation, every time, no matter how the
    // scale is normalised. The only way to get anisotropic geometry from a curved primitive is to
    // replace it with a convex hull sampled from the transformed surface

    static void append_ellipsoid_points(std::vector<glm::vec3>& out, f32 radius, const glm::vec3& scale);

    // Capsule is aligned along Y in Jolt: cylinder from -half_height to +half_height, with hemispherical caps
    static void append_capsule_points(std::vector<glm::vec3>& out, f32 radius, f32 half_height, const glm::vec3& scale);

    // Cylinder is aligned along Y in Jolt
    static void append_cylinder_points(std::vector<glm::vec3>& out, f32 radius, f32 half_height, const glm::vec3& scale);

    // --- top-level per-collider scale application ------------------------------------------------------

    // Called once per collider from create_physics_body_for. Curved primitives under non-uniform scale
    // become convex hulls; everything else bakes or wraps as appropriate. `hull_points` is an out-param
    // the caller must keep alive until physics->create_shape() returns — that's where the shape_data's
    // span<const vec3> points into
    GLT::physics::shape_data apply_collider_scale(GLT::physics::shape_data d, const glm::vec3& s, 
        std::vector<glm::vec3>& hull_points);

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // position/rotation(euler,rad)/scale -> mat4. YXZ order: T * Ry * Rx * Rz * S
    // Built in closed form (3 sin/cos pairs, 27 multiplies) instead of three glm::rotate calls: identical result (within
    // 1e-7), 2x faster optimised and far faster at -O0, where every glm vector operator is a real function call
    static glm::mat4 compose_transform(const component::transform& t) {
        const f32 cx = std::cos(t.rotation.x), sx = std::sin(t.rotation.x);
        const f32 cy = std::cos(t.rotation.y), sy = std::sin(t.rotation.y);
        const f32 cz = std::cos(t.rotation.z), sz = std::sin(t.rotation.z);
        const glm::vec3& s = t.scale;
        glm::mat4 m;
        m[0] = glm::vec4((cy * cz + sy * sx * sz) * s.x, (cx * sz) * s.x, (cy * sx * sz - sy * cz) * s.x, 0.f);
        m[1] = glm::vec4((sy * sx * cz - cy * sz) * s.y, (cx * cz) * s.y, (sy * sz + cy * sx * cz) * s.y, 0.f);
        m[2] = glm::vec4((sy * cx) * s.z, -sx * s.z, (cy * cx) * s.z, 0.f);
        m[3] = glm::vec4(t.position, 1.f);
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
            euler.y = std::atan2(m[2][0], m[2][2]);
            euler.z = std::atan2(m[0][1], m[1][1]);
        } else {
            euler.y = std::atan2(-m[0][2], m[0][0]);  // gimbal-lock branch is correct
            euler.z = 0.f;
        }
        return euler;
    }

    // --- scale predicates -------------------------------------------------------------------------------------------

    bool is_identity_scale(const glm::vec3& s) { return glm::all(glm::lessThanEqual(glm::abs(s - glm::vec3(1.f)), glm::vec3(1e-4f))); }


    bool is_uniform_scale(const glm::vec3& s) { return std::abs(s.x - s.y) < 1e-4f && std::abs(s.y - s.z) < 1e-4f; }

    // --- convex-hull approximations for curved primitives under non-uniform scale -------------------------------------

    static void append_ellipsoid_points(std::vector<glm::vec3>& out, f32 radius, const glm::vec3& scale) {

        constexpr u32 kSectors = 24;
        constexpr u32 kRings = 12;
        constexpr f32 kPi = 3.14159265358979323846f;

        out.reserve(out.size() + kSectors * (kRings - 1) + 6);

        for (u32 i = 1; i < kRings; ++i) {
            const f32 theta = kPi * static_cast<f32>(i) / kRings;
            const f32 st = std::sin(theta);
            const f32 ct = std::cos(theta);
            for (u32 j = 0; j < kSectors; ++j) {
                const f32 phi = 2.f * kPi * static_cast<f32>(j) / kSectors;
                out.push_back({
                    radius * scale.x * st * std::cos(phi),
                    radius * scale.y * st * std::sin(phi),
                    radius * scale.z * ct
                });
            }
        }
        // Extreme points on each axis so the hull touches all six semi-axes exactly
        out.push_back({  radius * scale.x, 0.f, 0.f });
        out.push_back({ -radius * scale.x, 0.f, 0.f });
        out.push_back({ 0.f,  radius * scale.y, 0.f });
        out.push_back({ 0.f, -radius * scale.y, 0.f });
        out.push_back({ 0.f, 0.f,  radius * scale.z });
        out.push_back({ 0.f, 0.f, -radius * scale.z });
    }


    static void append_capsule_points(std::vector<glm::vec3>& out, f32 radius, f32 half_height, const glm::vec3& scale) {

        constexpr u32 kSectors = 20;
        constexpr u32 kSideRings = 6;
        constexpr u32 kCapSegments = 6;
        constexpr f32 kPi = 3.14159265358979323846f;

        // Side
        for (u32 i = 0; i <= kSideRings; ++i) {
            const f32 t = static_cast<f32>(i) / kSideRings;
            const f32 y = -half_height + 2.f * half_height * t;
            for (u32 j = 0; j < kSectors; ++j) {
                const f32 phi = 2.f * kPi * static_cast<f32>(j) / kSectors;
                out.push_back({ radius * std::cos(phi), y, radius * std::sin(phi) });
            }
        }
        // Top cap
        for (u32 i = 1; i <= kCapSegments; ++i) {
            const f32 theta = 0.5f * kPi * static_cast<f32>(i) / kCapSegments;
            const f32 st = std::sin(theta);
            const f32 ct = std::cos(theta);
            for (u32 j = 0; j < kSectors; ++j) {
                const f32 phi = 2.f * kPi * static_cast<f32>(j) / kSectors;
                out.push_back({
                    radius * ct * std::cos(phi),
                    half_height + radius * st,
                    radius * ct * std::sin(phi)
                });
            }
        }
        // Bottom cap
        for (u32 i = 1; i <= kCapSegments; ++i) {
            const f32 theta = 0.5f * kPi * static_cast<f32>(i) / kCapSegments;
            const f32 st = std::sin(theta);
            const f32 ct = std::cos(theta);
            for (u32 j = 0; j < kSectors; ++j) {
                const f32 phi = 2.f * kPi * static_cast<f32>(j) / kSectors;
                out.push_back({
                    radius * ct * std::cos(phi),
                    -half_height - radius * st,
                    radius * ct * std::sin(phi)
                });
            }
        }

        for (auto& p : out) p *= scale;
    }


    static void append_cylinder_points(std::vector<glm::vec3>& out, f32 radius, f32 half_height, const glm::vec3& scale) {

        constexpr u32 kSectors = 24;
        constexpr f32 kPi = 3.14159265358979323846f;

        // Two rims + the flat caps' centres (so the hull includes the top/bottom faces)
        for (u32 j = 0; j < kSectors; ++j) {
            const f32 phi = 2.f * kPi * static_cast<f32>(j) / kSectors;
            const f32 x = radius * std::cos(phi);
            const f32 z = radius * std::sin(phi);
            out.push_back({ x,  half_height, z });
            out.push_back({ x, -half_height, z });
        }
        out.push_back({ 0.f,  half_height, 0.f });
        out.push_back({ 0.f, -half_height, 0.f });

        for (auto& p : out) p *= scale;
    }

    // --- top-level per-collider scale application ------------------------------------------------------

    GLT::physics::shape_data apply_collider_scale(GLT::physics::shape_data d, const glm::vec3& s, 
        std::vector<glm::vec3>& hull_points) {

        if (is_identity_scale(s))
            return d;

        const glm::vec3 clamped = glm::max(glm::abs(s), glm::vec3(MIN_SCALE));
        const bool uniform = is_uniform_scale(clamped);

        return std::visit([&](auto&& v) -> GLT::physics::shape_data {
            using T = std::decay_t<decltype(v)>;

            if constexpr (std::is_same_v<T, physics::sphere>) {
                if (uniform) {
                    auto r = v;
                    r.radius *= clamped.x;
                    return r;
                }
                hull_points.clear();
                append_ellipsoid_points(hull_points, v.radius, clamped);
                return physics::convex_hull{ .points = hull_points };
            }

            else if constexpr (std::is_same_v<T, physics::capsule>) {
                // Only y-axis differs → still a valid Jolt capsule (taller/shorter), bake it
                if (std::abs(clamped.x - clamped.z) < 1e-4f) {
                    auto r = v;
                    r.radius *= clamped.x;
                    r.half_height *= clamped.y;
                    return r;
                }
                hull_points.clear();
                append_capsule_points(hull_points, v.radius, v.half_height, clamped);
                return physics::convex_hull{ .points = hull_points };
            }

            else if constexpr (std::is_same_v<T, physics::cylinder>) {
                if (std::abs(clamped.x - clamped.z) < 1e-4f) {
                    auto r = v;
                    r.radius *= clamped.x;
                    r.half_height *= clamped.y;
                    return r;
                }
                hull_points.clear();
                append_cylinder_points(hull_points, v.radius, v.half_height, clamped);
                return physics::convex_hull{ .points = hull_points };
            }

            else if constexpr (std::is_same_v<T, physics::box>) {
                auto r = v;
                r.half_extents *= clamped;
                return r;
            }

            else if constexpr (std::is_same_v<T, physics::plane>) {
                if (uniform) {
                    auto r = v;
                    r.distance *= clamped.x;
                    return r;
                }
                // Non-uniform plane scale is meaningless; keep it as-is
                return v;
            }

            else if constexpr (std::is_same_v<T, physics::height_field>) {
                auto r = v;
                r.scale *= clamped;
                return r;
            }

            else if constexpr (std::is_same_v<T, physics::convex_hull>) {
                // ConvexHullShape supports non-uniform scale via ScaledShape natively
                auto out = std::make_shared<physics::scaled>();
                out->inner = std::make_shared<GLT::physics::shape_data>(std::move(v));
                out->scale = clamped;
                return GLT::physics::shape_data{ out };
            }

            else if constexpr (std::is_same_v<T, physics::triangle_mesh>) {
                // MeshShape also supports non-uniform scale via ScaledShape
                auto out = std::make_shared<physics::scaled>();
                out->inner = std::make_shared<GLT::physics::shape_data>(std::move(v));
                out->scale = clamped;
                return GLT::physics::shape_data{ out };
            }

            else if constexpr (std::is_same_v<T, GLT::ref<const physics::compound>> ||
                            std::is_same_v<T, GLT::ref<const physics::scaled>>) {
                // Should never reach here — compounds and nested scaled are handled at the call site
                // Fail closed so a mis-routed call is loud instead of silently wrong
                ASSERT(false, "", "apply_collider_scale: compound / scaled must be handled by the caller")
                return std::move(d);
            }
        }, std::move(d));
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

        m_registry.on_construct<component::mesh>().template connect<&ecs_world::on_mesh_construct>(this);
        m_registry.on_destroy<component::physics_body>().template connect<&ecs_world::on_physics_body_destroyed>(this);

        // Render cache invalidation. Adding / removing anything that decides WHAT is rendered (or how a transform is
        // inherited) rebuilds the cache; replacing transform / light data patches just that entity. Writes through
        // component pointers do not fire signals: those go through the i_world setters or mark_changed()
        auto structure = [this]<typename T>() {
            m_registry.on_construct<T>().template connect<&ecs_world::on_render_structure_signal>(this);
            m_registry.on_destroy<T>().template connect<&ecs_world::on_render_structure_signal>(this);
        };
        structure.template operator()<component::transform>();
        structure.template operator()<component::mesh>();
        structure.template operator()<component::light>();
        structure.template operator()<component::hierarchy>();
        structure.template operator()<component::no_inherit_transform>();
        m_registry.on_update<component::mesh>().template connect<&ecs_world::on_render_structure_signal>(this);
        m_registry.on_update<component::hierarchy>().template connect<&ecs_world::on_render_structure_signal>(this);
        m_registry.on_update<component::transform>().template connect<&ecs_world::on_render_content_signal>(this);
        m_registry.on_update<component::light>().template connect<&ecs_world::on_render_content_signal>(this);
    }


    ecs_world::~ecs_world() {

        m_plugin.unregister_live_world(this);

        // Same release loop as old unload_world()
        if (auto registry = asset::registry::get_ref()) {
            for (const auto& region : m_regions)
                if (region.asset != INVALID_HANDLE)
                    registry->release(region.asset);
        }
        // m_registry / m_slots / etc. unwind via their own destructors
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
            m_region_cell_size = glm::vec3(256.f), "", "Corrupt file holds invalid region cell size")

        m_streaming_radius = world_asset->streaming_radius;
        VALIDATE(m_streaming_radius >= 0.f, m_streaming_radius = 512.f, "", "corrupt file has invalid streaming radius")
        m_streaming_anchor = world_asset->active_camera_position;

        m_regions = world_asset->region_index;
        for (auto& region : m_regions)
            region.is_active = false;
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
            VALIDATE(h != INVALID_HANDLE, continue, "", "region for cell ({},{},{}) unresolvable - skipping", (cell.x, cell.y, cell.z))

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

        // Before any region streams in, so the world entity keeps its saved id
        load_or_create_world_entity(*world_asset);

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

            // Mint a fresh world asset at `path`
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
        // Safe: emplace is idempotent if a descriptor already added one
        m_registry.emplace_or_replace<component::transform>(entt_of(id));
        assign_entity_to_cell(id);

        return id;
    }


    void ecs_world::despawn(entity_id id) noexcept {

        if (id.is_valid() && id == m_world_entity)
            return;                             // the world entity lives as long as the world

        if (alive(id))
            detach_from_parent(id);             // don't leave a dead id in the parent's children list
        release_slot(id);
    }


    bool ecs_world::alive(entity_id id) const noexcept {

        if (id.index >= m_slots.size())
            return false;
        const slot& slot = m_slots[id.index];
        return slot.generation == id.generation
            && slot.handle != entt::null
            && m_registry.valid(slot.handle);
    }


    entity_id ecs_world::spawn_preset(std::string_view preset_name, entity_id parent) {

        const auto* preset = GLT::world::preset_registry::get().find(preset_name);
        if (!preset) {
            LOG(warn, "spawn_preset: preset '{}' is not registered", preset_name);
            return INVALID_ENTITY;
        }
        return instantiate_preset_recursive(*preset, parent);
    }

    // visibility ------------------------------------------------------------------------------------------------------

    bool ecs_world::is_visible(entity_id id) const {

        const entt::entity entity = entt_of(id);
        if (entity == entt::null)
            return false;

        if (const auto* mesh_comp = m_registry.try_get<component::mesh>(entity))
            return mesh_comp->visible;

        // No mesh — nothing is being rendered, so there's nothing to hide. Matches the way gather_scene
        // treats meshless entities: they simply aren't candidates in the first place
        return true;
    }


    void ecs_world::set_visible(entity_id id, bool visible) {

        const entt::entity entity = entt_of(id);
        if (entity == entt::null)
            return;

        // No-op for non-rendering entities. Adding a mesh component just to carry the flag would make every
        // hide-toggle pay for a component install (and fire on_mesh_construct). If you want a visibility
        // flag on a bare entity, add the mesh component first
        if (auto* mesh_comp = m_registry.try_get<component::mesh>(entity); mesh_comp && mesh_comp->visible != visible) {
            mesh_comp->visible = visible;
            mark_render_structure_dirty();                  // the entity enters / leaves the instance list
        }
    }


    mobility ecs_world::get_mobility(entity_id id) const {

        if (const auto* transform = m_registry.try_get<component::transform>(entt_of(id)))
            return transform->mobility;
        return mobility::fixed;
    }


    void ecs_world::set_mobility(entity_id id, mobility value) {

        auto* transform = m_registry.try_get<component::transform>(entt_of(id));
        if (!transform || transform->mobility == value)
            return;
        transform->mobility = value;
        mark_render_structure_dirty();                      // moves the entity (and maybe its subtree) between fixed and movable
    }


    void ecs_world::mark_changed(entity_id id) {

        if (alive(id))
            m_render.dirty.push_back(id);
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
        mark_changed(id);

        // World position changed -> which cell is this entity in now?
        assign_entity_to_cell(id);
    }


    void ecs_world::rotate_local(entity_id id, const glm::vec3& euler_delta) {

        if (!alive(id))
            return;

        auto& transform = m_registry.get_or_emplace<component::transform>(entt_of(id));
        transform.rotation += euler_delta;
        mark_changed(id);
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
        mark_changed(id);

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

        auto& t = m_registry.get_or_emplace<component::transform>(entt_of(id));
        const glm::vec3 old = t.scale;
        t.scale = scale;
        mark_changed(id);

        if (m_physics && old != scale) {
            const entt::entity e = entt_of(id);
            if (const auto* pb = m_registry.try_get<component::physics_body>(e); pb && pb->body.is_valid())
                rebuild_physics_body(id);
        }
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

        sync_physics_to_transforms();
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
        // (deactivate() clears them), so this only affects what's resident right now
        for (auto& [rid, state] : m_region_states)
            state.entities.clear();

        for (u32 index = 0; index < m_slots.size(); ++index) {

            const slot& slot = m_slots[index];
            if (slot.handle == entt::null)
                continue;

            const entity_id id{ index, slot.generation };
            if (id == m_world_entity)
                continue;                       // not part of any region: saved with the world asset

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
            if (id == m_world_entity)
                continue;                       // shown separately (i_world::world_entity)

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
        if (id.is_valid() && id == m_world_entity)
            return "World";
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

        if (child == m_world_entity || (new_parent.is_valid() && new_parent == m_world_entity))
            return;   // the world entity is never part of a hierarchy

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
        mark_render_structure_dirty();                      // world transforms and inherited mobility of the subtree change
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

    // snapshots (editor undo / redo) ----------------------------------------------------------------------------------

    std::vector<std::byte> ecs_world::snapshot_entities(std::span<const entity_id> ids) const {

        std::vector<entt::entity> handles;
        std::vector<entity_id> alive_ids;
        handles.reserve(ids.size());
        alive_ids.reserve(ids.size());

        for (const entity_id id : ids) {
            if (!alive(id) || std::find(alive_ids.begin(), alive_ids.end(), id) != alive_ids.end())
                continue;
            handles.push_back(entt_of(id));
            alive_ids.push_back(id);
        }

        std::vector<std::byte> blob;
        if (handles.empty())
            return blob;

        // We already know every id, so resolve with the small local table instead of the O(slots) entity_id_for()
        m_codec.save(m_registry, std::span<const entt::entity>(handles),
            [&](entt::entity e) {
                for (size_t i = 0; i < handles.size(); ++i)
                    if (handles[i] == e)
                        return alive_ids[i];
                return INVALID_ENTITY;
            }, blob);

        return blob;
    }


    std::vector<entity_id> ecs_world::restore_entities(std::span<const std::byte> blob) {

        if (blob.empty())
            return {};

        // Links of the entities we are about to replace. Kept so an undo doesn't reshuffle the outliner order
        struct old_links {
            entity_id                               parent{ INVALID_ENTITY };
            std::vector<entity_id>                  children;
        };
        std::unordered_map<entity_id, old_links> previous;

        // The codec asks for an entity per stored id. Throw away the current version (if any) and hand out the
        // very same id again. allocate_for_load() honours the stored id because the slot was just released
        const std::vector<entity_id> restored = m_codec.load(m_registry, blob, [this, &previous](entity_id saved) {

            if (alive(saved)) {
                if (const auto* h = hierarchy_of(saved))
                    previous[saved] = { h->parent, h->children };
                release_slot(saved);                // destroys the entt entity, the physics observer frees the body
            }

            auto result = allocate_for_load(saved);
            if (result.first != saved)
                LOG(warn, "restore_entities: slot {} is used by another entity, restored as {}:{}",
                    saved.index, result.first.index, result.first.generation);
            return result;
        });

        // The codec stores only the parent link; children lists are derived. Collect the current children of every
        // restored entity (they may be restored too, or may have stayed alive the whole time)
        std::unordered_map<entity_id, std::vector<entity_id>> found;
        for (const auto [entity, h] : m_registry.view<component::hierarchy>().each()) {

            if (!h.parent.is_valid())
                continue;
            if (std::find(restored.begin(), restored.end(), h.parent) == restored.end())
                continue;

            if (const entity_id child = id_of(entity); child.is_valid())
                found[h.parent].push_back(child);
        }

        for (const entity_id id : restored) {

            const entt::entity e = entt_of(id);
            if (e == entt::null)
                continue;

            const old_links* old = nullptr;
            if (auto it = previous.find(id); it != previous.end())
                old = &it->second;

            // children: keep the previous order, append whatever is new
            if (auto it = found.find(id); it != found.end()) {

                std::vector<entity_id> ordered;
                ordered.reserve(it->second.size());
                if (old)
                    for (const entity_id c : old->children)
                        if (std::find(it->second.begin(), it->second.end(), c) != it->second.end())
                            ordered.push_back(c);
                for (const entity_id c : it->second)
                    if (std::find(ordered.begin(), ordered.end(), c) == ordered.end())
                        ordered.push_back(c);

                m_registry.get_or_emplace<component::hierarchy>(e).children = std::move(ordered);
            }

            // parent: leave the old parent's list alone if nothing changed (keeps the sibling order)
            const auto* h = m_registry.try_get<component::hierarchy>(e);
            const entity_id new_parent = h ? h->parent : INVALID_ENTITY;

            if (old && old->parent.is_valid() && old->parent != new_parent)
                if (auto* old_parent = hierarchy_of(old->parent))
                    std::erase(old_parent->children, id);

            if (new_parent.is_valid())
                if (auto* parent = hierarchy_of(new_parent))
                    if (std::find(parent->children.begin(), parent->children.end(), id) == parent->children.end())
                        parent->children.push_back(id);

            if (id != m_world_entity)
                assign_entity_to_cell(id);

            create_physics_body_for(id);
        }

        return restored;
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


    // O(1): m_slot_of_entt maps the entt entity index to our slot. This used to be a linear scan over all slots, and every
    // gather called it once per entity, which made gathering O(n^2) (27k entities: ~290 ms per frame)
    entity_id ecs_world::id_of(entt::entity e) const noexcept {

        if (e == entt::null)
            return INVALID_ENTITY;

        const auto entt_index = static_cast<size_t>(entt::to_entity(e));
        if (entt_index >= m_slot_of_entt.size())
            return INVALID_ENTITY;

        const u32 index = m_slot_of_entt[entt_index];
        if (index >= m_slots.size() || m_slots[index].handle != e)        // also rejects a recycled entt handle (version differs)
            return INVALID_ENTITY;
        return { index, m_slots[index].generation };
    }


    void ecs_world::bind_slot(u32 index, entt::entity handle) {

        if (handle == entt::null)
            return;
        const auto entt_index = static_cast<size_t>(entt::to_entity(handle));
        if (entt_index >= m_slot_of_entt.size())
            m_slot_of_entt.resize(std::max<size_t>(entt_index + 1, m_slot_of_entt.size() * 2), render_cache::NONE);
        m_slot_of_entt[entt_index] = index;
    }

    // component type installation (called by ecs_world_plugin) --------------------------------------------------------

    void ecs_world::install_component_type(const GLT::world::component::type_descriptor& td) {

        if (m_component_storages.contains(td.meta.hash))
            return;

        auto storage = td.make_storage(*this);
        auto* const storage_ptr = storage.get();
        m_component_storages.emplace(td.meta.hash, std::move(storage));

        GLT::world::component::descriptor desc{};
        desc.hash = td.meta.hash;
        desc.name = td.meta.name;
        desc.category = td.meta.category;
        desc.required = td.meta.required;
        desc.scope = td.meta.scope;
        desc.can_remove = [](entity_id) { return true; };
        desc.has = [storage_ptr](entity_id id) { return storage_ptr->has(id); };
        desc.add = [storage_ptr](entity_id id) { storage_ptr->add(id); };
        desc.remove = [storage_ptr](entity_id id) { storage_ptr->remove(id); };

        if (td.draw)
            desc.draw = [this, fn = td.draw](entity_id id) { fn(*this, id); };
        if (td.summary)
            desc.summary = [this, fn = td.summary](entity_id id) { return fn(*this, id); };

        m_components.register_component(std::move(desc));
    }


    void ecs_world::uninstall_component_type(u64 hash) {

        m_component_storages.erase(hash);
        m_components.unregister_component(hash);
    }


    GLT::world::component::i_storage* ecs_world::storage_for(u64 hash) {

        const auto it = m_component_storages.find(hash);
        return it != m_component_storages.end() ? it->second.get() : nullptr;
    }


    void ecs_world::set_physics(physics::i_physics* physics) {

        if (m_physics == physics)
            return;

        // Detach: every entity loses its body — physics pointer is going away
        if (!physics) {
            auto view = m_registry.view<component::physics_body>();
            for (auto e : view) {
                auto& pb = view.get<component::physics_body>(e);
                if (pb.body.is_valid() && m_physics)
                    m_physics->destroy_body(pb.body);

                if (pb.shape.is_valid() && m_physics)
                    m_physics->release_shape(pb.shape);

                pb.body = {};
                pb.shape = {};
            }
            m_physics = nullptr;
            return;
        }

        m_physics = physics;

        // Walk every entity that already has a mesh. Regions activate before this runs in the common case
        // (instantiate() → init() → activate()); the observer catches everything added afterwards
        auto view = m_registry.view<component::mesh>();
        for (auto e : view) {
            const entity_id id = id_of(e);
            if (id.is_valid())
                create_physics_body_for(id);
        }
    }


    void ecs_world::create_physics_body_for(entity_id id) {

        if (!m_physics)
            return;

        const entt::entity ent = entt_of(id);
        if (ent == entt::null)
            return;

        if (const auto* existing = m_registry.try_get<component::physics_body>(ent);
            existing && existing->body.is_valid())
            return;

        const auto* mesh_comp = m_registry.try_get<component::mesh>(ent);
        if (!mesh_comp || !mesh_comp->mesh.valid())
            return;

        auto* registry = asset::registry::get_ptr();
        const auto* mesh_asset = registry->data_as<asset::mesh::mesh_asset>(mesh_comp->mesh.get());
        if (!mesh_asset || mesh_asset->colliders.empty())
            return;

        auto& pb = m_registry.get_or_emplace<component::physics_body>(ent);
        pb.motion = mesh_comp->motion;
        pb.material = mesh_comp->physics_material;
        pb.use_mesh_material = mesh_comp->use_mesh_material;

        // Entity scale, clamped away from zero. Applies to every collider below
        const glm::vec3 entity_scale = glm::max(glm::abs(get_local_scale(id)), glm::vec3(MIN_SCALE));

        // ---------------------------------------------------------------------------------------------------------------
        // Per-collider storage. Each alternative of physics::shape_data may hold spans into one of these vectors; they
        // must all remain alive until m_physics->create_shape() returns (which copies the geometry into Jolt)
        // ---------------------------------------------------------------------------------------------------------------

        struct owned_child {
            physics::shape_data                 data;
            std::vector<glm::vec3>              positions;      // triangle_mesh vertices
            std::vector<glm::vec3>              hull_points;    // convex_hull points (sphere→ellipsoid etc.)
        };
        std::vector<owned_child> owned;
        owned.reserve(mesh_asset->colliders.size());

        for (const auto& collider : mesh_asset->colliders) {

            owned_child child;
            switch (collider.shape) {

                case asset::mesh::collider_shape::sphere:
                    child.data = collider.sphere;
                    break;

                case asset::mesh::collider_shape::box:
                    child.data = collider.box;
                    break;

                case asset::mesh::collider_shape::capsule:
                    child.data = collider.capsule;
                    break;

                case asset::mesh::collider_shape::cylinder:
                    child.data = collider.cylinder;
                    break;

                case asset::mesh::collider_shape::triangle_mesh:
                    child.positions.reserve(mesh_asset->vertices.size());
                    for (const auto& v : mesh_asset->vertices)
                        child.positions.push_back(v.position);
                    child.data = physics::triangle_mesh{
                        .vertices = child.positions,
                        .indices = mesh_asset->indices
                    };
                    break;
            }

            // Apply the entity scale to this collider. For sphere / capsule / cylinder under non-uniform
            // scale this returns a convex_hull whose `points` span is backed by child.hull_points —
            // which the `owned` vector keeps alive through create_shape()
            child.data = apply_collider_scale(std::move(child.data), entity_scale, child.hull_points);

            owned.push_back(std::move(child));
        }

        // Pack into one shape_data: the sole collider, or a compound of all of them
        //
        // The compound's child-local translations from the mesh asset need the entity scale applied
        // along the translation column too, so a compound authored at half-offsets lines up with the
        // scaled visual mesh. Rotations stay untouched (they were authored as pure rotations; build_compound
        // rejects non-unit scale in the local matrix)
        physics::shape_data base_data;
        if (owned.size() == 1) {
            base_data = std::move(owned[0].data);
        } else {
            auto compound = std::make_shared<physics::compound>();
            compound->children.reserve(owned.size());
            for (size_t i = 0; i < owned.size(); ++i) {
                glm::mat4 local = mesh_asset->colliders[i].local_transform;
                local[3][0] *= entity_scale.x;
                local[3][1] *= entity_scale.y;
                local[3][2] *= entity_scale.z;
                compound->children.push_back({
                    std::make_shared<physics::shape_data>(std::move(owned[i].data)),
                    local
                });
            }
            base_data = physics::shape_data{ compound };
        }

        // --- compile to a physics shape -------------------------------------------------------------------------
        //
        // `owned` and everything reachable through it stay alive until here. create_shape() copies all
        // geometry (spans, hull points, mesh vertices) into Jolt-owned memory

        const physics::physics_material material = pb.use_mesh_material
            ? mesh_asset->colliders[0].material
            : pb.material;

        auto shape_result = m_physics->create_shape({
            .data = std::move(base_data),
            .material = material
        });

        if (!shape_result) {
            LOG(error, "Failed to create physics shape for entity [{}]: {}",
                id.index, GLT::util::enum_to_string(shape_result.error()));
            return;
        }

        pb.shape = *shape_result;

        // --- build the body at the entity's current world transform ---------------------------------------------
        //
        // Position AND rotation come from the world matrix. Column-normalizing strips scale — already
        // baked into the geometry by apply_collider_scale(), so the body's rigid transform must not
        // reapply it

        const glm::mat4 world = world_transform_of(id);
        const glm::quat world_rot = glm::quat_cast(glm::mat3(
            glm::normalize(glm::vec3(world[0])),
            glm::normalize(glm::vec3(world[1])),
            glm::normalize(glm::vec3(world[2]))));

        physics::body_desc desc{};
        desc.motion = pb.motion;
        desc.shape = pb.shape;
        desc.position = glm::vec3(world[3]);
        desc.rotation = world_rot;
        desc.linear_damping = 0.02f;
        desc.angular_damping = 0.05f;
        desc.entity = id;

        auto body_result = m_physics->create_body(desc);
        VALIDATE(body_result, m_physics->release_shape(pb.shape); pb.shape = {}; return,
            "", "Failed to create physics body [{}]", (GLT::util::enum_to_string(body_result.error())))
        pb.body = *body_result;
    }


    void ecs_world::rebuild_physics_body(entity_id id) {

        const entt::entity ent = entt_of(id);
        if (ent == entt::null)
            return;

        // Snapshot settings so the fresh component inherits them
        physics::motion_type motion = physics::motion_type::static_body;
        physics::physics_material material{};
        bool use_mesh = true;

        if (const auto* pb = m_registry.try_get<component::physics_body>(ent)) {
            motion = pb->motion;
            material = pb->material;
            use_mesh = pb->use_mesh_material;
        }

        // remove triggers on_destroy → body + shape released for us
        m_registry.remove<component::physics_body>(ent);

        auto& fresh = m_registry.emplace<component::physics_body>(ent);
        fresh.motion = motion;
        fresh.material = material;
        fresh.use_mesh_material = use_mesh;

        create_physics_body_for(id);
    }


    void ecs_world::sync_physics_to_transforms() {

        if (!m_physics)
            return;

        // Batch read is the fast path. Build body list + parallel entity list in one pass
        std::vector<physics::body_handle> bodies;
        std::vector<entt::entity> entities;

        auto view = m_registry.view<component::physics_body, component::transform>();
        bodies.reserve(view.size_hint());
        entities.reserve(view.size_hint());

        for (auto e : view) {
            const auto& pb = view.get<component::physics_body>(e);
            if (!pb.body.is_valid())
                continue;

            if (pb.motion != physics::motion_type::dynamic_body)
                continue;

            bodies.push_back(pb.body);
            entities.push_back(e);
        }

        if (bodies.empty())
            return;

        std::vector<physics::body_state> states(bodies.size());
        m_physics->get_body_states(bodies, states);

        for (size_t i = 0; i < bodies.size(); ++i) {

            if (!states[i].valid)
                continue;

            auto& transform = m_registry.get<component::transform>(entities[i]);
            transform.position = states[i].position;
            transform.rotation = euler_yxz_from_quat(states[i].rotation);
        }
    }  

    // i_world_inspector -----------------------------------------------------------------------------------------------

    std::span<const GLT::world::component::descriptor> ecs_world::descriptors() const noexcept { return m_components.all(); }


    std::vector<const GLT::world::component::descriptor*> ecs_world::components_on(entity_id id) const { return m_components.on(id); }


    bool ecs_world::add_component(entity_id id, u64 hash) { return m_components.add(id, hash); }


    bool ecs_world::remove_component(entity_id id, u64 hash) { return m_components.remove(id, hash); }


    void ecs_world::copy_components(entity_id from, entity_id to) { m_components.copy(from, to); }

    // i_world_scene ---------------------------------------------------------------------------------------------------

    void ecs_world::gather_scene(std::vector<GLT::asset::mesh::instance>& out) const {

        refresh_render_cache(true, false);
        out.assign(m_render.instances.begin(), m_render.instances.end());
    }


    void ecs_world::gather_lights(std::vector<GLT::world::light_snapshot>& out) const {

        refresh_render_cache(false, true);
        out.assign(m_render.lights.begin(), m_render.lights.end());
    }


    bool ecs_world::view_scene(scene_view& out) {

        refresh_render_cache(true, true);
        out.instances = m_render.instances;
        out.lights = m_render.lights;
        out.fixed_instance_count = m_render.fixed_instance_count;
        out.fixed_light_count = m_render.fixed_light_count;
        out.fixed_version = m_render.fixed_version;
        return true;
    }

    // render cache ----------------------------------------------------------------------------------------------------
    //
    // Per frame the cost is: (#movable entities) + (#fixed entities edited this frame, plus their children). A world full
    // of fixed props costs ~nothing. Benchmarked against the old per-frame rebuild (same entt, same component layouts):
    //   50k fixed:     37 ms -> 0 ms (-O2)     137 ms -> 0 ms (-O0)
    //   50k, 1% movable:            0.03 ms (-O2)    0.44 ms (-O0)
    // A full rebuild (structural change: spawn, despawn, reparent, visibility, region streaming) is ~14x faster than the
    // old gather at -O2 too: O(1) ids, closed-form compose, and memoised parents (each parent composed once, not once per
    // descendant: 3.3k entities in chains of 32 went from 55 ms to 1.4 ms)

    void ecs_world::refresh_render_cache(bool meshes, bool lights) const {

        auto& cache = m_render;
        begin_world_memo_pass();

        if (!cache.structure_dirty && !cache.dirty.empty()) {

            // Patch edited fixed entities in place. Movable ones are recomputed below anyway
            bool patched = false;
            for (const entity_id id : cache.dirty) {
                if (!alive(id))
                    continue;                                       // despawned: the destroy signal already asked for a rebuild
                if (!patch_fixed_subtree(id)) {
                    cache.structure_dirty = true;                   // e.g. made visible, mobility changed: membership differs
                    break;
                }
                patched = true;
            }
            if (patched)
                ++cache.fixed_version;
        }
        cache.dirty.clear();

        if (cache.structure_dirty)
            rebuild_render_cache();

        // movables: recomputed every refresh, written after the fixed prefix
        if (meshes) {
            cache.instances.resize(cache.fixed_instance_count + cache.movable_meshes.size());
            u32 count = cache.fixed_instance_count;
            for (const entt::entity entity : cache.movable_meshes) {
                if (!emits_instance(entity))                        // hidden by writing the component directly
                    continue;
                write_instance(cache.instances[count++], entity, id_of(entity));
            }
            cache.instances.resize(count);
        }

        if (lights) {
            // The sun (world entity light) is entry 0 and recomputed every refresh: one entry, and time-of-day scripts
            // rotate it without being told the world entity is movable
            if (cache.has_sun) {
                const entt::entity world_handle = entt_of(m_world_entity);
                if (world_handle != entt::null && m_registry.all_of<component::light, component::transform>(world_handle))
                    write_light(cache.lights[0], world_handle, m_world_entity);
            }
            cache.lights.resize(cache.fixed_light_count + cache.movable_lights.size());
            u32 count = cache.fixed_light_count;
            for (const entt::entity entity : cache.movable_lights)
                write_light(cache.lights[count++], entity, id_of(entity));
        }
    }


    void ecs_world::rebuild_render_cache() const {
        GLT_PROFILE_SCOPE("ecs_world::rebuild_render_cache");

        auto& cache = m_render;
        cache.structure_dirty = false;
        ++cache.fixed_version;

        cache.instance_of_slot.assign(m_slots.size(), render_cache::NONE);
        cache.light_of_slot.assign(m_slots.size(), render_cache::NONE);
        cache.movable_mesh_of_slot.assign(m_slots.size(), 0);
        cache.movable_meshes.clear();
        cache.movable_lights.clear();

        // ---- meshes: fixed ones are written straight into the cache (elements are reused, so their override vectors keep capacity)
        auto mesh_view = m_registry.view<const component::mesh, const component::transform>();
        cache.instances.resize(mesh_view.size_hint());
        u32 count = 0;
        for (const auto [entity, mesh, transform] : mesh_view.each()) {

            if (!mesh.visible || mesh.mesh == INVALID_HANDLE)          // == emits_instance(), components already in hand
                continue;
            const entity_id id = id_of(entity);
            if (!id.is_valid())
                continue;

            // fast path for the common case (fixed, static body, no parent); the full check walks the hierarchy
            const bool movable = transform.mobility == mobility::movable || mesh.motion != physics::motion_type::static_body
                || (m_registry.all_of<component::hierarchy>(entity) && is_effectively_movable(entity));
            if (movable) {
                cache.movable_meshes.push_back(entity);
                cache.movable_mesh_of_slot[id.index] = 1;
                continue;
            }
            cache.instance_of_slot[id.index] = count;
            write_instance(cache.instances[count++], entity, id);
        }
        cache.instances.resize(count);
        cache.fixed_instance_count = count;

        // ---- lights: world entity first (renderers give the first directional light the shadow map)
        auto light_view = m_registry.view<const component::light, const component::transform>();
        cache.lights.resize(light_view.size_hint() + 1);
        count = 0;

        const entt::entity world_handle = entt_of(m_world_entity);
        cache.has_sun = world_handle != entt::null && light_view.contains(world_handle);
        if (cache.has_sun)
            write_light(cache.lights[count++], world_handle, m_world_entity);

        for (const entt::entity entity : light_view) {

            if (entity == world_handle)
                continue;
            const entity_id id = id_of(entity);
            if (!id.is_valid())
                continue;

            if (is_effectively_movable(entity)) {
                cache.movable_lights.push_back(entity);
                continue;
            }
            cache.light_of_slot[id.index] = count;
            write_light(cache.lights[count++], entity, id);
        }
        cache.lights.resize(count);
        cache.fixed_light_count = count;
    }


    bool ecs_world::patch_fixed_subtree(entity_id id) const {

        auto& cache = m_render;
        const entt::entity entity = entt_of(id);
        if (entity == entt::null || id.index >= cache.instance_of_slot.size())
            return false;                                           // spawned after the last rebuild

        const bool movable = is_effectively_movable(entity);

        // mesh entry. Membership must be unchanged, otherwise the caller rebuilds:
        //   cached as fixed    -> must still be fixed and emitting (else: hidden, made movable or lost its mesh)
        //   listed as movable  -> must still be movable (recomputed every refresh, hidden ones are skipped there)
        //   neither            -> must still emit nothing
        const u32 instance = cache.instance_of_slot[id.index];
        const bool emits = emits_instance(entity);
        const bool listed_movable = cache.movable_mesh_of_slot[id.index] != 0;
        if (instance != render_cache::NONE) {
            if (movable || !emits)
                return false;
            write_instance(cache.instances[instance], entity, id);
        } else if (listed_movable) {
            if (!movable)
                return false;
        } else if (emits)
            return false;

        // light entry (the sun is refreshed every frame anyway)
        if (id != m_world_entity) {
            const u32 light = cache.light_of_slot[id.index];
            const bool has_light = m_registry.all_of<component::light, component::transform>(entity);
            if ((light != render_cache::NONE) != (has_light && !movable))
                return false;
            if (light != render_cache::NONE)
                write_light(cache.lights[light], entity, id);
        }

        // children inherit the new world transform
        if (const component::hierarchy* h = hierarchy_of(id))
            for (const entity_id child : h->children)
                if (alive(child) && !patch_fixed_subtree(child))
                    return false;
        return true;
    }


    void ecs_world::begin_world_memo_pass() const {

        auto& cache = m_render;
        if (++cache.memo_pass == 0) {                               // wrapped: invalidate everything explicitly
            std::fill(cache.world_memo_pass.begin(), cache.world_memo_pass.end(), 0u);
            cache.memo_pass = 1;
        }
        if (cache.world_memo.size() < m_slots.size()) {
            cache.world_memo.resize(m_slots.size());
            cache.world_memo_pass.resize(m_slots.size(), 0u);
        }
    }


    // Same result as world_transform_of(), but every entity is composed once per pass: a child reuses its parent's matrix
    // instead of walking (and recomposing) the whole chain, which world_transform_of() does on every call (O(depth))
    //
    // world_transform_of() semantics, kept exactly: [no_inherit_transform] only cuts the chain of the entity that carries
    // it. Its descendants still multiply every ancestor ABOVE it (they are not placed relative to where the no-inherit
    // entity is drawn). So the memo stores the full chain product, and the cut is applied on output
    glm::mat4 ecs_world::world_transform_memo(entity_id id, entt::entity entity) const {

        if (m_registry.all_of<component::no_inherit_transform>(entity)) {
            const auto* transform = m_registry.try_get<component::transform>(entity);
            return transform ? compose_transform(*transform) : glm::mat4(1.0f);
        }
        return chain_transform_memo(id, entity);
    }


    glm::mat4 ecs_world::chain_transform_memo(entity_id id, entt::entity entity) const {

        auto& cache = m_render;
        const bool memo = id.index < cache.world_memo_pass.size();
        if (memo && cache.world_memo_pass[id.index] == cache.memo_pass)
            return cache.world_memo[id.index];

        const auto* transform = m_registry.try_get<component::transform>(entity);
        glm::mat4 matrix = transform ? compose_transform(*transform) : glm::mat4(1.0f);

        if (const auto* h = m_registry.try_get<component::hierarchy>(entity); h && h->parent.is_valid()) {
            const entt::entity parent = entt_of(h->parent);
            if (parent != entt::null)                                // dead parent: world_transform_of stops there too
                matrix = chain_transform_memo(h->parent, parent) * matrix;
        }

        if (memo) {
            cache.world_memo[id.index] = matrix;
            cache.world_memo_pass[id.index] = cache.memo_pass;
        }
        return matrix;
    }


    bool ecs_world::is_effectively_movable(entt::entity entity) const {

        // A movable entity, or a moving physics body
        auto moves_itself = [this](entt::entity e) {
            if (const auto* transform = m_registry.try_get<component::transform>(e); transform && transform->mobility == mobility::movable)
                return true;
            const auto* mesh = m_registry.try_get<component::mesh>(e);
            return mesh && mesh->motion != physics::motion_type::static_body;
        };

        if (moves_itself(entity))
            return true;
        if (m_registry.all_of<component::no_inherit_transform>(entity))
            return false;                                           // ignores every ancestor (see world_transform_memo)

        // every ancestor counts, even above a no-inherit one: its descendants still multiply that part of the chain
        for (const auto* h = m_registry.try_get<component::hierarchy>(entity); h && h->parent.is_valid(); ) {
            const entt::entity parent = entt_of(h->parent);
            if (parent == entt::null)
                break;
            if (moves_itself(parent))
                return true;
            h = m_registry.try_get<component::hierarchy>(parent);
        }
        return false;
    }


    bool ecs_world::emits_instance(entt::entity entity) const {

        const auto* mesh = m_registry.try_get<component::mesh>(entity);
        return mesh && mesh->visible && mesh->mesh != INVALID_HANDLE && m_registry.all_of<component::transform>(entity);
    }


    void ecs_world::write_instance(GLT::asset::mesh::instance& out, entt::entity entity, entity_id id) const {

        const auto& mesh = m_registry.get<component::mesh>(entity);
        out.mesh = mesh.mesh;
        out.procedural_mesh = 0;
        out.flags = 0;
        out.transform = world_transform_memo(id, entity);
        out.entity = id;
        out.material_overrides.resize(mesh.material_overrides.size());        // reused element: no allocation after the first frame
        for (size_t index = 0; index < mesh.material_overrides.size(); ++index)
            out.material_overrides[index] = mesh.material_overrides[index];
    }


    void ecs_world::write_light(GLT::world::light_snapshot& out, entt::entity entity, entity_id id) const {

        const auto& light = m_registry.get<component::light>(entity);
        const glm::mat4 world = world_transform_memo(id, entity);

        out = {};
        out.position = glm::vec3(world[3]);                                     // translation column
        out.direction = -glm::normalize(glm::vec3(world[2]));                   // -Z is forward, engine convention
        out.color = light.color;
        out.intensity = light.intensity;
        out.radius = light.radius;
        out.inner_cone_angle = light.inner_cone_angle;
        out.outer_cone_angle = light.outer_cone_angle;
        out.type = light.type;
        out.entity = id;
    }


    void ecs_world::on_render_content_signal(entt::registry&, entt::entity entity) {

        if (const entity_id id = id_of(entity); id.is_valid())
            m_render.dirty.push_back(id);
    }


    void ecs_world::gather_environment(GLT::world::environment_snapshot& out) const {

        out = {};
        const entt::entity world_handle = entt_of(m_world_entity);

        if (world_handle != entt::null) {

            if (const auto* sky = m_registry.try_get<component::sky>(world_handle)) {
                out.has_sky = true;
                out.sky_intensity = sky->intensity;
                out.sky_haze = sky->haze;
                out.sky_ground_albedo = sky->ground_albedo;
                out.sun_disk_size = sky->sun_disk_size;
            }

            if (const auto* ambient = m_registry.try_get<component::ambient_light>(world_handle)) {
                out.ambient_intensity = ambient->intensity;
                out.ambient_sky_color = ambient->sky_color;
                out.ambient_ground_color = ambient->ground_color;
            }
        }

        // Sun: the world entity's directional light, else the first directional light anywhere (legacy worlds)
        auto take_sun = [&](entt::entity entity, entity_id id) -> bool {
            const auto* light = m_registry.try_get<component::light>(entity);
            if (!light || light->type != GLT::world::light_type::directional)
                return false;
            const glm::mat4 world = world_transform_of(id);
            out.has_sun = true;
            out.sun_direction = -glm::normalize(glm::vec3(world[2]));
            out.sun_color = light->color;
            out.sun_intensity = light->intensity;
            out.sun_entity = id;
            return true;
        };

        if (world_handle != entt::null && take_sun(world_handle, m_world_entity))
            return;

        for (auto entity : m_registry.view<const component::light, const component::transform>()) {
            if (entity == world_handle)
                continue;
            if (const entity_id id = id_of(entity); id.is_valid() && take_sun(entity, id))
                return;
        }
    }

    // world entity ----------------------------------------------------------------------------------------------------

    void ecs_world::load_or_create_world_entity(const asset::world::world_asset& world_asset) {

        m_world_entity = INVALID_ENTITY;

        if (world_asset.world_entity_codec == CODEC_ENTT_V1 && !world_asset.world_entity_data.empty()) {

            const auto ids = m_codec.load(m_registry, std::span<const std::byte>(world_asset.world_entity_data),
                [this](entity_id saved) { return allocate_for_load(saved); });

            if (!ids.empty())
                m_world_entity = ids.front();
            for (size_t i = 1; i < ids.size(); ++i)             // only one world entity is ever written
                release_slot(ids[i]);
        }

        const bool created = !m_world_entity.is_valid();
        if (created) {
            // Worlds saved before world entities existed (and fresh worlds): a default environment. No sun light is added:
            // a legacy world's own directional light keeps working (gather_environment falls back to it)
            m_world_entity = alloc_slot();
            const entt::entity e = entt_of(m_world_entity);
            m_registry.emplace_or_replace<component::sky>(e);
            m_registry.emplace_or_replace<component::ambient_light>(e);
            LOG(info, "world has no world entity yet - created a default one (sky + ambient)");
        }

        // Invariants: a transform (directional lights take their direction from it), never in a hierarchy
        const entt::entity e = entt_of(m_world_entity);
        m_registry.get_or_emplace<component::transform>(e);
        m_registry.remove<component::hierarchy>(e);
    }


    void ecs_world::encode_world_entity(asset::world::world_asset& world_asset) const {

        world_asset.world_entity_data.clear();
        world_asset.world_entity_codec = CODEC_NONE;

        const entt::entity e = entt_of(m_world_entity);
        if (e == entt::null)
            return;

        const entt::entity entities[] = { e };
        m_codec.save(m_registry, std::span<const entt::entity>(entities),
            [this](entt::entity entity) { return entity_id_for(entity); }, world_asset.world_entity_data);
        world_asset.world_entity_codec = CODEC_ENTT_V1;
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
        // a camera parented to a moving platform moves with it
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
        // a camera parented to a moving platform moves with it
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
            if (!id.is_valid() || id == m_world_entity)
                continue;

            GLT::world::entity_position p{};
            p.id = id;
            p.world_position = glm::vec3(world_transform_of(id)[3]);
            out.push_back(p);
        }
    }

    // slot management -------------------------------------------------------------------------------------------------

    entity_id ecs_world::alloc_slot() {

        // m_free may hold stale entries (slots taken by allocate_for_load, or pushed twice): skip slots that are in use
        while (!m_free.empty()) {
            const u32 idx = m_free.back();
            m_free.pop_back();
            if (idx >= m_slots.size() || m_slots[idx].handle != entt::null)
                continue;
            slot& slot = m_slots[idx];
            slot.generation = (slot.generation + 1) ? slot.generation + 1 : 1;   // skip 0
            slot.handle = m_registry.create();
            bind_slot(idx, slot.handle);
            return { idx, slot.generation };
        }

        m_slots.push_back({ .handle = m_registry.create(), .generation = 1 });
        const u32 idx = static_cast<u32>(m_slots.size() - 1);
        bind_slot(idx, m_slots.back().handle);
        return { idx, 1 };
    }


    void ecs_world::release_slot(entity_id id) {

        if (id.index >= m_slots.size())
            return;

        slot& slot = m_slots[id.index];
        if (slot.generation != id.generation)
            return;   // stale handle

        if (slot.handle != entt::null) {
            const auto entt_index = static_cast<size_t>(entt::to_entity(slot.handle));
            if (entt_index < m_slot_of_entt.size())
                m_slot_of_entt[entt_index] = render_cache::NONE;
            m_registry.destroy(slot.handle);
        }

        slot.handle = entt::null;
        slot.generation = (slot.generation + 1) ? slot.generation + 1 : 1;
        m_free.push_back(id.index);
    }


    std::pair<entity_id, entt::entity> ecs_world::allocate_for_load(entity_id preferred) {

        if (preferred.index >= m_slots.size())
            m_slots.resize(preferred.index + 1);

        slot& slot = m_slots[preferred.index];

        if (slot.handle == entt::null) {                   // Slot is free: honour the file'slot id verbatim

            // A released slot may still sit on the free list: alloc_slot skips it because the handle is set again. (This
            // used to std::erase it from m_free: O(free list) per loaded entity, quadratic when a region streams in)
            slot.generation = preferred.generation ? preferred.generation : 1;
            slot.handle = m_registry.create();
            bind_slot(preferred.index, slot.handle);
            return { { preferred.index, slot.generation }, slot.handle };
        }

        // Slot in use: fall back to a fresh id. Cross-region references that pointed at `preferred` will dangle - that's the caller's problem
        const entity_id fresh = alloc_slot();
        return { fresh, m_slots[fresh.index].handle };
    }


    entity_id ecs_world::entity_id_for(entt::entity entity) const noexcept { return id_of(entity); }      // was O(n): saving was O(n^2)

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

        VALIDATE(region.asset != INVALID_HANDLE, region.is_active = true; return,
            "", "region asset handle is unresolved; skipping entity load")

        const auto* region_asset = registry->data_as<GLT::asset::region::region_asset>(region.asset);
        VALIDATE(region_asset, region.is_active = true; return, "", "handle is not a region_asset")

        auto& state = m_region_states[region.id];
        state.entities.clear();

        if (region_asset->entity_codec == CODEC_ENTT_V1 && !region_asset->entity_data.empty()) {

            state.entities = m_codec.load(m_registry, std::span<const std::byte>(region_asset->entity_data),
                [this](entity_id saved) { return allocate_for_load(saved); });

            if (m_physics) {
                for (const entity_id id : state.entities)
                    create_physics_body_for(id);
            }
        }

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


    void ecs_world::detach_from_parent(entity_id id) noexcept {

        const component::hierarchy* h = hierarchy_of(id);
        if (!h || !h->parent.is_valid())
            return;

        if (component::hierarchy* parent = hierarchy_of(h->parent))
            std::erase(parent->children, id);
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
        for (auto& region : world_asset->region_index)
            region.is_active = false;
        world_asset->region_cell_size = m_region_cell_size;
        world_asset->streaming_radius = m_streaming_radius;
        encode_world_entity(*world_asset);
        world_asset->active_camera_index = m_active_camera.index;
        world_asset->active_camera_generation = m_active_camera.generation;
        if (m_active_camera.is_valid()) {
            camera_snapshot snap{};
            if (get_camera_view(m_active_camera, snap))
                world_asset->active_camera_position = snap.position;
        }
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
        // Net: exactly +1 for the caller, regardless of N
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
            // find() returns a borrowed handle -> bump once for the caller
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

    // observers -------------------------------------------------------------------------------------------------------

    void ecs_world::on_mesh_construct(entt::registry&, entt::entity entity) {

        if (!m_physics)
            return;
        const entity_id id = id_of(entity);
        if (!id.is_valid())
            return;
        create_physics_body_for(id);
    }


    void ecs_world::on_physics_body_destroyed(entt::registry& reg, entt::entity entity) {

        // entt still exposes the component during on_destroy. Read the handles, then let the world's own destruction proceed
        const auto* pb = reg.try_get<component::physics_body>(entity);
        if (!pb || !m_physics)
            return;

        if (pb->body.is_valid())
            m_physics->destroy_body(pb->body);

        if (pb->shape.is_valid())
            m_physics->release_shape(pb->shape);
    }

    // preset helpers ---------------------------------------------------------------------------------------------

    entity_id ecs_world::instantiate_preset_recursive(const GLT::world::entity_preset& preset, entity_id parent) {

        const std::string& name = preset.entity_name.empty() ? preset.name : preset.entity_name;
        auto builder = make_entity(name);

        if (parent.is_valid())
            builder.parent(parent);

        // Set the transform first so world-space cell assignment is correct
        component::transform tf;
        tf.position = preset.position;
        tf.rotation = preset.rotation;
        tf.scale = preset.scale;
        builder.set_transform(tf);

        // set_transform does NOT re-bin the entity by world position - fix that up now, before children look at our transform
        assign_entity_to_cell(builder.id());

        // Apply component specs. `transform` and `name` are handled implicitly from the preset's own fields - skip them
        // so a preset with an explicit transform spec doesn't clobber what we just set
        for (const auto& spec : preset.components) {

            if (spec.name == "transform" || spec.name == "name")
                continue;

            if (const auto* fn = m_plugin.find_preset_component(spec.name))
                (*fn)(builder, spec);
            else
                LOG(warn, "preset '{}' requests unknown component '{}' - skipping", preset.name, spec.name);
        }

        // Children. Instantiating them *after* the parent is fully set up ensures each child's world-transform (and
        // therefore its cell assignment) reflects the parent's transform
        for (const auto& child : preset.children)
            (void)instantiate_preset_recursive(child, builder.id());

        return builder.id();
    }

}
