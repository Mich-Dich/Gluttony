#pragma once

#include "util/pch.h"
#include "world/i_world.h"
#include "world/component_registry.h"

// The component that binds scripts to an entity (scripting.md §14). Plain authored data, saved with the world: which
// script classes run on the entity, under which instance name, with which editor settings. The scripting plugin
// registers it with the world and its host keeps live instances in sync with it (reconciliation).
//
// Changing it from code (the editor does the same):
//
//     if (auto* comp = GLT::script::ensure_bindings(world, entity))
//         comp->bindings.push_back({ .script_name = "scripts.spinner", .instance_name = "spinner" });
//     // a running host picks it up on its next update (or host->refresh(entity) right away)



namespace GLT::script {

    // CONSTANTS =======================================================================================================

    // Stored in world files: keep it
    inline constexpr std::string_view                   BINDING_COMPONENT_NAME = "script_bindings";

    // TYPES ===========================================================================================================

    struct script_binding {

        std::string                                     script_name{};      // registered class name ("scripts.spinner")
        std::string                                     instance_name{};    // unique on the entity ("spinner")
        bool                                            enabled = true;     // false: no instance is created
        std::vector<std::byte>                          editor_settings{};  // [[=editor]] fields (script_fields.h blob)
    };


    struct script_binding_component {

        std::vector<script_binding>                     bindings{};
    };


    // FUNCTION DECLARATION ============================================================================================

    // The bindings of [entity], or null (no component, or no scripting plugin loaded). Looks the component up by name,
    // so it works from any module (component_registry::get<T> only knows types registered in the same module)
    [[nodiscard]] inline script_binding_component* find_bindings(world::i_world& world, const world::entity_id entity) {

        auto* storage = world.storage_for(world::component_registry::hash_of(BINDING_COMPONENT_NAME));
        return storage ? static_cast<script_binding_component*>(storage->get(entity)) : nullptr;
    }


    // Like find_bindings(), adds the component first if the entity has none
    [[nodiscard]] inline script_binding_component* ensure_bindings(world::i_world& world, const world::entity_id entity) {

        auto* storage = world.storage_for(world::component_registry::hash_of(BINDING_COMPONENT_NAME));
        if (!storage)
            return nullptr;
        if (!storage->has(entity))
            storage->add(entity);
        return static_cast<script_binding_component*>(storage->get(entity));
    }

}
