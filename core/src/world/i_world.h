#pragma once

#include <glm/glm.hpp>

#include "asset/type.h"
#include "asset/asset_ref.h"
#include "asset/region.h"
#include "world/entity.h"
#include "physics/i_physics.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {
    class i_world_scene;
    class i_world_inspector;
}

namespace GLT::world {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief One live world instance
    //
    // Owns its own ECS registry, slot table, region list, streaming state, and per-frame update loop
    // Multiple instances can coexist (one per game_session). Lifetime is RAII: instances are minted
    // by i_world_plugin's factory methods and destroyed when their owning unique_ref drops
    //
    // "Destroy at runtime" == drop the unique_ref. No explicit teardown call is needed; entity slots,
    // region assets, and streaming state all unwind in the destructor
    class i_world {
    public:
        virtual ~i_world() = default;

        // identity ----------------------------------------------------------------------------------------------------

        // Underlying .glt_world asset. Invalid for a world created via create_empty()
        // until save_as() mints an asset for it
        [[nodiscard]] virtual const asset::asset_ref& asset() const noexcept = 0;
        [[nodiscard]] virtual asset::handle asset_handle() const noexcept = 0;

        // physics -----------------------------------------------------------------------------------------------------

        // Give this world a physics simulation to drive. Passing nullptr detaches. Sessions call this once per world,
        // after the world has been instantiated and its regions resolved. The world walks any entities already carrying
        // mesh components and creates matching rigid bodies; from then on every newly added mesh with colliders gets
        // a body automatically. Worlds that don't care about physics just keep the default no-op
        virtual void set_physics(GLT::physics::i_physics* /*physics*/) {}
    
        // persistence -------------------------------------------------------------------------------------------------

        // Persist back to the world's canonical path. Fails with not_found when no asset is bound
        [[nodiscard]] virtual std::expected<void, asset::load_error> save() = 0;

        // Persist to `path`. If no asset is bound yet, mints a fresh one and installs it there
        // (this is Save-As on a never-saved world). If one is already bound elsewhere, migrates
        [[nodiscard]] virtual std::expected<void, asset::load_error> save_as(const std::filesystem::path& path) = 0;

        // entity lifecycle -------------------------------------------------------------------------------------------
        [[nodiscard]] virtual entity_id spawn() = 0;
        virtual void despawn(entity_id id) noexcept = 0;
        [[nodiscard]] virtual bool alive(entity_id id) const noexcept = 0;

        // transform ---------------------------------------------------------------------------------------------------
        [[nodiscard]] virtual glm::vec3 get_local_position(entity_id id) const = 0;
        [[nodiscard]] virtual glm::vec3 get_local_rotation(entity_id id) const = 0;
        virtual void translate_local(entity_id id, const glm::vec3& local_delta) = 0;
        virtual void rotate_local(entity_id id, const glm::vec3& euler_delta) = 0;
        [[nodiscard]] virtual glm::quat get_local_rotation_q(entity_id id) const = 0;
        virtual void set_local_rotation_q(entity_id id, const glm::quat& rotation) = 0;
        [[nodiscard]] virtual glm::vec3 get_local_scale(entity_id id) const = 0;
        virtual void set_local_scale(entity_id id, const glm::vec3& scale) = 0;

        // region queries ----------------------------------------------------------------------------------------------
        [[nodiscard]] virtual std::span<const asset::region::region> regions() const noexcept = 0;
        [[nodiscard]] virtual const asset::region::region* region_at(const glm::vec3& point) const noexcept = 0;
        [[nodiscard]] virtual bool is_region_active(UUID id) const noexcept = 0;
        virtual void set_region_active(UUID id, bool active) = 0;

        // streaming ---------------------------------------------------------------------------------------------------
        virtual void set_streaming_anchor(const glm::vec3& position) = 0;
        [[nodiscard]] virtual glm::vec3 get_streaming_anchor() const noexcept = 0;
        virtual void set_streaming_radius(f32 radius) = 0;
        [[nodiscard]] virtual f32 get_streaming_radius() const noexcept = 0;

        // per-frame ---------------------------------------------------------------------------------------------------
        virtual void update(f32 delta_time) = 0;

        // region grid -------------------------------------------------------------------------------------------------
        [[nodiscard]] virtual glm::vec3 get_region_cell_size() const noexcept = 0;
        virtual void set_region_cell_size(const glm::vec3& size) = 0;
        virtual void reassign_all_entities() = 0;

        // hierarchy (defaults cover flat worlds) ----------------------------------------------------------------------
        [[nodiscard]] virtual std::vector<entity_id> root_entities() const { return {}; }
        [[nodiscard]] virtual std::vector<entity_id> children_of(entity_id /*id*/) const { return {}; }
        [[nodiscard]] virtual std::string_view entity_name(entity_id /*id*/) const { return {}; }
        [[nodiscard]] virtual bool has_children(entity_id /*id*/) const noexcept { return false; }
        virtual void set_parent(entity_id /*child*/, entity_id /*parent*/) {}
        [[nodiscard]] virtual entity_id parent_of(entity_id /*id*/) const noexcept { return INVALID_ENTITY; }

        // capability access -------------------------------------------------------------------------------------------
        // Concrete worlds opt into extra interfaces (i_world_scene, i_world_inspector) by
        // inheriting them. Callers reach them via `world.as<...>()`
        template<typename T> [[nodiscard]] T* as() noexcept { return dynamic_cast<T*>(this); }
        template<typename T> [[nodiscard]] const T* as() const noexcept { return dynamic_cast<const T*>(this); }

    };

}
