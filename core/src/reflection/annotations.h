
#pragma once

#include "reflection/annotations.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::reflect::annotations {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // serialization ---------------------------------------------------------------------------------------------------
    struct serialize {};                        // default-on for non-static data members


    struct skip {};                             // never serialize, never show in editor


    struct asset_ref {};                        // serialize as UUID, retain/release on load


    struct version {                            // overrides the descriptor's default version
        u16                     value;
    };

    // editor hints ----------------------------------------------------------------------------------------------------

    struct display_name {

        const char*             value;
    };


    struct tooltip {

        const char*             value;
    };


    struct category {

        const char*             value;
    };


    struct range {

        f64                     min;
        f64                     max; 
    };


    struct color {};


    struct multiline {};


    struct readonly {};

    // kind annotations (exactly one per class) ------------------------------------------------------------------------

    struct entity_script {};


    struct global_script {};


    struct world_generator {};


    struct editor_action {

        const char*             label;
    };

    // field annotations -----------------------------------------------------------------------------------------------

    struct persist {};


    struct transient {};                        // runtime-only, drop on save


    struct editor {

        const char*             category = "";
    };

    // class annotations -----------------------------------------------------------------------------------------------

    struct name {

        const char*             value;
    };


    struct priority {

        i32                     value;
    };

    // function annotations --------------------------------------------------------------------------------------------

    struct editor_callable {

        const char*             label = "";
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
