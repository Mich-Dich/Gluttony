
#include "util/pch.h"
#include "app.h"



// FORWARD DECLARATIONS ================================================================================================

// CONSTANTS ===========================================================================================================

// MACROS ==============================================================================================================

#if defined(PLATFORM_LINUX)

    #define MAIN_FUNC   main(int argc, char* argv[])
    #define ARGC        argc
    #define ARGV        argv

#elif defined(PLATFORM_WINDOWS)

    #define MAIN_FUNC   WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
    #define ARGC        __argc
    #define ARGV        __argv

#endif

// TYPES ===============================================================================================================

// STATIC VARIABLES ====================================================================================================

// INTERNAL TEMPLATE DECLARATION =======================================================================================

// INTERNAL FUNCTION DECLARATION =======================================================================================

// INTERNAL TEMPLATE IMPLEMENTATION ====================================================================================

// INTERNAL FUNCTION IMPLEMENTATION ====================================================================================

// TEMPLATE IMPLEMENTATION =============================================================================================

// FUNCTION IMPLEMENTATION =============================================================================================

int MAIN_FUNC {

    GLT::plugin_manager::discover_plugins(GLT::util::get_executable_path());
    GLT::plugin_manager::enter_phase(GLT::plugin_manager::phase::earliest_possible);
    GLT::config::init();
    GLT::thread_pool::init();
    GLT::plugin_manager::enter_phase(GLT::plugin_manager::phase::post_config_init);
    GLT::logger::register_label_for_thread("main");
    GLT::logger::init("[$B$T:$J$E] [$B$R $L$X $Q - $I:$P:$G$E] $C$Z", true, GLT::util::get_executable_path() / GLT::config::LOG_DIR,
        "gluttony.log", false);
    GLT::logger::set_buffer_threshold(GLT::logger::severity::warn);
    GLT::crash_handler::attach();
    GLT::crash_handler::subscribe(GLT::logger::shutdown);

    {
        GLT::launcher::app app{};
        app.run();
    }

    // cleanup core systems
    GLT::plugin_manager::load_plugins(GLT::plugin_manager::phase::final_cleanup);
    GLT::logger::shutdown();
    GLT::crash_handler::detach();
    GLT::plugin_manager::shutdown();
    GLT::thread_pool::wait_for_all();                 // drain any stragglers
    GLT::thread_pool::pump_main_thread();             // drain callbacks posted during the wait
    GLT::thread_pool::shutdown();                     // join workers
    GLT::plugin_manager::unload_plugins(GLT::plugin_manager::phase::final_cleanup);

    return EXIT_SUCCESS;
}

// CLASS IMPLEMENTATION ================================================================================================

// CLASS PUBLIC ========================================================================================================

// CLASS PROTECTED =====================================================================================================

// CLASS PRIVATE =======================================================================================================
