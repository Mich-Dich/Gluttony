
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::plugin_manager {

    // MACROS ==========================================================================================================

    // every plugin is compiled into the engine executable. discover_plugins() only reads the static registry (populated by
    // EXPORT_PLUGIN_CLASS); the plugins/ directory is never scanned and dlopen/dlsym is unused. The plugin set is baked into the binary
    //          -> Use for shipping / exported games
    #define LINKAGE_STYLE_STATIC                                    1


    // plugins ship as shared libraries. discover_plugins() walks the plugins/ directory and dlopen()s each candidate to read its
    // descriptor. The static registry is ignored (a shared library's copy of it is a different instance anyway).
    //          -> Use for the editor / development builds.
    #define LINKAGE_STYLE_DYNAMIC                                   2


    // Selects how the plugin manager pulls plugins into the process
    // If left undefined, DYNAMIC is assumed
    #ifndef LINKAGE_STYLE

        #define LINKAGE_STYLE                                       LINKAGE_STYLE_DYNAMIC

    #endif


    #if LINKAGE_STYLE != LINKAGE_STYLE_STATIC && LINKAGE_STYLE != LINKAGE_STYLE_DYNAMIC

        #error "LINKAGE_STYLE must be LINKAGE_STYLE_STATIC (1) or LINKAGE_STYLE_DYNAMIC (2)"

    #endif


    #if LINKAGE_STYLE == LINKAGE_STYLE_STATIC

        // Static plugins are linked into the host executable as OBJECT libraries. All three entry symbols would otherwise
        // collide across plugins, so give them internal linkage. The static registrar constructor in the same TU is the
        // only thing that touches them
        #define GLT_PLUGIN_SYMBOL                                   static

    #else

        // Dynamic plugins are dlopen'd by name, so the symbols must be externally visible and unmangled
        #if defined(PLATFORM_WINDOWS)
    
            #define GLT_PLUGIN_SYMBOL                               __declspec(dllexport)

        #elif defined(PLATFORM_LINUX)

            #define GLT_PLUGIN_SYMBOL                               __attribute__((visibility("default")))

        #endif  

    #endif

    // CONSTANTS =======================================================================================================

    inline constexpr bool is_static_linkage = (LINKAGE_STYLE == LINKAGE_STYLE_STATIC);

    inline constexpr bool is_dynamic_linkage = (LINKAGE_STYLE == LINKAGE_STYLE_DYNAMIC);

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
