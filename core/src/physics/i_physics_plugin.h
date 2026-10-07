
#pragma once

#include "plugin_system/plugin_manager.h"
#include "world/i_world.h"          // entity_id, handle



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics {
    class i_physics_plugin;
}

namespace GLT::physics {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================
    
    enum class motion_type : u8 {
        static_body = 0,
        dynamic_body,
        kinematic_body
    };
    

    // Collision layer/group for filtering
    struct collision_profile {
        u32                                         object_layer{};
        u32                                         broadphase_layer{};
        u32                                         group_filter{};          // optional bitmask
    };


    struct ray_cast_result {
        bool                                        hit{};
        f32                                         distance{};
        glm::vec3                                   position{};
        glm::vec3                                   normal{};
        world::entity_id                            entity{ world::INVALID_ENTITY };
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // convenience accessor
    FORCE_INLINE_R ref<i_physics_plugin> get_ref() {
        return plugin_manager::get_plugin_ref<i_physics_plugin>(plugin_manager::interface::physics);
    }

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class i_physics_plugin : public plugin_manager::i_plugin {
    public:

        // lifecycle ------------------------------------------------------------------
        virtual void init(f32 gravity_y = -9.81f) = 0;
        virtual void shutdown() = 0;

        // per-frame step (called from game loop)
        virtual void step(f32 delta_time, u32 collision_steps = 1) = 0;

        // TODO: implement

    };

}
