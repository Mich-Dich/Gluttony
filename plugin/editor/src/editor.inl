#pragma once

#include <util/pch.h>
#include <layer/layer.h>
#include <layer/layer_stack.h>
#include <application.h>

#include "editor_layer.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    plugin::plugin() {}
    
    
    plugin::~plugin() {}

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    void plugin::on_load() {

        mp_editor_layer = GLT::application::get().get_layer_stack_ref().push_layer<editor_layer>();
    }


    void plugin::on_unload() {

        GLT::application::get().get_layer_stack_ref().pop_layer();
        mp_editor_layer = {};
    }


    void plugin::update(const GLT::update_event&) {

    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
