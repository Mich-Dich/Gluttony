
#pragma once

#include "event/event.h"

// FORWARD DECLARATIONS ================================================================================================

namespace GLT {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Base class for one unit of per-frame logic and UI
    //
    // A layer is the engine's primary composition point for anything that needs a slice of the frame: it receives an [update()]
    // tick, can draw ImGui, and lives inside a [layer_stack]. Two flavours coexist in the same container - regular layers
    // and overlays - distinguished by [is_overlay]. Layers render in insertion order; overlays always sit above every layer
    // regardless of insertion order
    //
    // Lifecycle: [on_attach()] is called once when the layer is pushed, [on_detach()] once when it's popped or the stack is cleared
    class layer {
    public:

        // @param name  Debug name used in log lines ("attaching [name]" / "detaching [name]")
        layer(const std::string& name = "layer") : m_debugname(name) {}
        virtual ~layer() = default;

        DEFAULT_GETTER_SETTER(bool,         is_active);
        DEFAULT_GETTER_SETTER(bool,         is_overlay);


        // @brief Per-frame logic tick
        // @param delta_time  Seconds elapsed since the previous frame
        virtual void update(const f32 delta_time) = 0;


        // @brief Per-frame ImGui pass
        // @param delta_time  Seconds elapsed since the previous frame
        virtual void render_imgui(const f32 delta_time) = 0;


        // @brief Called once when the layer is pushed onto the stack
        //
        // Default implementation logs the attach; override to allocate resources or subscribe to events
        virtual void on_attach();


        // @brief Called once when the layer is popped or the stack is cleared
        //
        // Default implementation logs the detach; override to release resources or unsubscribe from events
        virtual void on_detach();

    private:

		std::string         m_debugname;
        bool                m_is_active = true;
        bool                m_is_overlay = false;

    };

}
