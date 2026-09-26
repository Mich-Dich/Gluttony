
#include "util/pch.h"
#include "world_layer.h"

#include "event/event_bus.h"
#include "world/object/camera.h"
#include "world/i_world.h"
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

        if (m_editor_camera)        // The editor camera is the streaming anchor FOR NOW
            m_world->set_streaming_anchor(m_editor_camera->get_position());

        m_world->update(delta_time);
    }


    void world_layer::render_imgui(const f32 /*delta_time*/) { }


    void world_layer::set_world(ref<GLT::world::i_world_plugin> world) {

        VALIDATE(world, return, "", "Provided world plugin is invalid")
        m_world = std::move(world);
    }


    void world_layer::create_editor_camera(const glm::vec3 position, const glm::vec3 rotation) {

        m_editor_camera = GLT::create_ref<GLT::world::camera>();
        m_editor_camera->set_position(position);
        m_editor_camera->rotate(rotation);
    }


    void world_layer::soft_create_editor_camera(const glm::vec3 position, const glm::vec3 rotation) {

        if (!m_editor_camera)
            create_editor_camera(position, rotation);
    }


    void world_layer::set_controller(ref<GLT::world::controller> ctrl) {

        VALIDATE(ctrl, return, "", "Provided Controller is invalid")
        m_controller = ctrl;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
