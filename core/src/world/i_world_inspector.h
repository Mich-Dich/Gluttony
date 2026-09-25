
#pragma once

#include <string_view>
#include <functional>
#include <vector>

#include "world/entity.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {
    class i_world_plugin;
}

namespace GLT::world {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // One entry in the inspector's component registry. Descriptors are
    // registered once at plugin init; the editor enumerates them to build
    // the "Add Component" menu and to render each component on an entity.
    //
    // `draw` is intentionally typed loosely (entity_id in, void out) - the
    // editor supplies its own ImGui context and service locators, and the
    // callback is free to use them however it wants.
    struct component_descriptor {

        u64                                             hash{ 0 };              // same FNV-1a used by the codec
        std::string_view                                name;                  // "Transform", "Mesh Renderer", ...
        std::string_view                                category;              // "Core", "Rendering", "Audio", ...

        // Present on an entity? Called per-frame by the details panel; must be cheap.
        std::function<bool(entity_id)>                  has;

        // Default-construct and attach. Called from the "Add Component" menu.
        std::function<void(entity_id)>                  add;

        // Detach. Called from the "..." context menu.
        std::function<void(entity_id)>                  remove;

        // Renders the component's editor UI. Empty = "this component is
        // runtime-only" (no panel shown, but it still appears in the "has"
        // list for debugging). The editor does not wrap this in any child,
        // separator, or header - the callback owns its own presentation.
        std::function<void(entity_id)>                  draw;

        // Optional: can this component be removed? Defaults to true. Some
        // components are structural (hierarchy) and shouldn't be yanked out
        // from under the outliner.
        std::function<bool(entity_id)>                  can_remove;

        // Optional: return a short one-line preview for the outliner
        // (e.g. "1.5 KB" or "body_lod1"). Empty = nothing shown.
        std::function<std::string(entity_id)>           summary;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // The inspector interface. Implementations live inside the concrete
    // world plugin (it has the entt registry, the slot table, and the
    // component type info). The editor only sees this.
    //
    // Get it via `world_plugin->as<i_world_inspector>()`. A world plugin
    // that doesn't offer one returns nullptr and the editor's Outliner /
    // Details panels show a graceful placeholder.
    class i_world_inspector {
    public:

        virtual ~i_world_inspector() = default;

        // ---- registry -----------------------------------------------------------------------------------------------

        [[nodiscard]] virtual std::span<const component_descriptor> descriptors() const noexcept = 0;


        // Which descriptors are present on `id`? Returned in registry order.
        [[nodiscard]] virtual std::vector<const component_descriptor*> components_on(entity_id id) const = 0;

        // ---- mutation -----------------------------------------------------------------------------------------------

        // Sugar for descriptor->add. Returns false if the descriptor isn't
        // registered, the entity is dead, or the component is already present.
        virtual bool add_component(entity_id id, u64 component_hash) = 0;


        virtual bool remove_component(entity_id id, u64 component_hash) = 0;

        // ---- bulk ops ----------------------------------------------------------------------------------------------

        // Copy every component from `from` to `to`. Used for duplication.
        // Components whose descriptors aren't registered are silently skipped.
        virtual void copy_components(entity_id from, entity_id to) = 0;

    };

}
