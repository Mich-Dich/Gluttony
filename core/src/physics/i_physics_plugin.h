#pragma once

#include <expected>
#include <glm/glm.hpp>

#include "physics/type.h"
#include "physics/i_physics.h"
#include "plugin_system/plugin_manager.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics {
    class i_physics_plugin;
}

namespace GLT::physics {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // @brief Hot-path accessor: cached raw pointer, thread-safe, no refcount traffic. Cache the result and compare
    // `plugin_manager::get_generation()` for invalidation
    // @return nullptr if the physics plugin is not loaded
    FORCE_INLINE_R i_physics_plugin* get_ptr() noexcept {

        return GLT::plugin_manager::get_plugin_raw<i_physics_plugin>(GLT::plugin_manager::interface::physics);
    }


    // @brief Owning reference for cases where you need to keep the plugin alive across unload phases
    FORCE_INLINE_R ref<i_physics_plugin> get_ref() {

        return GLT::plugin_manager::get_plugin_ref_unsafe<i_physics_plugin>(GLT::plugin_manager::interface::physics);
    }

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Factory for i_physics instances
    //
    // One per application. All step / body / shape / query operations now live on i_physics —
    // this interface only knows how to mint a physics world. The plugin itself is free of
    // per-session state
    class i_physics_plugin : public plugin_manager::i_plugin {
    public:
        ~i_physics_plugin() override = default;

        template<typename T> requires std::is_polymorphic_v<T>
        [[nodiscard]] T* as() noexcept { return dynamic_cast<T*>(this); }

        template<typename T> requires std::is_polymorphic_v<T>
        [[nodiscard]] const T* as() const noexcept { return dynamic_cast<const T*>(this); }

        // identity ----------------------------------------------------------------------------------------------------

        [[nodiscard]] virtual std::string_view backend_name() const noexcept = 0;

        // factory -----------------------------------------------------------------------------------------------------

        // @brief Constructs a fresh physics instance
        //
        // The returned instance owns its own PhysicsSystem, body/shape/contact caches, and gravity.
        // Destroying the unique_ref performs the entire shutdown — no explicit teardown call is needed
        //
        // @param gravity  Initial gravity vector (default: Earth)
        // @return Owning reference to the new instance, or a physics_error
        [[nodiscard]] virtual std::expected<unique_ref<i_physics>, physics_error>
            instantiate(const glm::vec3& gravity = { 0.f, -9.81f, 0.f }) = 0;
    };

}
