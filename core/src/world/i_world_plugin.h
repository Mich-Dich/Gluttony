#pragma once

#include "asset/type.h"
#include "asset/asset_ref.h"
#include "plugin_system/plugin_manager.h"
#include "world/component.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {
    class i_world;
    class i_world_plugin;
}

namespace GLT::world {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    namespace manager {

        // @brief Hot-path accessor: cached raw pointer, thread-safe, no refcount traffic. Cache the result and compare
        // `plugin_manager::get_generation()` for invalidation
        // @return nullptr if the audio plugin is not loaded
        FORCE_INLINE_R i_world_plugin* get_ptr() noexcept {

            return GLT::plugin_manager::get_plugin_raw<i_world_plugin>(GLT::plugin_manager::interface::world);
        }


        // @brief Owning reference for cases where you need to keep the plugin alive across unload phases
        FORCE_INLINE_R ref<i_world_plugin> get_ref() {

            return GLT::plugin_manager::get_plugin_ref_unsafe<i_world_plugin>(GLT::plugin_manager::interface::world);
        }

    }

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Factory for i_world instances
    //
    // One per application. All entity / region / streaming operations now live on i_world —
    // this interface is deliberately tiny: it only knows how to mint a world
    //
    // World instances are RAII-owned by their callers. Dropping the unique_ref destroys the instance and its state
    // The plugin does not track live instances (game_layer already enumerates sessions -> worlds, which is the tracker the editor needs)
    class i_world_plugin : public GLT::plugin_manager::i_plugin {
    public:
        virtual ~i_world_plugin() = default;

        // ---- factory --------------------------------------------------------

        // Bind a world asset and return a fully initialised instance
        [[nodiscard]] virtual std::expected<unique_ref<i_world>, asset::load_error> instantiate(asset::asset_ref asset) = 0;


        // Resolve `path` via the registry (content-dir-relative), then instantiate
        [[nodiscard]] virtual std::expected<unique_ref<i_world>, asset::load_error> instantiate_from_path(const std::filesystem::path& path) = 0;


        // Forwards to world.save() / world.save_as(path). Provided so callers that only have the plugin don't need to include i_world.h
        [[nodiscard]] virtual std::expected<void, asset::load_error> save_world(i_world& world) = 0;


        [[nodiscard]] virtual std::expected<void, asset::load_error> save_world_as(i_world& world, const std::filesystem::path& path) = 0;


        // Called by `component_registry::add<T>()`. The plugin stores the descriptor and pushes it into every live world
        virtual void register_component_type(const component::type_descriptor& td) = 0;


        virtual bool unregister_component_type(u64 hash) = 0;

    };

}
