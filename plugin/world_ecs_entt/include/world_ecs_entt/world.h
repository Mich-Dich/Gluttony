
#pragma once

#include <world/i_world.h>
#include <world/i_world_scene.h>

#include "world_asset_handler.h"
#include "components.h"
#include "entity_codec.h"
#include "entity_builder.h"
#include "component_registry.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Default world implementation shipped with the engine. Backed by EnTT.
    //
    // Region entity data lives in region_asset::entity_data as an opaque blob produced by entity_codec (CODEC_ENTT_V1).
    //
    // Owns:
    //   - the ECS registry
    //   - the slot table that maps entity_id <-> entt::entity
    //   - the region streaming pass
    //
    // Does NOT own:
    //   - the region/world assets themselves (registry does)
    //   - asset refcounting (see notes at the bottom of the .cpp)
    class ecs_world_plugin final :
        public GLT::world::i_world_plugin,
        public GLT::world::i_world_inspector,
        public GLT::world::i_world_scene {
    public:

        ecs_world_plugin();
        ~ecs_world_plugin() override;

        // i_plugin ----------------------------------------------------------------------------------------------------

        void on_load() override;


        void on_unload() override;

        // world lifecycle ---------------------------------------------------------------------------------------------

        [[nodiscard]] std::expected<void, GLT::asset::load_error> load_world(GLT::asset::asset_ref world) override;


        void unload_world() noexcept override;


        [[nodiscard]] GLT::asset::handle world_handle() const noexcept override;


        [[nodiscard]] std::expected<void, GLT::asset::load_error> save_world() override;

    
        [[nodiscard]] std::expected<void, GLT::asset::load_error> save_world_as(const std::filesystem::path& path) override;

        // entity lifecycle --------------------------------------------------------------------------------------------

        [[nodiscard]] entity_id spawn() override;


        void despawn(entity_id id) noexcept override;


        [[nodiscard]] bool alive(entity_id id) const noexcept override;

        // transform ---------------------------------------------------------------------------------------------------

        [[nodiscard]] glm::vec3 get_local_position(entity_id id) const override;


        [[nodiscard]] glm::vec3 get_local_rotation(entity_id id) const override;


        void translate_local(entity_id id, const glm::vec3& local_delta) override;


        void rotate_local(entity_id id, const glm::vec3& euler_delta) override;


        [[nodiscard]] glm::quat get_local_rotation_q(entity_id id) const override;


        void set_local_rotation_q(entity_id id, const glm::quat& rotation) override;


        [[nodiscard]] glm::vec3 get_local_scale(entity_id id) const override;


        void set_local_scale(entity_id id, const glm::vec3& scale) override;

        // region queries ----------------------------------------------------------------------------------------------

        [[nodiscard]] std::span<const GLT::asset::region::region> regions() const noexcept override;


        [[nodiscard]] const GLT::asset::region::region* region_at(const glm::vec3& point) const noexcept override;


        [[nodiscard]] bool is_region_active(GLT::UUID id) const noexcept override;


        void set_region_active(GLT::UUID id, bool active) override;

        // streaming ---------------------------------------------------------------------------------------------------

        void set_streaming_anchor(const glm::vec3& position) override;


        [[nodiscard]] glm::vec3 get_streaming_anchor() const noexcept override;


        void set_streaming_radius(const f32 radius) override;


        [[nodiscard]] f32 get_streaming_radius() const noexcept override;

        // update ------------------------------------------------------------------------------------------------------

        void update(f32 delta_time) override;

        // hierarchy ---------------------------------------------------------------------------------------------------

        [[nodiscard]] std::vector<entity_id> root_entities() const override;


        // Returns a copy - the underlying vector lives in the ECS and may be invalidated by any subsequent structural change.
        [[nodiscard]] std::vector<entity_id> children_of(entity_id id) const override;


        [[nodiscard]] std::string_view entity_name(entity_id id) const override;


        [[nodiscard]] bool has_children(entity_id id) const noexcept override;


        // The only place the parent/child invariant is enforced. Use this instead of touching `hierarchy` directly.
        //
        //   - self-parent: no-op
        //   - cycle-creating: no-op
        //   - parent == INVALID_ENTITY: detach from current parent
        //   - parent not alive: no-op
        void set_parent(entity_id child, entity_id parent) override;


        [[nodiscard]] entity_id parent_of(entity_id id) const noexcept override;

        // extension API for plugins that couple to this concrete type -------------------------------------------------

        [[nodiscard]] entt::registry& registry() noexcept { return m_registry; }


        [[nodiscard]] const entt::registry& registry() const noexcept { return m_registry; }


        template<typename T>
        void register_component(std::string_view name) { m_codec.register_component<T>(name); }

        // builder API -------------------------------------------------------------------------------------------------

        // Create a fresh entity, optionally named. The entity exists immediately; the builder is just a handle for chaining.
        [[nodiscard]] entity_builder make_entity(std::string_view name = "");


        // Open an existing entity for editing. The returned builder is invalid if `id` is not alive.
        [[nodiscard]] entity_builder edit(entity_id id);

        // ECS internals (used by entity_builder) -----------------------------------------------------------------------

        [[nodiscard]] entt::entity entt_of(entity_id id) const noexcept;


        [[nodiscard]] entity_id id_of(entt::entity e) const noexcept;

        // region grid ------------------------------------------------------------------------------------

        [[nodiscard]] glm::vec3 get_region_cell_size() const noexcept override;


        void set_region_cell_size(const glm::vec3& size) override;


        void reassign_all_entities() override;



        // i_world_inspector -------------------------------------------------------------------------------------------

        [[nodiscard]] std::span<const GLT::world::component_descriptor> descriptors() const noexcept override;


        [[nodiscard]] std::vector<const GLT::world::component_descriptor*> components_on(entity_id id) const override;


        bool add_component(entity_id id, u64 hash) override;


        bool remove_component(entity_id id, u64 hash) override;


        void copy_components(entity_id from, entity_id to) override;




        // i_world_scene -----------------------------------------------------------------------------------------------

        void gather_scene(std::vector<GLT::asset::mesh::instance>& out) const override;


        void gather_lights(std::vector<GLT::world::light_snapshot>& out) const override;


        void gather_entity_positions(std::vector<GLT::world::entity_position>& out) const override;


        [[nodiscard]] bool get_camera_view(GLT::world::camera_snapshot& out) const override;


        void set_active_camera(entity_id id) override;


        [[nodiscard]] entity_id get_active_camera() const noexcept override { return m_active_camera; }

    private:

        struct slot {

            entt::entity                                    handle{ entt::null };
            u32                                             generation{ 0 };
        };

        struct region_state {

            std::vector<entity_id>                          entities;      // entities spawned by this region
        };

    
        entity_id entity_id_for(entt::entity entity) const noexcept;

        // slot management ----------------------------------------------------------------------------------------------

        [[nodiscard]] entity_id alloc_slot();


        void release_slot(entity_id id);


        [[nodiscard]] std::pair<entity_id, entt::entity> allocate_for_load(entity_id preferred);

        // streaming ----------------------------------------------------------------------------------------------------

        void stream_pass();


        [[nodiscard]] bool should_be_active(const GLT::asset::region::region& r) const noexcept;


        void activate(GLT::asset::region::region& r);


        void deactivate(GLT::asset::region::region& r);


        [[nodiscard]] GLT::asset::region::region* find_region(GLT::UUID id) noexcept;


        [[nodiscard]] component::hierarchy* hierarchy_of(entity_id id) noexcept;


        [[nodiscard]] const component::hierarchy* hierarchy_of(entity_id id) const noexcept;


        // Re-encode each active region's entities and call registry->save(region)
        // Called from save_world / save_world_as before touching the world asset
        [[nodiscard]] std::expected<void, GLT::asset::load_error> flush_regions();


        // Reaps regions that are no longer needed: active, non-core, and empty after reassign_all_entities() has run
        //
        // Only active regions are candidates. An inactive region may still have entity data on disk that hasn't been
        // loaded into the slot table since the last save - pruning it would silently drop that data. Since activation
        // happens before saving, everything that should be checked will be
        //
        // The core region (cell 0,0,0) is preserved by flag; if pruning would empty m_regions entirely, one candidate is
        // kept so the world still satisfies "at least one region"
        void prune_empty_regions();


        [[nodiscard]] glm::mat4 world_transform_of(entity_id id) const noexcept;


        void sync_world_asset();


        // Naming-convention path for a region cell:
        //   <world_dir>/<world_stem>_r_<x>_<y>_<z>.glt_region
        // Returns {} if the world handle is stale or has no virtual path.
        [[nodiscard]] std::filesystem::path region_path_for_cell(const glm::ivec3& cell) const;


        // Resolve (or create) the region that lives in `cell`. Tries, in order:
        //   1. registry->find_by_id(expected_id)   - region is already loaded
        //   2. registry->find(region_path)          - same, but under the canonical path
        //   3. registry->load(region_path)          - file exists on disk
        //   4. register_runtime(...)                - fresh empty region, using `expected_id`
        //
        // On success, the returned handle is **retained** on the plugin's behalf;
        // the caller must eventually release it (unload_world does this uniformly).
        // Returns INVALID_HANDLE on total failure.
        [[nodiscard]] GLT::asset::handle resolve_region_handle(const glm::ivec3& cell, GLT::UUID expected_id);

        // region grid -------------------------------------------------------------------------------------------------

        // floor((p + cell/2) / cell) per axis, so cell (0,0,0) is centred on the origin
        [[nodiscard]] glm::ivec3 cell_coord_of(const glm::vec3& world_pos) const noexcept;


        // Bounds of a cell: [cell*size - size/2, cell*size + size/2]
        [[nodiscard]] GLT::AABB cell_bounds(const glm::ivec3& cell) const noexcept;


        // Returns the UUID of the region whose centre is in `cell`, creating one (and retaining it) if none exists. Returns {} on failure
        [[nodiscard]] GLT::UUID ensure_region_for_cell(const glm::ivec3& cell);


        // Move [id] to the region its world position falls into. No-op if already there.
        void assign_entity_to_cell(entity_id id);



        entt::registry                                      m_registry;
        entity_codec                                        m_codec;
        world_asset_handler                                 m_asset_handler;
        std::vector<GLT::asset::region::region>             m_regions{};
        std::unordered_map<GLT::UUID, region_state>         m_region_states{};
        std::vector<slot>                                   m_slots{};
        std::vector<u32>                                    m_free{};
        GLT::asset::asset_ref                               m_world{};
        glm::vec3                                           m_streaming_anchor{ 0.f };
        f32                                                 m_streaming_radius{ 1024.f };
        component_registry                                  m_components{ *this };
        entity_id                                           m_active_camera{ INVALID_ENTITY };
        glm::vec3                                           m_region_cell_size{ 256.f, 256.f, 256.f };

    };

}

#include "entity_builder.inl"
#include "component_registry.inl"
