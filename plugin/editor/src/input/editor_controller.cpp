
#include "util/pch.h"
#include "editor_controller.h"

#include <layer/layer_stack.h>
#include <application.h>
#include <world/world_layer.h>
#include <world/object/camera.h>

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

        auto* world = GLT::application::get().get_layer_stack_ref().get<GLT::world::world_layer>();
        m_camera = world->get_editor_camera();
        ASSERT(m_camera, "", "World layer does not have editor")

        action move{
            .name = "move",                       // debug only now
            .type = value_type::axis3d,
            .bindings = {
                // W -> +Y
                { source::key(GLT::key_code::key_W),
                    { },
                    { trigger::key_down() }
                },

                // S -> -Y
                { source::key(key_code::key_S),
                    { modifier::invert() },
                    { trigger::key_down() }
                },

                // A -> -X
                { source::key(key_code::key_A),
                    { modifier::axis(1), modifier::invert() },
                    { trigger::key_down() }
                },

                // D -> +X
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
            .bindings = {
                // x/y mouse -> x/y rotation camera
                { source::mouse_move(),
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

        const f32 scroll = get_axis(m_scroll_action_handle);
        if (scroll != 0.0f) {

            m_move_speed += (scroll * (m_move_speed * 0.2f));
            m_move_speed = GLT::math::clamp(m_move_speed, 0.01f, 5.f);
        }

        const glm::vec3 move = get_axis3d(m_move_action_handle);
        if (glm::length(move) > 0.f)
            m_camera->move(glm::vec3(move.x * m_move_speed, move.y * m_move_speed, move.z * m_move_speed));

        const glm::vec2 look = get_axis2d(m_look_action_handle);
        if (glm::length(look) > 0.f)
            m_camera->rotate(look.y, look.x, 0.0f);
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
