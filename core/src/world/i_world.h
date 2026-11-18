#pragma once

#include <glm/glm.hpp>

#include "asset/type.h"
#include "asset/asset_ref.h"
#include "asset/region.h"
#include "world/entity.h"
#include "world/mobility.h"
#include "world/component.h"
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

        // Unique per world instance for the life of the process (never reused, unlike the address). Systems that keep
        // per-world state use it to notice that a world was replaced by another one at the same address
        [[nodiscard]] u64 instance_id() const noexcept { return m_instance_id; }

        // Underlying .glt_world asset. Invalid for a world created via create_empty()
        // until save_as() mints an asset for it
        [[nodiscard]] virtual const asset::asset_ref& asset() const noexcept = 0;
        [[nodiscard]] virtual asset::handle asset_handle() const noexcept = 0;

        // presets ----------------------------------------------------------------------------------------------------

        // @brief Spawns a fresh entity (possibly a small hierarchy) from a registered entity preset
        //
        // Looks up [preset_name] in GLT::world::preset_registry. The concrete world interprets the preset's component specs into
        // its own component types. Unknown components are logged and skipped, so a preset referencing a plugin's custom component
        // will still partially instantiate on a world that doesn't know about it
        //
        // @param preset_name  Registered preset name (see GLT::world::preset_registry)
        // @param parent        Optional parent for the root entity. INVALID_ENTITY leaves it unparented
        // @return The new root entity_id, or INVALID_ENTITY on failure
        virtual entity_id spawn_preset(std::string_view /*preset_name*/, entity_id /*parent*/ = INVALID_ENTITY) { return INVALID_ENTITY; }

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

        // world entity ------------------------------------------------------------------------------------------------
        // Every world owns exactly one world entity: never streamed, never part of a region, saved with the world asset,
        // cannot be despawned or parented. World-scope components (sky, sun light, ambient, plugin-defined world
        // settings) live on it; see component::scope. INVALID_ENTITY on worlds that don't support it
        [[nodiscard]] virtual entity_id world_entity() const noexcept { return INVALID_ENTITY; }
        [[nodiscard]] bool is_world_entity(entity_id id) const noexcept { return id.is_valid() && id == world_entity(); }

        // entity lifecycle -------------------------------------------------------------------------------------------
        [[nodiscard]] virtual entity_id spawn() = 0;
        virtual void despawn(entity_id id) noexcept = 0;
        [[nodiscard]] virtual bool alive(entity_id id) const noexcept = 0;

        // visibility --------------------------------------------------------------------------------------------------
        // Per-entity render visibility. Backed by the mesh component's [visible] flag on implementations
        // that have a renderable mesh. Entities with no mesh have nothing to hide and report [true].
        //
        // This is a leaf-level flag: it does NOT propagate down the hierarchy. Callers that want to hide
        // an entire subtree must walk it themselves (children_of / parent_of are already on this interface)
        [[nodiscard]] virtual bool is_visible(entity_id /*id*/) const { return true; }
        virtual void set_visible(entity_id /*id*/, bool /*visible*/) {}

        // Editor-side hiding, on top of the flag above and independent of it: the entity AND everything below it in the
        // hierarchy stops being drawn (meshes) and stops giving light. Nothing about the entity changes: the state is
        // not part of the world's data, is not saved with it and survives the entity being streamed out and in again.
        // Hidden entities are still handed to the renderers, marked as hidden (asset::mesh::INSTANCE_FLAG_HIDDEN,
        // light_snapshot::hidden), so tools can outline them. [is_hidden] is the entity's own flag, not the inherited one
        [[nodiscard]] virtual bool is_hidden(entity_id /*id*/) const { return false; }
        virtual void set_hidden(entity_id /*id*/, bool /*hidden*/) {}

        // procedural meshes -------------------------------------------------------------------------------------------
        // Draws [id] (a render::procedural_mesh_id, see render/procedural_mesh.h) on [entity] instead of a mesh asset. The
        // generator owns the mesh: it creates / updates / destroys it in GLT::render::procedural_meshes(); the world only
        // references it (several entities may share one). INVALID (0) removes the reference. The reference is runtime
        // state: it is NOT saved with the world (a generator recreates its meshes on load), but it survives editor undo.
        // Worlds without renderable meshes ignore it
        virtual void set_procedural_mesh(entity_id /*entity*/, u32 /*procedural_mesh_id*/) {}
        [[nodiscard]] virtual u32 get_procedural_mesh(entity_id /*entity*/) const { return 0; }

        // mesh assets / names (for systems that create ordinary, saved entities: baked terrain chunks, ...) -----------
        // Draws the static mesh asset [mesh] on [entity] (the mesh renderer; INVALID removes the mesh). Saved with the world
        virtual void set_mesh(entity_id /*entity*/, const asset::asset_ref& /*mesh*/) {}
        virtual void set_entity_name(entity_id /*entity*/, std::string_view /*name*/) {}

        // transient entities ------------------------------------------------------------------------------------------
        // Runtime-only entities a system creates and owns (terrain chunks, ...): never saved, part of no region (so never
        // streamed out by the world), not listed among the root entities (the outliner does not show them). Set it right
        // after spawn(); the owner despawns them. Children of a transient entity should be transient too
        virtual void set_transient(entity_id /*entity*/, bool /*transient*/) {}
        [[nodiscard]] virtual bool is_transient(entity_id /*entity*/) const { return false; }

        // spawn() + set_transient(), without ever touching the region system (no region is created for its cell)
        [[nodiscard]] virtual entity_id spawn_transient() {
            const entity_id id = spawn();
            set_transient(id, true);
            return id;
        }

        // mobility ----------------------------------------------------------------------------------------------------
        // Performance hint for render data caching, see world/mobility.h. Worlds without caching treat everything as movable
        [[nodiscard]] virtual mobility get_mobility(entity_id /*id*/) const { return mobility::movable; }
        virtual void set_mobility(entity_id /*id*/, mobility /*value*/) {}

        // Tells the world that component data of [id] was written directly (through a component pointer, e.g. an editor
        // property panel) instead of through this interface, so cached render data of [id] and its children is refreshed.
        // Setters on this interface do this themselves. Cheap: the work is deferred to the next gather
        virtual void mark_changed(entity_id /*id*/) {}

        // transform ---------------------------------------------------------------------------------------------------
        [[nodiscard]] virtual glm::vec3 get_local_position(entity_id id) const = 0;
        [[nodiscard]] virtual glm::vec3 get_local_rotation(entity_id id) const = 0;
        virtual void translate_local(entity_id id, const glm::vec3& local_delta) = 0;
        virtual void rotate_local(entity_id id, const glm::vec3& euler_delta) = 0;
        [[nodiscard]] virtual glm::quat get_local_rotation_q(entity_id id) const = 0;
        virtual void set_local_rotation_q(entity_id id, const glm::quat& rotation) = 0;
        [[nodiscard]] virtual glm::vec3 get_local_scale(entity_id id) const = 0;
        virtual void set_local_scale(entity_id id, const glm::vec3& scale) = 0;

        // @brief World transform of [id]: its local transform under its parents' (identity for an invalid id). The default
        //        walks parent_of() with the local getters; worlds that know more (e.g. entities that do not inherit their
        //        parent's transform) override it
        [[nodiscard]] virtual glm::mat4 get_world_transform(entity_id id) const {

            glm::mat4 world(1.f);
            entity_id walk = id;
            for (u32 guard = 0; walk.is_valid() && alive(walk) && guard < 256; ++guard) {
                world = glm::translate(glm::mat4(1.f), get_local_position(walk)) * glm::mat4_cast(get_local_rotation_q(walk))
                    * glm::scale(glm::mat4(1.f), get_local_scale(walk)) * world;
                walk = parent_of(walk);
            }
            return world;
        }

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

        // snapshots (editor undo / redo) ------------------------------------------------------------------------------
        // An entity snapshot is an opaque blob holding every persisted component of the given entities (incl. their parent
        // link). Dead ids are skipped. Worlds that don't support it return an empty blob (= the editor can't undo world edits)
        [[nodiscard]] virtual std::vector<std::byte> snapshot_entities(std::span<const entity_id> /*ids*/) const { return {}; }

        // Puts every entity stored in `blob` back into exactly the stored state. Alive entities are replaced, despawned
        // ones are re-created under the SAME entity_id, so anything else that references them stays valid. Parent/child
        // links, region membership and physics bodies are rebuilt. Returns the restored ids
        virtual std::vector<entity_id> restore_entities(std::span<const std::byte> /*blob*/) { return {}; }

        // Type-erased access. Returns the storage for `hash` in *this world,
        // creating it lazily on first call. Null if no such type is registered.
        [[nodiscard]] virtual component::i_storage* storage_for(u64 hash) = 0;

        // capability access -------------------------------------------------------------------------------------------
        // Concrete worlds opt into extra interfaces (i_world_scene, i_world_inspector) by
        // inheriting them. Callers reach them via `world.as<...>()`
        template<typename T> [[nodiscard]] T* as() noexcept { return dynamic_cast<T*>(this); }
        template<typename T> [[nodiscard]] const T* as() const noexcept { return dynamic_cast<const T*>(this); }

    private:

        inline static std::atomic<u64>                  s_next_instance_id{ 0 };
        const u64                                       m_instance_id = ++s_next_instance_id;

    };

}
