#pragma once

#include <entt/entt.hpp>

#include <world/i_world.h>
#include <world/i_world_scene.h>
#include <world/component.h>
#include <world/component_registry.h>
#include <world/i_world_inspector.h>
#include <world/entity_preset.h>

#include "components.h"
#include "entity_builder.h"
#include "component_registry.h"

namespace GLT::asset::world {
    class world_asset;
}



// entity_codec and ecs_world_plugin are forward-declared; both are only used
// behind a reference in this header and are fully defined in ecs_world.cpp.

namespace GLT::world::world_ecs_entt {

    class ecs_world_plugin;
    class entity_codec;

    // @brief EnTT-backed world instance.
    //
    // Region entity data lives in region_asset::entity_data as an opaque blob
    // produced by the shared entity_codec (CODEC_ENTT_V1).
    //
    // Owns: ECS registry, slot table, region list, streaming state, active camera,
    // world asset ref, per-instance component_registry.
    // Borrows: entity_codec and asset handler (via ecs_world_plugin).
    class ecs_world final : public i_world,
        public i_world_inspector,
        public i_world_scene {
    public:

        ecs_world(ecs_world_plugin& plugin, asset::asset_ref asset);
        ~ecs_world() override;

        // Called exactly once by ecs_world_plugin::instantiate(); resolves region
        // handles, sets up streaming state. Failures abort instantiation.
        [[nodiscard]] std::expected<void, asset::load_error> init();

        // i_world -----------------------------------------------------------------------------------------------------
        [[nodiscard]] const asset::asset_ref& asset() const noexcept override { return m_asset; }
        [[nodiscard]] asset::handle asset_handle() const noexcept override     { return m_asset.get(); }

        [[nodiscard]] std::expected<void, asset::load_error> save() override;
        [[nodiscard]] std::expected<void, asset::load_error> save_as(const std::filesystem::path& path) override;

        [[nodiscard]] entity_id world_entity() const noexcept override { return m_world_entity; }
        [[nodiscard]] entity_id spawn() override;
        void despawn(entity_id id) noexcept override;
        [[nodiscard]] bool alive(entity_id id) const noexcept override;

        [[nodiscard]] bool is_visible(entity_id id) const override;
        void set_visible(entity_id id, bool visible) override;

        [[nodiscard]] mobility get_mobility(entity_id id) const override;
        void set_mobility(entity_id id, mobility value) override;
        void mark_changed(entity_id id) override;

        entity_id spawn_preset(std::string_view preset_name, entity_id parent = INVALID_ENTITY) override;

        [[nodiscard]] glm::vec3 get_local_position(entity_id id) const override;
        [[nodiscard]] glm::vec3 get_local_rotation(entity_id id) const override;
        void translate_local(entity_id id, const glm::vec3& local_delta) override;
        void rotate_local(entity_id id, const glm::vec3& euler_delta) override;
        [[nodiscard]] glm::quat get_local_rotation_q(entity_id id) const override;
        void set_local_rotation_q(entity_id id, const glm::quat& rotation) override;
        [[nodiscard]] glm::vec3 get_local_scale(entity_id id) const override;
        void set_local_scale(entity_id id, const glm::vec3& scale) override;

        [[nodiscard]] std::span<const asset::region::region> regions() const noexcept override;
        [[nodiscard]] const asset::region::region* region_at(const glm::vec3& point) const noexcept override;
        [[nodiscard]] bool is_region_active(UUID id) const noexcept override;
        void set_region_active(UUID id, bool active) override;

        void set_streaming_anchor(const glm::vec3& position) override;
        [[nodiscard]] glm::vec3 get_streaming_anchor() const noexcept override;
        void set_streaming_radius(f32 radius) override;
        [[nodiscard]] f32 get_streaming_radius() const noexcept override;

        void update(f32 delta_time) override;

        [[nodiscard]] glm::vec3 get_region_cell_size() const noexcept override;
        void set_region_cell_size(const glm::vec3& size) override;
        void reassign_all_entities() override;

        [[nodiscard]] std::vector<entity_id> root_entities() const override;
        [[nodiscard]] std::vector<entity_id> children_of(entity_id id) const override;
        [[nodiscard]] std::string_view entity_name(entity_id id) const override;
        [[nodiscard]] bool has_children(entity_id id) const noexcept override;
        void set_parent(entity_id child, entity_id parent) override;
        [[nodiscard]] entity_id parent_of(entity_id id) const noexcept override;
        [[nodiscard]] std::vector<std::byte> snapshot_entities(std::span<const entity_id> ids) const override;
        std::vector<entity_id> restore_entities(std::span<const std::byte> blob) override;

        // extension API (entity_builder, component_registry) ----------------------------------------------------------
        [[nodiscard]] entt::registry& registry() noexcept             { return m_registry; }
        [[nodiscard]] const entt::registry& registry() const noexcept { return m_registry; }

        [[nodiscard]] entity_builder make_entity(std::string_view name = "");
        [[nodiscard]] entity_builder edit(entity_id id);

        [[nodiscard]] entt::entity entt_of(entity_id id) const noexcept;
        [[nodiscard]] entity_id id_of(entt::entity e) const noexcept;

        // component type installation (called by ecs_world_plugin) ----------------------------------------------------

        void install_component_type(const GLT::world::component::type_descriptor& td);
        void uninstall_component_type(u64 hash);

        // i_world -----------------------------------------------------------------------------------------------------
        [[nodiscard]] GLT::world::component::i_storage* storage_for(u64 hash) override;

        // physics -----------------------------------------------------------------------------------------------------
        void set_physics(physics::i_physics* physics);
        [[nodiscard]] physics::i_physics* get_physics() const noexcept { return m_physics; }
        void create_physics_body_for(entity_id id);
        void rebuild_physics_body(entity_id id);                                // destroy + create (motion change, mesh swap)
        void sync_physics_to_transforms();                                      // post-step: body_state → component::transform

        // i_world_inspector -------------------------------------------------------------------------------------------
        [[nodiscard]] std::span<const GLT::world::component::descriptor> descriptors() const noexcept override;
        [[nodiscard]] std::vector<const GLT::world::component::descriptor*> components_on(entity_id id) const override;
        bool add_component(entity_id id, u64 hash) override;
        bool remove_component(entity_id id, u64 hash) override;
        void copy_components(entity_id from, entity_id to) override;

        // i_world_scene -----------------------------------------------------------------------------------------------
        void gather_scene(std::vector<asset::mesh::instance>& out) const override;
        void gather_lights(std::vector<light_snapshot>& out) const override;
        [[nodiscard]] bool view_scene(scene_view& out) override;
        void gather_environment(environment_snapshot& out) const override;
        void gather_entity_positions(std::vector<entity_position>& out) const override;
        [[nodiscard]] bool get_camera_view(camera_snapshot& out) const override;
        [[nodiscard]] bool get_camera_view(world::entity_id camera, world::camera_snapshot& out) override;
        void set_active_camera(entity_id id) override;
        [[nodiscard]] entity_id get_active_camera() const noexcept override { return m_active_camera; }

    private:

        struct slot {
            entt::entity                                    handle{ entt::null };
            u32                                             generation{ 0 };
        };

        struct region_state {
            std::vector<entity_id>                          entities;
        };

        // Render snapshot cache. Fixed entities are written once and patched when edited; movable ones are recomputed on
        // every refresh from a cached entity list (no per-frame scan of all entities). Mutable: the gather functions are
        // const by contract but keep this cache warm
        struct render_cache {
            static constexpr u32                            NONE = UINT32_MAX;

            std::vector<asset::mesh::instance>              instances;          // [fixed | movable]
            std::vector<light_snapshot>                     lights;             // [sun | fixed | movable]
            std::vector<entt::entity>                       movable_meshes;     // in output order, after the fixed prefix
            std::vector<entt::entity>                       movable_lights;
            std::vector<u32>                                instance_of_slot;   // slot index -> fixed instance index (NONE: not cached)
            std::vector<u32>                                light_of_slot;      // slot index -> fixed light index
            std::vector<u8>                                 movable_mesh_of_slot;   // slot index -> listed in movable_meshes
            std::vector<entity_id>                          dirty;              // edited since the last refresh (patched or ignored if movable)
            std::vector<glm::mat4>                          world_memo;         // per slot: ancestors * local (see chain_transform_memo), valid for [memo_pass]
            std::vector<u32>                                world_memo_pass;
            u32                                             memo_pass = 0;
            u32                                             fixed_instance_count = 0;
            u32                                             fixed_light_count = 0;      // includes the sun entry
            u64                                             fixed_version = 1;
            bool                                            has_sun = false;
            bool                                            structure_dirty = true;     // membership changed: full rebuild
        };

        // Init helpers --------------------------------------------------------
        [[nodiscard]] std::expected<void, asset::load_error> init_from_world_asset();

        // World entity --------------------------------------------------------
        void load_or_create_world_entity(const asset::world::world_asset& world_asset);
        void encode_world_entity(asset::world::world_asset& world_asset) const;

        // Slot management -----------------------------------------------------
        [[nodiscard]] entity_id alloc_slot();
        void release_slot(entity_id id);
        [[nodiscard]] std::pair<entity_id, entt::entity> allocate_for_load(entity_id preferred);
        [[nodiscard]] entity_id entity_id_for(entt::entity entity) const noexcept;
        void bind_slot(u32 index, entt::entity handle);                          // keeps m_slot_of_entt in sync with m_slots

        // Streaming -----------------------------------------------------------
        void stream_pass();
        [[nodiscard]] bool should_be_active(const asset::region::region& r) const noexcept;
        void activate(asset::region::region& r);
        void deactivate(asset::region::region& r);
        [[nodiscard]] asset::region::region* find_region(UUID id) noexcept;

        // Hierarchy helpers ---------------------------------------------------
        [[nodiscard]] component::hierarchy* hierarchy_of(entity_id id) noexcept;
        [[nodiscard]] const component::hierarchy* hierarchy_of(entity_id id) const noexcept;
        void detach_from_parent(entity_id id) noexcept;                        // removes `id` from its parent's children list

        // Transform -----------------------------------------------------------
        [[nodiscard]] glm::mat4 world_transform_of(entity_id id) const noexcept;

        // Render cache (see render_cache) ------------------------------------
        void refresh_render_cache(bool meshes, bool lights) const;
        void rebuild_render_cache() const;
        [[nodiscard]] bool patch_fixed_subtree(entity_id id) const;              // false: membership changed, rebuild instead
        void begin_world_memo_pass() const;
        [[nodiscard]] glm::mat4 world_transform_memo(entity_id id, entt::entity entity) const;  // O(1) amortised within a pass
        [[nodiscard]] glm::mat4 chain_transform_memo(entity_id id, entt::entity entity) const;  // product of all ancestors * local
        [[nodiscard]] bool is_effectively_movable(entt::entity entity) const;   // own / inherited mobility, physics motion
        [[nodiscard]] bool emits_instance(entt::entity entity) const;
        void write_instance(asset::mesh::instance& out, entt::entity entity, entity_id id) const;
        void write_light(light_snapshot& out, entt::entity entity, entity_id id) const;
        void mark_render_structure_dirty() const noexcept { m_render.structure_dirty = true; }
        void on_render_structure_signal(entt::registry&, entt::entity) { m_render.structure_dirty = true; }
        void on_render_content_signal(entt::registry&, entt::entity entity);

        // Persistence helpers ------------------------------------------------
        [[nodiscard]] std::expected<void, asset::load_error> flush_regions();
        void prune_empty_regions();
        void sync_world_asset();
        [[nodiscard]] std::filesystem::path region_path_for_cell(const glm::ivec3& cell) const;
        [[nodiscard]] asset::handle resolve_region_handle(const glm::ivec3& cell, UUID expected_id);

        // Region grid ---------------------------------------------------------
        [[nodiscard]] glm::ivec3 cell_coord_of(const glm::vec3& world_pos) const noexcept;
        [[nodiscard]] AABB cell_bounds(const glm::ivec3& cell) const noexcept;
        [[nodiscard]] UUID ensure_region_for_cell(const glm::ivec3& cell);
        void assign_entity_to_cell(entity_id id);

        // observers ---------------------------------------------------------------------------------------------------
        void on_mesh_construct(entt::registry& reg, entt::entity entity);
        void on_physics_body_destroyed(entt::registry& reg, entt::entity entity);

        // preset helpers ---------------------------------------------------------------------------------------------
        entity_id instantiate_preset_recursive(const GLT::world::entity_preset& preset, entity_id parent);


        ecs_world_plugin&                                                               m_plugin;
        const entity_codec&                                                             m_codec;            // borrowed from plugin (stateless)
        component_registry                                                              m_components;       // per-instance, descriptors capture this
        asset::asset_ref                                                                m_asset;            // owns one ref to the .glt_world
        mutable render_cache                                                            m_render;           // BEFORE m_registry: its destruction fires on_destroy handlers that touch this
        entt::registry                                                                  m_registry;
        std::vector<asset::region::region>                                              m_regions;
        std::unordered_map<UUID, region_state>                                          m_region_states;
        std::vector<slot>                                                               m_slots;
        std::vector<u32>                                                                m_slot_of_entt;     // entt::to_entity(handle) -> slot index (O(1) id_of)
        std::vector<u32>                                                                m_free;
        glm::vec3                                                                       m_streaming_anchor{ 0.f };
        f32                                                                             m_streaming_radius{ 1024.f };
        entity_id                                                                       m_active_camera{ INVALID_ENTITY };
        entity_id                                                                       m_world_entity{ INVALID_ENTITY };   // never streamed, see i_world::world_entity
        glm::vec3                                                                       m_region_cell_size{ 256.f, 256.f, 256.f };
        physics::i_physics*                                                             m_physics = nullptr;
        std::unordered_map<u64, GLT::unique_ref<GLT::world::component::i_storage>>      m_component_storages;

    };

}

#include "entity_builder.inl"
// #include "component_registry.inl"
