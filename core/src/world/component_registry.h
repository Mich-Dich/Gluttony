
#pragma once

#include "world/component.h"
#include "world/entity.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {
    class i_world;
}

namespace GLT::world::component_registry {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // ---- shared per-type hash --------------------------------------------------------------------------------------

    // Written by the active world plugin's add<T> / make_descriptor<T>, read by core get<T> / entities_with<T>.
    // C++17+ inline template variable: single instance per T across the whole program. Zero means "not registered".
    template<typename T>
    inline u64                                          g_type_hash = 0;

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // stable hash (must match the plugin's component_name_hash) -------------------------------------------------------

    [[nodiscard]] inline constexpr u64 hash_of(std::string_view name) noexcept {

        u64 h = 0xcbf29ce484222325ULL;
        for (char c : name) {
            h ^= static_cast<u8>(c);
            h *= 0x100000001b3ULL;
        }
        return h;
    }

    // catalog ---------------------------------------------------------------------------------------------------------
    
    // Called by the world plugin's register.h when add<T>/remove_type<T> run. Kept non-template so the game plugin
    // doesn't have to reimplement catalog maintenance.
    void register_meta(const component::component_type_meta& meta);

    void unregister_meta(u64 hash);

    [[nodiscard]] const component::component_type_meta* find(u64 hash);

    [[nodiscard]] const component::component_type_meta* find(std::string_view name);

    [[nodiscard]] std::span<const component::component_type_meta> all();

    // TEMPLATE DECLARATION ============================================================================================
    
    // typed access ----------------------------------------------------------------------------------------------------

    template<typename T>
    [[nodiscard]] T* get(i_world& world, entity_id id);

    template<typename T>
    [[nodiscard]] std::vector<std::pair<entity_id, T*>> entities_with(i_world& world);

    // registration (declared here, defined by the active world plugin) ------------------------------------------------

    // The definition lives in <world_ecs_entt/register.h>. Game plugins include that header. This keeps the core
    // header clean of entt, while letting the game's translation unit instantiate the typed storage.
    template<typename T>
    void add(std::string_view name, std::string_view category, std::function<void(i_world&, entity_id)> draw = {},
        std::function<std::string(i_world&, entity_id)> summary = {}, std::vector<u64> required = {});

    template<typename T>
    bool remove_type();


    // CLASS DECLARATION ===============================================================================================

}

#include "component_registry.inl"
