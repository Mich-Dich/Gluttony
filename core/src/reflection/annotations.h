
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::reflect::annotations {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // ---- serialization ----------------------------------------------------------
    struct serialize {};                        // default-on for non-static data members


    struct skip {};                             // never serialize, never show in editor


    struct transient {};                        // runtime-only, drop on save


    struct asset_ref {};                        // serialize as UUID, retain/release on load


    struct version {                            // overrides the descriptor's default version
        u16 value;
    };

    // ---- editor hints -----------------------------------------------------------

    struct display_name {
        const char* value;
    };


    struct tooltip {
        const char* value;
    };


    struct category {
        const char* value;
    };


    struct range {
        f64 min;
        f64 max; 
    };


    struct color {};


    struct multiline {};


    struct readonly {};

    // ---- type-level -------------------------------------------------------------

    // Stable name override. Required for any type you intend to serialize across
    // compiler / engine versions — display_string_of is implementation-defined.
    struct name         { const char* value; };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
