
#pragma once

#include "layer/layer.h"
#include "layer/layer_stack.h"
#include "debug/profiler.h"
#include "plugin_system/plugin_manager.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::platform {
    class i_window_plugin; 
}

namespace GLT::render {
    class i_renderer_plugin; 
}

namespace GLT::audio {
    class i_audio_plugin; 
}

namespace GLT::game_loop {
    class i_game_loop_plugin;
}

namespace GLT {
    class update_event;
}

namespace GLT::game_loop {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Everything a game loop is allowed to touch, bundled. The application fills this in once per frame before calling run()
    // Plugins must NOT keep a reference to it
    struct context {

        f32&                                delta_time;
        layer_stack&                        layers;
        ref<platform::i_window_plugin>      window{};
        ref<render::i_renderer_plugin>      renderer{};
        ref<audio::i_audio_plugin>          audio{};
        util::interval_controller&          fps_controller;
        debug::application_stats&           stats;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // @brief Fetches the process-wide game-loop plugin
    // @return Strong reference to the game-loop plugin, or an empty ref if it isn't loaded
    FORCE_INLINE_R ref<GLT::game_loop::i_game_loop_plugin> get_ref() {

        return GLT::plugin_manager::get_plugin_ref<GLT::game_loop::i_game_loop_plugin>(plugin_manager::interface::game_loop);
    }

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Plugin that owns the main loop and decides the order of every per-frame step (input polling, update, render, present)
    //
    // The application drives the plugin through three calls, in order:
    //   1. [init(ctx)]      - one-time setup (allocate frame resources, build DAGs)
    //   2. [run(ctx)]       - the blocking loop body itself
    //   3. [shutdown(ctx)]  - teardown after [run()] returns
    //
    // [run()] is expected to return only once [is_stop_requested()] becomes true - typically set from a window-close handler
    // or an explicit call from the editor. [request_stop()] must be callable from any thread
    class i_game_loop_plugin : public GLT::plugin_manager::i_plugin {
    public:

        virtual ~i_game_loop_plugin() = default;


        // Called once, before run(). Allocate per-loop state, build your schedule / DAG here
        // @param ctx  Live application context; do NOT store a reference past init()
        virtual void init(context& /*ctx*/) {}


        // Called once, after run() returns. Join worker threads, free resources
        // @param ctx  Live application context
        virtual void shutdown(context& /*ctx*/) {}


        // The loop itself. Blocking. Return when is_stop_requested() is true. The plugin decides order, threading and dependencies
        // @param ctx  Live application context, refreshed by the application each frame
        virtual void run(context& ctx) = 0;


        // Called from any thread (e.g. window close event). Must be thread-safe
        void request_stop() noexcept                    { m_stop_requested.store(true, std::memory_order_relaxed); }


        // @brief Reports whether a stop has been requested
        // @return true if [request_stop()] has been called
        bool is_stop_requested() const noexcept         { return m_stop_requested.load(std::memory_order_relaxed); }

    protected:

        std::atomic_bool                                m_stop_requested{ false };

    };

}
