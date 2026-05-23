
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

    class layer {
    public:

        layer(const std::string& name = "layer") : m_debugname(name) {}
        virtual ~layer() = default;

        DEFAULT_GETTER_SETTER(bool,         is_active);
        DEFAULT_GETTER_SETTER(bool,         is_overlay);

        // These must be implemented by derived classes
        virtual void update(const f32 delta_time) = 0;
        virtual void render_imgui(const f32 delta_time) = 0;

        // These can be overwritten by derived classes
        virtual void on_attach();
        virtual void on_detach();

    private:

		std::string         m_debugname;
        bool                m_is_active = true;
        bool                m_is_overlay = false;

    };

}
