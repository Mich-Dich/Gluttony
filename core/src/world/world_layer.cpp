
#include "util/pch.h"
#include "world_layer.h"

#include "world/object/camera.h"



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
        : layer("input") {}


    world_layer::~world_layer() = default;

    // CLASS PUBLIC ====================================================================================================

    void world_layer::update(const f32 delta_time) {
        
        if (m_controller)
            m_controller->update(delta_time);
    }


    void world_layer::render_imgui(const f32 /*delta_time*/) { }


    void world_layer::create_editor_camera(const glm::vec3 position, const glm::vec3 rotation) {

        m_editor_camera = GLT::create_ref<GLT::world::camera>();
        m_editor_camera->set_position(position);
        m_editor_camera->rotate(rotation);
    }


    void soft_create_editor_camera(const glm::vec3 position = glm::vec3{ 0.f }, const glm::vec3 rotation = glm::vec3{ 0.f }) {

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
