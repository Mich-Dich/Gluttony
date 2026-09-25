#pragma once

#include "util/data_structures/UUID.h"
#include "asset/type.h"
#include "asset/region.h"
#include "world/entity.h"
#include "plugin_system/plugin_manager.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {
    class i_world_plugin;
}

namespace GLT::world {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    namespace manager {

        FORCE_INLINE_R ref<GLT::world::i_world_plugin> get_ref() {

            return GLT::plugin_manager::get_plugin_ref<GLT::world::i_world_plugin>(plugin_manager::interface::world);
        }

    }

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // The engine-side contract for "a world / scene / map".
    //
    // Deliberately minimal. It knows nothing about components, archetypes, views, or inheritance. It only knows that:
    //   - entities exist and can be spawned / despawned with a stable handle
    //   - regions exist, have a non-uniform AABB, and can be streamed in/out
    //   - a single "streaming anchor" (usually the active camera) drives which regions are hot
    //
    // Everything else (transform, health, AI, whatever) is layered on top by concrete plugins that COUPLE to a specific world implementation.
    //
    // Implementations that want to expose richer access (e.g. entt::registry&) do so via as<T>() - not by widening this interface.
    class i_world_plugin : public GLT::plugin_manager::i_plugin {
    public:

        virtual ~i_world_plugin() = default;

        // world lifecycle ---------------------------------------------------------------------------------------------

        // Bind a world asset to this plugin. Any previously bound world is unloaded first. 
        // Returns an error if the handle isn't a world asset or its regions couldn't be indexed.
        [[nodiscard]] virtual std::expected<void, GLT::asset::load_error> load_world(const GLT::asset::handle world) = 0;


        virtual void unload_world() noexcept = 0;


        [[nodiscard]] virtual GLT::asset::handle world_handle() const noexcept = 0;

        // entity lifecycle --------------------------------------------------------------------------------------------

        // Allocate a fresh entity. The returned id is guaranteed alive until despawn().
        [[nodiscard]] virtual entity_id spawn() = 0;


        // Idempotent. Despawning an already-dead id is a no-op.
        virtual void despawn(const entity_id id) noexcept = 0;


        [[nodiscard]] virtual bool alive(entity_id id) const noexcept = 0;

        // region queries ----------------------------------------------------------------------------------------------

        [[nodiscard]] virtual std::span<const GLT::asset::region::region> regions() const noexcept = 0;


        // Returns the region whose AABB contains `point`, or nullptr. If regions overlap, the smallest (most specific) match wins.
        [[nodiscard]] virtual const GLT::asset::region::region* region_at(const glm::vec3& point) const noexcept = 0;


        [[nodiscard]] virtual bool is_region_active(const GLT::UUID id) const noexcept = 0;


        // Manual override. Streaming normally toggles this; exposed for cinematics, save games, and the editor.
        virtual void set_region_active(const GLT::UUID id, const bool active) = 0;

        // streaming ---------------------------------------------------------------------------------------------------

        // The point from which regions become hot. Usually the active camera. The plugin compares this against every region's bounds each tick.
        virtual void set_streaming_anchor(const glm::vec3& position) = 0;


        [[nodiscard]] virtual glm::vec3 get_streaming_anchor() const noexcept = 0;


        // Regions whose (bounds expanded by `radius`) contain the anchor are kept hot. Radius is in world units; 0 means "only the containing region".
        virtual void set_streaming_radius(const f32 radius) = 0;


        [[nodiscard]] virtual f32 get_streaming_radius() const noexcept = 0;

        // update ------------------------------------------------------------------------------------------------------

        // Advance simulation. Called once per frame by world_layer. The plugin is expected to perform its own streaming 
        // activation/deactivation pass here using the current anchor.
        virtual void update(const f32 delta_time) = 0;

        // hierarchy ---------------------------------------------------------------------------------------------------

        // Tree view over the world's entities. Not every world needs a hierarchy — a flat implementation can leave the defaults
        // and the outliner will show a single-level list.
        // The editor consumes this; the world plugin does not know who's asking.

        // Snapshot of every live entity that has no parent (or a parent that doesn't exist — orphans are treated as roots so they're always reachable).
        [[nodiscard]] virtual std::vector<entity_id> root_entities() const { return {}; }


        // Direct children of `id`, in a stable order. Empty if the entity has none or the implementation has no hierarchy.
        [[nodiscard]] virtual std::vector<entity_id> children_of(entity_id /*id*/) const { return {}; }


        // Borrowed for the current frame only. Valid until the next structural change (spawn / despawn / reparent / rename).
        // Copy if you need to keep it. Empty view = "no name"; callers should substitute a fallback.
        [[nodiscard]] virtual std::string_view entity_name(entity_id /*id*/) const { return {}; }


        [[nodiscard]] virtual bool has_children(entity_id /*id*/) const noexcept { return false; }


        // Reparent. INVALID_ENTITY detaches. Implementations are required to be cycle-safe: parenting an ancestor under its
        // own descendant must be a no-op, not a corruption.
        virtual void set_parent(entity_id /*child*/, entity_id /*parent*/) {}


        // Returns the parent of `child`, or INVALID_ENTITY if it has none.
        // Implementations that don't have a hierarchy can leave the default
        // (everything is a root).
        [[nodiscard]] virtual entity_id parent_of(entity_id /*id*/) const noexcept { return INVALID_ENTITY; }

        // capability cast ---------------------------------------------------------------------------------------------

        // Escape hatch for plugins that want to reach past this interface to a concrete implementation
        // (e.g. `world.as<ecs_world>()` -> entt::registry&).
        //
        // Rule of thumb:
        //   - Engine-shipped, perf-sensitive plugins (actors, scripting, netcode) couple directly to the concrete type and 
        //     call as<T>() once on init, caching the result. They do NOT call as<T>() per frame.
        //   - Third-party / swappable plugins stay on i_world_plugin only.
        template<typename T>
        requires std::is_polymorphic_v<T>
        [[nodiscard]] T* as() noexcept { return dynamic_cast<T*>(this); }


        // and the const version:
        template<typename T>
        requires std::is_polymorphic_v<T>
        [[nodiscard]] const T* as() const noexcept { return dynamic_cast<const T*>(this); }

    };

}
