
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

    // Everything a game loop is allowed to touch, bundled. The application fills this in
    // once per frame before calling run(). Plugins must NOT keep a reference to it.
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

    FORCE_INLINE_R ref<GLT::game_loop::i_game_loop_plugin> get_ref() {

        return GLT::plugin_manager::get_plugin_ref<GLT::game_loop::i_game_loop_plugin>(plugin_manager::interface::game_loop);
    }

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class i_game_loop_plugin : public GLT::plugin_manager::i_plugin {
    public:

        virtual ~i_game_loop_plugin() = default;


        // Called once, before run(). Allocate per-loop state, build your schedule / DAG here.
        virtual void init(context& /*ctx*/) {}


        // Called once, after run() returns. Join worker threads, free resources.
        virtual void shutdown(context& /*ctx*/) {}


        // The loop itself. Blocking. Return when is_stop_requested() is true.
        // The plugin decides order, threading and dependencies.
        virtual void run(context& ctx) = 0;


        // Called from any thread (e.g. window close event). Must be thread-safe.
        void request_stop() noexcept                    { m_stop_requested.store(true, std::memory_order_relaxed); }


        bool is_stop_requested() const noexcept         { return m_stop_requested.load(std::memory_order_relaxed); }

    protected:

        std::atomic_bool                                m_stop_requested{ false };

    };

}
