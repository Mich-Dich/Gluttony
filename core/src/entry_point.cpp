
#include "util/pch.h"
#include "plugin_system/plugin_manager.h"
#include "application.h"

// FORWARD DECLARATIONS ================================================================================================

// CONSTANTS =======================================================================================================

// MACROS ==========================================================================================================

#if defined(PLATFORM_LINUX)
    #define ARGC        argc
    #define ARGV        argv
    #define MAIN_FUNC   main(int argc, char* argv[])
#elif defined(PLATFORM_WINDOWS)
    #include <Windows.h>
    #define ARGC        __argc
    #define ARGV        __argv
    #define MAIN_FUNC   WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#endif

// TYPES ===========================================================================================================

// STATIC VARIABLES ================================================================================================

// FUNCTION IMPLEMENTATION =========================================================================================

int MAIN_FUNC {
    
    {
        GLT::plugin_manager::discover_plugins();
        GLT::plugin_manager::load_plugins(GLT::plugin_manager::load_phase::earliest_possible);
        GLT::config::init();
        GLT::plugin_manager::load_plugins(GLT::plugin_manager::load_phase::post_config_init);
        GLT::logger::init("[$B$T:$J$E] [$B$R $L$X $Q - $I:$P:$G$E] $C$Z", true, GLT::util::get_executable_path() / "logs", "gluttony.log", true);
        GLT::logger::set_buffer_threshold(GLT::logger::severity::warn);
        GLT::logger::register_label_for_thread("main");
        GLT::crash_handler::attach();
        GLT::crash_handler::subscribe(GLT::logger::shutdown);
        GLT::plugin_manager::load_plugins(GLT::plugin_manager::load_phase::post_setup);
    }

    {
        GLT::application app{ARGC, ARGV};
        app.run();
    }

    {
        GLT::plugin_manager::load_plugins(GLT::plugin_manager::load_phase::final_cleanup);
        GLT::logger::shutdown();
        GLT::crash_handler::detach();
        GLT::plugin_manager::shutdown();
    }

    return EXIT_SUCCESS;
}

#undef MAIN_FUNC

// CLASS IMPLEMENTATION ============================================================================================

// CLASS PUBLIC ====================================================================================================

// CLASS PROTECTED =================================================================================================

// CLASS PRIVATE ===================================================================================================
