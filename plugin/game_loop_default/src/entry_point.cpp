
#include "util/pch.h"

#include <application.h>
#include <util/timing/stopwatch.h>
#include <debug/trace.h>
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
        .display_name                                           = "Default Game Loop",
        .description                                            = "A simple, straightforward game-loop",
        .category                                               = "Core|Logic",
        .author                                                 = "Mich-Dich",
        .plugin_version                                         = {1, 0, 0},
        .api_version                                            = {1, 0, 0},
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
                m_window->show(GLT::application::get().get_main_window(), true);
        }


        void shutdown(context& /*ctx*/) override {

            m_window = nullptr;
            m_renderer = nullptr;
            m_audio = nullptr;
        }


        void run(context& ctx) override {

            GLT_PROFILE_THREAD("Main");

            // The loop time includes every blocking call (GPU fences, swapchain acquire, present/vsync). Those are tracked by
            // GLT_PROFILE_WAIT zones, so CPU work = loop time - waits. The limiter sleep is reported separately
            f32 loop_ms = 0.f;
            util::stopwatch cpu_timer(&loop_ms, GLT::time_unit::milliseconds);
            u64 frame = 0;

            while (!is_stop_requested()) {

                cpu_timer.restart();

                if (m_editor != nullptr)                                        // user can only change plugins with the editor
                    refresh_if_stale();

                // update --------------------------------------------------------------------------------------------------
                {
                    GLT_PROFILE_SCOPE("Poll events");
                    m_window->poll_events();
                }
                thread_pool::pump_main_thread();
                {
                    GLT_PROFILE_SCOPE("Layer update");
                    for (auto layer = ctx.layers.end(); layer != ctx.layers.begin(); )
                        (*--layer)->update(ctx.delta_time);                     // world_layer submits to scene renderers
                }
                {
                    GLT_PROFILE_SCOPE("Update event");
                    GLT::event_bus::post<update_event>(ctx.delta_time);
                }
                {
                    GLT_PROFILE_SCOPE("Audio update");
                    m_audio->update_3d_audio();
                }

                // draw ----------------------------------------------------------------------------------------------------
                m_renderer->begin_frame(frame);                                 // tick shared resource manager

                auto& scenes = GLT::application::get().get_scene_renderers_ref();

                for (auto& scene : scenes)
                    scene->update(ctx.delta_time);

                {
                    GLT_PROFILE_SCOPE("Build UI (ImGui)");
                    for (auto layer = ctx.layers.begin(); layer != ctx.layers.end(); )
                        (*layer++)->render_imgui(ctx.delta_time);
                }

                // Offscreen targets (editor viewport, previews) first, window targets after: the UI rendered by a window
                // target samples the offscreen images, so they must already hold THIS frame. Otherwise the viewport image
                // lags one frame behind everything ImGui draws on top of it (gizmo wobbles around the entity when the
                // camera moves)
                auto render_scene = [&](auto& scene) {
                    auto result = scene->render(frame);
                    if (!result) {
                        if (result.error().kind == GLT::render::render_error::code::target_out_of_date && !scene->is_offscreen()) {
                            const auto fb = m_window->get_framebuffer_size(GLT::application::get().get_main_window());
                            scene->resize({ static_cast<u32>(fb.x), static_cast<u32>(fb.y) });
                        } else {
                            LOG(error, "scene_renderer::render failed: {}", result.error().message)
                        }
                    }
                };

                {
                    GLT_PROFILE_SCOPE("Render offscreen targets");
                    for (auto& scene : scenes)
                        if (scene->is_offscreen())
                            render_scene(scene);
                }
                {
                    GLT_PROFILE_SCOPE("Render window targets");
                    for (auto& scene : scenes)
                        if (!scene->is_offscreen())
                            render_scene(scene);
                }

                cpu_timer.stop();
                frame++;

                // stats ---------------------------------------------------------------------------------------------------
                const f32 wait_ms = debug::trace::current_frame_wait_ms();
                const u64 limiter_start = debug::trace::now_ns();
                {
                    GLT_PROFILE_IDLE("Frame limiter");
                    ctx.delta_time = ctx.fps_controller.limit();
                }
                const f32 limiter_ms = static_cast<f32>(debug::trace::now_ns() - limiter_start) * 1e-6f;

                ctx.total_time += ctx.delta_time;
                ctx.stats.frame_time_ms = ctx.delta_time;
                ctx.stats.cpu_wait_ms = wait_ms;
                ctx.stats.cpu_time_ms = std::max(loop_ms - wait_ms, 0.f);
                ctx.stats.limiter_ms = limiter_ms;
                ctx.stats.fps = (ctx.delta_time > 0.0f) ? (1000.0f / ctx.delta_time) : 0.0f;
                ctx.stats.render = m_renderer->get_render_stats();
                ctx.stats.ram_bytes = GLT::util::get_process_ram_bytes();
                debug::update_app_stats(ctx.stats);

                GLT_PROFILE_PLOT("CPU work (ms)", static_cast<double>(ctx.stats.cpu_time_ms));
                GLT_PROFILE_PLOT("CPU wait (ms)", static_cast<double>(wait_ms));
                GLT_PROFILE_PLOT("GPU (ms)", static_cast<double>(ctx.stats.render.gpu_time_ms));
                GLT_PROFILE_FRAME();
            }
        }

    private:

        void refresh_if_stale() {

            const auto gen = GLT::plugin_manager::get_generation();
            if (gen == m_gen)
                return;

            m_window = GLT::platform::window::get_ptr();
            m_renderer = GLT::render::renderer::get_ptr();
            m_audio = GLT::audio::manager::get_ptr();
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
