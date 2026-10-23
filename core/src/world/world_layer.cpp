
#include "util/pch.h"
#include "world_layer.h"

#include "application.h"
#include "event/event_bus.h"
#include "world/i_world.h"
#include "world/i_world_scene.h"
#include "render/i_renderer.h"
#include "render/i_scene_renderer.h"
#include "asset/i_asset_registry.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {

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

    world_layer::world_layer()
        : layer("input") {

        m_world = GLT::world::manager::get_ref();
    }


    world_layer::~world_layer() { }

    // CLASS PUBLIC ====================================================================================================

    void world_layer::update(const f32 delta_time) {

        if (m_controller)
            m_controller->update(delta_time);

        if (!m_world)
            return;

        auto* world_scene = m_world->as<GLT::world::i_world_scene>();

        // Sample the camera before the world tick so the streaming pass (which
        // runs inside m_world->update) sees this frame's anchor.
        GLT::world::camera_snapshot cam{};
        const bool has_camera = world_scene && world_scene->get_camera_view(cam);

        if (has_camera)
            m_world->set_streaming_anchor(cam.position);

        m_world->update(delta_time);

        if (!world_scene)
            return;

        auto renderer = GLT::render::renderer::get_ref();
        if (!renderer)
            return;

        world_scene->gather_scene(m_scene_buffer);
        world_scene->gather_lights(m_light_buffer);

        auto* main_world_scene_renderer = GLT::application::get().get_main_world_scene_renderer();
        main_world_scene_renderer->submit_scene(m_scene_buffer);
        main_world_scene_renderer->submit_lights(m_light_buffer);

        if (has_camera)
            main_world_scene_renderer->set_active_camera(cam);
    }


    void world_layer::render_imgui(const f32 /*delta_time*/) { }


    void world_layer::set_world(ref<GLT::world::i_world_plugin> world) {

        VALIDATE(world, return, "", "Provided world plugin is invalid")
        m_world = std::move(world);
    }


    void world_layer::set_controller(ref<GLT::world::controller> ctrl) {

        VALIDATE(ctrl, return, "", "Provided Controller is invalid")
        m_controller = ctrl;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
