
#pragma once

#include "world/controller.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {
    class camera;
}

namespace GLT::world {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class world_layer final : public layer {
    public:

        world_layer();

        ~world_layer();

        DEFAULT_GETTER(ref<GLT::world::controller>,             controller)
        DEFAULT_GETTER(ref<GLT::world::camera>,                 editor_camera)


        void update(const f32 delta_time) override;


        void render_imgui(const f32 delta_time) override;


        // Hand the layer ownership of a controller. Accepts anything derived
        // from controller by implicit unique_ptr conversion.
        template<typename controller_type, typename... args>
        requires std::derived_from<controller_type, GLT::world::controller>
        void set_controller(args&&... arguments);


        void create_editor_camera(const glm::vec3 position = glm::vec3{ 0.f }, const glm::vec3 rotation = glm::vec3{ 0.f });
        
        
        void soft_create_editor_camera(const glm::vec3 position = glm::vec3{ 0.f }, const glm::vec3 rotation = glm::vec3{ 0.f });


        void set_controller(ref<GLT::world::controller> ctrl);

    private:

        ref<GLT::world::controller>                             m_controller{};
        ref<GLT::world::camera>                                 m_editor_camera{};

    };

}

#include "world_layer.inl"
