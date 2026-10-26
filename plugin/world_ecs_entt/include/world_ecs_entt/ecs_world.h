#pragma once

#include <entt/entt.hpp>

#include <unordered_map>
#include <vector>

#include <world/i_world.h>
#include <world/i_world_scene.h>
#include <world/i_world_inspector.h>

#include "components.h"
#include "entity_builder.h"
#include "component_registry.h"

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

        [[nodiscard]] entity_id spawn() override;
        void despawn(entity_id id) noexcept override;
        [[nodiscard]] bool alive(entity_id id) const noexcept override;

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

        // extension API (entity_builder, component_registry) ----------------------------------------------------------
        [[nodiscard]] entt::registry& registry() noexcept             { return m_registry; }
        [[nodiscard]] const entt::registry& registry() const noexcept { return m_registry; }

        [[nodiscard]] entity_builder make_entity(std::string_view name = "");
        [[nodiscard]] entity_builder edit(entity_id id);

        [[nodiscard]] entt::entity entt_of(entity_id id) const noexcept;
        [[nodiscard]] entity_id id_of(entt::entity e) const noexcept;

        // physics -----------------------------------------------------------------------------------------------------
        void set_physics(physics::i_physics* physics);
        [[nodiscard]] physics::i_physics* get_physics() const noexcept { return m_physics; }
        void create_physics_body_for(entity_id id);
        void rebuild_physics_body(entity_id id);                                // destroy + create (motion change, mesh swap)
        void sync_physics_to_transforms();                                      // post-step: body_state → component::transform

        // i_world_inspector -------------------------------------------------------------------------------------------
        [[nodiscard]] std::span<const component_descriptor> descriptors() const noexcept override;
        [[nodiscard]] std::vector<const component_descriptor*> components_on(entity_id id) const override;
        bool add_component(entity_id id, u64 hash) override;
        bool remove_component(entity_id id, u64 hash) override;
        void copy_components(entity_id from, entity_id to) override;

        // i_world_scene -----------------------------------------------------------------------------------------------
        void gather_scene(std::vector<asset::mesh::instance>& out) const override;
        void gather_lights(std::vector<light_snapshot>& out) const override;
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

        // Init helpers --------------------------------------------------------
        [[nodiscard]] std::expected<void, asset::load_error> init_from_world_asset();

        // Slot management -----------------------------------------------------
        [[nodiscard]] entity_id alloc_slot();
        void release_slot(entity_id id);
        [[nodiscard]] std::pair<entity_id, entt::entity> allocate_for_load(entity_id preferred);
        [[nodiscard]] entity_id entity_id_for(entt::entity entity) const noexcept;

        // Streaming -----------------------------------------------------------
        void stream_pass();
        [[nodiscard]] bool should_be_active(const asset::region::region& r) const noexcept;
        void activate(asset::region::region& r);
        void deactivate(asset::region::region& r);
        [[nodiscard]] asset::region::region* find_region(UUID id) noexcept;

        // Hierarchy helpers ---------------------------------------------------
        [[nodiscard]] component::hierarchy* hierarchy_of(entity_id id) noexcept;
        [[nodiscard]] const component::hierarchy* hierarchy_of(entity_id id) const noexcept;

        // Transform -----------------------------------------------------------
        [[nodiscard]] glm::mat4 world_transform_of(entity_id id) const noexcept;

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


        ecs_world_plugin&                               m_plugin;
        const entity_codec&                             m_codec;            // borrowed from plugin (stateless)
        component_registry                              m_components;       // per-instance, descriptors capture this
        asset::asset_ref                                m_asset;            // owns one ref to the .glt_world
        entt::registry                                  m_registry;
        std::vector<asset::region::region>              m_regions;
        std::unordered_map<UUID, region_state>          m_region_states;
        std::vector<slot>                               m_slots;
        std::vector<u32>                                m_free;
        glm::vec3                                       m_streaming_anchor{ 0.f };
        f32                                             m_streaming_radius{ 1024.f };
        entity_id                                       m_active_camera{ INVALID_ENTITY };
        glm::vec3                                       m_region_cell_size{ 256.f, 256.f, 256.f };
        physics::i_physics*                             m_physics = nullptr;

    };

}

#include "entity_builder.inl"
#include "component_registry.inl"
