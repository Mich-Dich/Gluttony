#include "util/pch.h"
#include "util/jolt_pch.h"

#include <thread>

#include "plugin.h"
#include "physics_world.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics::jolt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    plugin::plugin() = default;


    plugin::~plugin() = default;

    // CLASS PUBLIC ====================================================================================================

    void plugin::on_load() {

        if (JPH::Factory::sInstance)
            return;

        // Process-global Jolt setup. Do this exactly once per plugin load. If you ever ship two physics plugins in one
        // process this needs to become refcounted; for now the plugin_manager's one-plugin-per-interface rule guarantees
        // a single owner
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        // Shared across every physics_world instance. Construction is single-threaded here (plugin load phase runs on
        // the main thread), so no coordination is needed
        const u32 hw = std::max(1u, std::thread::hardware_concurrency());
        m_job_system = GLT::create_unique_ref<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
            static_cast<int>(hw > 1 ? hw - 1 : 1));
    }


    void plugin::on_unload() {

        // Every physics_world instance should already be gone by the time we unload. If they aren't, that's a session
        // teardown ordering bug: physics worlds only exist as long as their game_session, and sessions die before
        // pre_application_shutdown. Reset the job system first so the next load starts clean
        m_job_system.reset();

        if (JPH::Factory::sInstance) {
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }
    }


    std::string_view plugin::backend_name() const noexcept { return "Jolt"; }


    std::expected<unique_ref<i_physics>, physics_error> plugin::instantiate(const glm::vec3& gravity) {

        if (!m_job_system)
            return std::unexpected(physics_error::invalid_argument);   // on_load hasn't run

        // A physics_world ctor allocates the PhysicsSystem, its body/constraint pools, and a TempAllocator. Those can
        // fail on OOM; catch and translate so callers see physics_error rather than a thrown exception
        try {

            return GLT::create_unique_ref<physics_world>(m_job_system.get(), gravity);

        } catch (const std::bad_alloc&) {

            return std::unexpected(physics_error::out_of_memory);
        }

    }

}
