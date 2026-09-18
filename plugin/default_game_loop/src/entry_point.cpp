
#include "util/pch.h"

#include <event/event_bus.h>
#include <event/application_event.h>
#include <plugin_system/i_plugin.h>
#include <plugin_system/i_game_loop_plugin.h>
#include <plugin_system/i_window_plugin.h>
#include <plugin_system/i_audio_plugin.h>
#include <plugin_system/i_renderer_plugin.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    static constexpr const char*                                dependencies_names[] = {

        nullptr
    };
    
    static constexpr GLT::plugin_manager::interface             dependencies_interfaces[] = {
        
        GLT::plugin_manager::interface::window,
        GLT::plugin_manager::interface::audio,
        GLT::plugin_manager::interface::renderer,
    };

    static constexpr GLT::plugin_manager::plugin_descriptor     descriptor = {

        .name                                                   = GLT_MODULE_NAME,
        .load_phase                                             = GLT::plugin_manager::phase::application_ready,
        .unload_phase                                           = GLT::plugin_manager::phase::post_application_shutdown,
        .target                                                 = GLT::plugin_manager::interface::game_loop,
        .dependency_names_count                                 = ARRAY_SIZE(dependencies_names),
        .dependency_names                                       = dependencies_names,
        .dependency_interface_count                             = ARRAY_SIZE(dependencies_interfaces),
        .dependency_interfaces                                  = dependencies_interfaces,
    };

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    class game_loop_default final : public GLT::i_game_loop_plugin {
    public:

        void on_load() { }


        void on_unload() { }


        void init(GLT::game_loop_context& ctx) override { ctx.window->show(true); }


        void shutdown(GLT::game_loop_context& /*ctx*/) override { }


        void run(GLT::game_loop_context& ctx) override {

            while (!is_stop_requested()) {

                // update ----------------------------------------------------------------------------------------------
                ctx.window->poll_events();                                                      // update internal state
                thread_pool::pump_main_thread();                                                // run deferred UI updates
                for (auto layer = ctx.layers.end(); layer != ctx.layers.begin(); )
                    (*--layer)->update(ctx.delta_time);
                GLT::event_bus::post<update_event>(ctx.delta_time);                             // all systems can subscribe to this (eg: plugins)            
                ctx.audio->update_3d_audio();
    
                // draw ------------------------------------------------------------------------------------------------
                ctx.renderer->begin_frame();                                                    // start frame + start imgui frame
                for (auto layer = ctx.layers.begin(); layer != ctx.layers.end(); )
                    (*layer++)->render_imgui(ctx.delta_time);
                ctx.renderer->draw_frame();                                                     // finish imgui stuff and render world
    
                // stats -----------------------------------------------------------------------------------------------
                ctx.delta_time = ctx.fps_controller.limit();
                ctx.stats.frame_time_ms = ctx.delta_time;
                ctx.stats.cpu_time_ms = 0.f;                                                    // TODO: set value
                ctx.stats.fps = (ctx.delta_time > 0.0f) ? (1000.0f / ctx.delta_time) : 0.0f;
                ctx.stats.render = ctx.renderer->get_render_stats();
                debug::update_app_stats(ctx.stats);
            }
        }

    };

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

EXPORT_PLUGIN_CLASS(GLT::game_loop_default, GLT::descriptor)
