
#include "util/pch.h"
#include "editor_controller.h"

#include <layer/layer_stack.h>
#include <application.h>
#include <world/world_layer.h>
#include <world/i_world_scene.h>

#include "util/context.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor::input {

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

    editor_controller::editor_controller() {

        using namespace GLT::input::input_manager_default;

        m_world = GLT::world::manager::get_ref();
        ASSERT(m_world, "", "editor_controller: world plugin not available")

        action move{
            .name = "move",                       // debug only now
            .type = value_type::axis3d,
            .bindings = {
                // W -> +X
                { source::key(GLT::key_code::key_W),
                    { },
                    { trigger::key_down() }
                },

                // S -> -X
                { source::key(key_code::key_S),
                    { modifier::invert() },
                    { trigger::key_down() }
                },

                // A -> -Y
                { source::key(key_code::key_A),
                    { modifier::axis(1), modifier::invert() },
                    { trigger::key_down() }
                },

                // D -> +Y
                { source::key(key_code::key_D),
                    { modifier::axis(1) },
                    { trigger::key_down() }
                },

                // space -> +Z
                { source::key(key_code::key_space),
                    { modifier::axis(2) },
                    { trigger::key_down() }
                },

                // Ctrl -> -Z
                { source::key(key_code::key_left_shift),
                    { modifier::axis(2), modifier::invert() },
                    { trigger::key_down() }
                },
            }
        };
        m_move_action_handle = add_action(std::move(move));

        action look{
            .name = "look",
            .type = value_type::axis2d,
            .bindings = {                               // x/y mouse -> x/y rotation camera
                { source::mouse_move(-1),               // invert both axis
                    { },
                    { }
                },
            }
        };
        m_look_action_handle = add_action(std::move(look));

        action scroll{
            .name = "scroll",
            .type = value_type::axis1d,
            .bindings = {
                // x/y mouse -> x/y rotation camera
                { source::mouse_wheel(),
                    { },
                    { }
                },
            }
        };
        m_scroll_action_handle = add_action(std::move(scroll));
    }


    editor_controller::~editor_controller() { }

    // CLASS PUBLIC ====================================================================================================

    void editor_controller::update(const f32 delta_time) {
        controller::update(delta_time);

        if (!GLT::editor::context::get().get_viewport_interacted())
            return;

        if (!m_world)
            return;

        auto* scene = m_world->as<GLT::world::i_world_scene>();                 // Resolve the active camera
        if (!scene)
            return;

        const GLT::world::entity_id camera_entity = scene->get_active_camera();
        if (!camera_entity.is_valid())
            return;

        const f32 scroll = get_axis(m_scroll_action_handle);
        if (scroll != 0.0f) {

            m_move_speed += (scroll * (m_move_speed * 0.2f));
            m_move_speed = GLT::math::clamp(m_move_speed, 0.01f, 5.f);
        }

        const glm::vec3 move = get_axis3d(m_move_action_handle);
        if (glm::length(move) > 0.f) {

            const glm::vec3 local_delta{move.y * m_move_speed, move.z * m_move_speed, -move.x * m_move_speed};
            m_world->translate_local(camera_entity, local_delta);
        }

        const glm::vec2 look = get_axis2d(m_look_action_handle);
        if (glm::length(look) > 0.f) {

            const glm::vec3 euler_delta{
                glm::radians(look.y * m_look_sensitivity),  // pitch around +X
                glm::radians(look.x * m_look_sensitivity),  // yaw around +Y
                0.f
            };
            m_world->rotate_local(camera_entity, euler_delta);
        }
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
