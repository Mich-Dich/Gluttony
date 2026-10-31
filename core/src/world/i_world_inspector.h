
#pragma once

#include "world/entity.h"
#include "world/component.h"



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

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // The inspector interface. Implementations live inside the concrete world plugin (it has the entt registry, the slot
    // table, and the component type info). The editor only sees this
    //
    // Get it via `world_plugin->as<i_world_inspector>()`. A world plugin that doesn't offer one returns nullptr and the
    // editor's Outliner / Details panels show a graceful placeholder
    class i_world_inspector {
    public:

        virtual ~i_world_inspector() = default;

        // ---- registry -----------------------------------------------------------------------------------------------

        [[nodiscard]] virtual std::span<const component::descriptor> descriptors() const noexcept = 0;


        // Which descriptors are present on `id`? Returned in registry order
        [[nodiscard]] virtual std::vector<const component::descriptor*> components_on(entity_id id) const = 0;

        // ---- mutation -----------------------------------------------------------------------------------------------

        // Sugar for descriptor->add. Returns false if the descriptor isn't
        // registered, the entity is dead, or the component is already present
        virtual bool add_component(entity_id id, u64 component_hash) = 0;


        virtual bool remove_component(entity_id id, u64 component_hash) = 0;

        // ---- bulk ops ----------------------------------------------------------------------------------------------

        // Copy every component from `from` to `to`. Used for duplication
        // Components whose descriptors aren't registered are silently skipped
        virtual void copy_components(entity_id from, entity_id to) = 0;

        // NOTE: typed access (`get<T>`, `entities_with<T>`) lives on world/component_registry.h. It is not virtual;
        // it is instantiated in the caller's TU against the shared per-type hash, so it works across the plugin
        // boundary without a vtable entry per T

    };

}
