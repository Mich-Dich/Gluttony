
#include <event/event_bus.h>
#include <event/input_event.h>
#include <util/util.h>
#include <plugin_system/plugin_interface.h>

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::input_action_mapper {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    static const char*                              dependencies_names[] = {
        
        nullptr
    };

    static plugin_manager::targeted_interface       dependencies_interfaces[] = {

        plugin_manager::targeted_interface::window 
    };

    static plugin_manager::plugin_descriptor        descriptor = {
        .name                           = GLT_MODULE_NAME,
        .phase                          = plugin_manager::load_phase::application_ready,
        .target                         = plugin_manager::targeted_interface::logger,
        .dependency_names_count         = ARRAY_SIZE(dependencies_names),
        .dependency_names               = dependencies_names,
        .dependency_interface_count     = ARRAY_SIZE(dependencies_interfaces),
        .dependency_interfaces          = dependencies_interfaces,
    };

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    class plugin : public GLT::plugin_manager::i_plugin {
    public:

        void on_load() override {

            m_key_event_sub = GLT::event_bus::subscribe(
                std::function<void(const GLT::key_event&)>(std::bind_front(&plugin::on_key_event, this))
            );

            m_mouse_event_sub = GLT::event_bus::subscribe(
                std::function<void(const GLT::mouse_event&)>(std::bind_front(&plugin::on_mouse_event, this))
            );

            LOG_LOADED
        }

        
        void on_unload() override {

            GLT::event_bus::unsubscribe(m_key_event_sub);
            GLT::event_bus::unsubscribe(m_mouse_event_sub);
            LOG_UNLOADED
        }

    private:

        void on_key_event(const GLT::key_event& event) {

        }


        void on_mouse_event(const GLT::mouse_event& event) {

        }


        handle              m_key_event_sub{};
        handle              m_mouse_event_sub{};
    };

}

EXPORT_PLUGIN_CLASS(GLT::input_action_mapper::plugin, GLT::input_action_mapper::descriptor)
