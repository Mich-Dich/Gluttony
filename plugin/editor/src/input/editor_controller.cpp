
#include "util/pch.h"
#include "editor_controller.h"

#include <layer/layer_stack.h>
#include <application.h>
#include <world/i_world_scene.h>

#include "settings/editor_settings.h"



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

    editor_controller::editor_controller(GLT::game::game_session* session)
        : controller(session) {

        using namespace GLT::input::input_manager_default;

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


    editor_controller::~editor_controller() = default;

    // CLASS PUBLIC ====================================================================================================

    void editor_controller::update(const f32 delta_time) {
        controller::update(delta_time);

        if (!get_input_enabled())
            return;

        if (!m_session)
            return;

        auto* world = m_session->get_world();
        if (!world)
            return;

        auto* scene = world->as<GLT::world::i_world_scene>();
        if (!scene)
            return;

        // The editor controller follows whatever the scene currently marks as
        // active camera. Set it once per frame so get_target() is meaningful to
        // anyone reading it (gizmo, HUD, inspector…)
        const auto target = scene->get_active_camera();
        if (!target.is_valid())
            return;
        set_target(target);

        auto& camera = GLT::editor::editor_settings::get().camera;

        const f32 scroll = get_axis(m_scroll_action_handle);
        if (scroll != 0.0f && camera.scroll_changes_speed) {

            camera.fly_speed += (scroll * (camera.fly_speed * 0.2f));
            camera.fly_speed = GLT::math::clamp(camera.fly_speed, 0.01f, 5.f);
        }

        const f32 speed = camera.fly_speed;
        const glm::vec3 move = get_axis3d(m_move_action_handle);
        if (glm::length(move) > 0.f) {

            const glm::vec3 local_delta{move.y * speed, move.z * speed, -move.x * speed};
            world->translate_local(target, local_delta);
        }

        const glm::vec2 look = get_axis2d(m_look_action_handle);
        if (glm::length(look) > 0.f) {

            const f32 pitch_sign = camera.invert_y ? -1.f : 1.f;
            const glm::vec3 euler_delta{
                glm::radians(look.y * camera.look_sensitivity * pitch_sign),    // pitch around +X
                glm::radians(look.x * camera.look_sensitivity),                 // yaw around +Y
                0.f
            };
            world->rotate_local(target, euler_delta);
        }
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
