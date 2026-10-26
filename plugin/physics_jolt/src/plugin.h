#pragma once

#include <physics/i_physics_plugin.h>


// FORWARD DECLARATIONS ================================================================================================

namespace JPH {
    class JobSystemThreadPool;
}

namespace GLT::physics::jolt {
    class plugin;
}

namespace GLT::physics::jolt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Jolt backend for i_physics_plugin
    //
    // Holds only process-wide state: the Jolt bootstrap (RegisterTypes / Factory) and the shared JobSystemThreadPool that
    // every physics_world instance borrows. Everything simulation-scoped lives on physics_world — see physics_world.h
    class plugin final : public GLT::physics::i_physics_plugin {
    public:

        plugin();
        ~plugin() override;

        // i_plugin ----------------------------------------------------------------------------------------------------
        void on_load() override;
        void on_unload() override;

        // identity ----------------------------------------------------------------------------------------------------
        [[nodiscard]] std::string_view backend_name() const noexcept override;

        // factory -----------------------------------------------------------------------------------------------------
        [[nodiscard]] std::expected<unique_ref<i_physics>, physics_error>
            instantiate(const glm::vec3& gravity) override;

    private:

        GLT::unique_ref<JPH::JobSystemThreadPool>       m_job_system;

    };

}
