
#pragma once

#include "asset/mesh.h"
#include "world/controller.h"
#include "event/application_event.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {
    class camera;
    class i_world_plugin;
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
        DEFAULT_GETTER(ref<GLT::world::i_world_plugin>,         world)


        void update(const f32 delta_time) override;


        void render_imgui(const f32 delta_time) override;


        // Bind a world plugin. Any previous one is detached (not unloaded - the plugin manager owns its lifetime).
        void set_world(ref<GLT::world::i_world_plugin> world);


        // Hand the layer ownership of a controller. Accepts anything derived from controller by implicit unique_ptr conversion.
        template<typename controller_type, typename... args>
        requires std::derived_from<controller_type, GLT::world::controller>
        void set_controller(args&&... arguments);


        void set_controller(ref<GLT::world::controller> ctrl);

    private:

        void on_save_event(const GLT::save_event& event);


        ref<GLT::world::controller>                             m_controller{};
        ref<GLT::world::camera>                                 m_editor_camera{};
        ref<GLT::world::i_world_plugin>                         m_world{};
        handle                                                  m_save_sub_handle{};
        std::vector<GLT::asset::mesh::instance>                 m_scene_buffer{};

    };

}

#include "world_layer.inl"
