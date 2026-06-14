
#include <event/event_bus.h>
#include <event/input_event.h>
#include <util/util.h>
#include <plugin_system/plugin_interface.h>

#include "input_action_mapper.h"
#include "action_types.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::input_action_mapper {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    static const char*                              dependencies_names[] = {
        
        nullptr
    };

    static plugin_manager::interface                dependencies_interfaces[] = {

        plugin_manager::interface::window 
    };

    static plugin_manager::plugin_descriptor        descriptor = {
        .name                                       = GLT_MODULE_NAME,
        .phase                                      = plugin_manager::load_phase::application_ready,
        .target                                     = plugin_manager::interface::logger,
        .dependency_names_count                     = ARRAY_SIZE(dependencies_names),
        .dependency_names                           = dependencies_names,
        .dependency_interface_count                 = ARRAY_SIZE(dependencies_interfaces),
        .dependency_interfaces                      = dependencies_interfaces,
    };

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    class plugin : public GLT::plugin_manager::i_plugin {
    public:

        void on_load() override;

        
        void on_unload() override;

        // update internal state and all registered actions
        // after calling this, the callback will be called
        void update() override;

    private:

        void on_key_event(const GLT::key_event& event);


        void on_mouse_event(const GLT::mouse_event& event);


        handle                                          m_key_event_sub{};
        handle                                          m_mouse_event_sub{};
        std::unordered_map<GLT::key_code, key_info>     m_key_states{};         // buffer key states from event bus until next call of update();
        mouse_state                                     m_mouse_state{};              // buffer mouse states from event bus until next call of update();

    };

}

#include "input_action_mapper.inl"

EXPORT_PLUGIN_CLASS(GLT::input_action_mapper::plugin, GLT::input_action_mapper::descriptor)
