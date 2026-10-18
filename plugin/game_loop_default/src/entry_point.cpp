
#include "util/pch.h"

#include <util/timing/stopwatch.h>
#include <event/event_bus.h>
#include <event/application_event.h>
#include <platform/i_window.h>
#include <plugin_system/i_plugin.h>
#include <plugin_system/i_game_loop_plugin.h>
#include <plugin_system/i_audio_plugin.h>
#include <render/i_renderer.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::game_loop {

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

    class plugin final : public GLT::game_loop::i_game_loop_plugin {
    public:

        void on_load() { }


        void on_unload() { }


        void init(context& /*ctx*/) override { 
            
            refresh_if_stale();
            if (m_window)
                m_window->show(true);
        }


        void shutdown(context& /*ctx*/) override {

            m_window = nullptr;
            m_renderer = nullptr;
            m_audio = nullptr;
        }


        void run(context& ctx) override {

            util::stopwatch cpu_timer(&ctx.stats.cpu_time_ms, GLT::time_unit::milliseconds);

            while (!is_stop_requested()) {

                cpu_timer.restart();

                if (m_editor != nullptr)        // user can only change plugins in editor, else plugins are stable over runtime
                    refresh_if_stale();

                // update ----------------------------------------------------------------------------------------------
                m_window->poll_events();                                                        // update internal state
                thread_pool::pump_main_thread();                                                // run deferred UI updates
                for (auto layer = ctx.layers.end(); layer != ctx.layers.begin(); )
                    (*--layer)->update(ctx.delta_time);
                GLT::event_bus::post<update_event>(ctx.delta_time);                             // all systems can subscribe to this (eg: plugins)
                m_audio->update_3d_audio();

                // draw ------------------------------------------------------------------------------------------------
                m_renderer->begin_frame();                                                      // start frame + start imgui frame
                for (auto layer = ctx.layers.begin(); layer != ctx.layers.end(); )
                    (*layer++)->render_imgui(ctx.delta_time);
                m_renderer->draw_frame();                                                       // finish imgui stuff and render world

                cpu_timer.stop();                                                               // Measure CPU work

                // stats -----------------------------------------------------------------------------------------------
                ctx.delta_time = ctx.fps_controller.limit();
                ctx.total_time += ctx.delta_time;
                ctx.stats.frame_time_ms = ctx.delta_time;
                ctx.stats.fps = (ctx.delta_time > 0.0f) ? (1000.0f / ctx.delta_time) : 0.0f;
                ctx.stats.render = m_renderer->get_render_stats();
                ctx.stats.ram_bytes = GLT::util::get_process_ram_bytes();
                debug::update_app_stats(ctx.stats);
            }
        }

    private:

        void refresh_if_stale() {

            const auto gen = GLT::plugin_manager::get_generation();
            if (gen == m_gen)
                return;

            m_window = GLT::plugin_manager::get_plugin_raw<platform::i_window_plugin>(plugin_manager::interface::window);
            m_renderer = GLT::plugin_manager::get_plugin_raw<render::i_renderer_plugin>(plugin_manager::interface::renderer);
            m_audio = GLT::plugin_manager::get_plugin_raw<audio::i_audio_plugin>(plugin_manager::interface::audio);
            m_editor = GLT::plugin_manager::get_plugin_raw<plugin_manager::i_plugin>(plugin_manager::interface::editor_core);
            m_gen = gen;
        }


        u64                                                     m_gen = 0;
        GLT::platform::i_window_plugin*                         m_window = nullptr;
        GLT::render::i_renderer_plugin*                         m_renderer = nullptr;
        GLT::audio::i_audio_plugin*                             m_audio = nullptr;
        plugin_manager::i_plugin*                               m_editor = nullptr;
    };

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

EXPORT_PLUGIN_CLASS(GLT::game_loop::plugin, GLT::game_loop::descriptor)
