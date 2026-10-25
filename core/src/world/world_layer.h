
#pragma once

#include "asset/mesh.h"
#include "world/controller.h"
#include "event/application_event.h"
#include "render/i_scene_renderer.h"
#include "world/light_snapshot.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {
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
        DEFAULT_GETTER(ref<GLT::world::i_world_plugin>,         world)


        void update(const f32 delta_time) override {}


        void render_imgui(const f32 delta_time) override {}


        // Bind a world plugin. Any previous one is detached (not unloaded - the plugin manager owns its lifetime).
        void set_world(ref<GLT::world::i_world_plugin> world) {}


        // Hand the layer ownership of a controller. Accepts anything derived from controller by implicit unique_ptr conversion.
        template<typename controller_type, typename... args>
        requires std::derived_from<controller_type, GLT::world::controller>
        void set_controller(args&&... arguments) {}


        void set_controller(ref<GLT::world::controller> ctrl) {}


        void add_scene_renderer(GLT::render::i_scene_renderer* scene_renderer) {}

    private:


        ref<GLT::world::controller>                             m_controller{};
        ref<GLT::world::i_world_plugin>                         m_world{};
        handle                                                  m_save_sub_handle{};
        std::vector<GLT::asset::mesh::instance>                 m_scene_buffer{};
        std::vector<GLT::world::light_snapshot>                 m_light_buffer{};
        std::vector<GLT::render::i_scene_renderer*>             m_world_relative_scene_renderers{};

    };

}
