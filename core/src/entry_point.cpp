
#include "util/pch.h"
#include "plugin_system/plugin_manager.h"
#include "util/argument_parser.h"
#include "application.h"



// FORWARD DECLARATIONS ================================================================================================

// CONSTANTS =======================================================================================================

// Define expected arguments
const std::vector<GLT::argument_parser::argument_spec> specs = {

    {
        .name = "project_path",
        .short_name = "",
        .required = true,
        .positional = true,
        .position = 0,
        .type = "path",
        .default_value = std::filesystem::path(),
        .help_text = "Path to the project file"
    },
};

// MACROS ==========================================================================================================

#if defined(PLATFORM_LINUX)

    #define MAIN_FUNC   main(int argc, char* argv[])
    #define ARGC        argc
    #define ARGV        argv

#elif defined(PLATFORM_WINDOWS)

    #define MAIN_FUNC   WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
    #define ARGC        __argc
    #define ARGV        __argv

#endif

// TYPES ===========================================================================================================

// STATIC VARIABLES ================================================================================================

// FUNCTION IMPLEMENTATION =========================================================================================

int MAIN_FUNC {

    // parse arguments
    std::error_code error{};
    const auto parsed = GLT::argument_parser::parse_arguments(specs, argc, argv, error);
    ASSERT(!error, "", "Argument error [{}]", error.message())
    const auto project_path = GLT::argument_parser::get<std::filesystem::path>(parsed, "project_path");

    // setup some core systems
    GLT::plugin_manager::discover_plugins(project_path);
    GLT::plugin_manager::load_plugins(GLT::plugin_manager::phase::earliest_possible);
    GLT::plugin_manager::unload_plugins(GLT::plugin_manager::phase::earliest_possible);
    GLT::config::init();
    GLT::thread_pool::init();
    GLT::plugin_manager::load_plugins(GLT::plugin_manager::phase::post_config_init);
    GLT::plugin_manager::unload_plugins(GLT::plugin_manager::phase::post_config_init);
    GLT::logger::register_label_for_thread("main");
    GLT::logger::init("[$B$T:$J$E] [$B$R $L$X $Q - $I:$P:$G$E] $C$Z", true, GLT::util::get_executable_path() / GLT::config::LOG_DIR, "gluttony.log", true);
    GLT::logger::set_buffer_threshold(GLT::logger::severity::warn);
    GLT::crash_handler::attach();
    GLT::crash_handler::subscribe(GLT::logger::shutdown);
    GLT::plugin_manager::load_plugins(GLT::plugin_manager::phase::post_setup);
    GLT::plugin_manager::unload_plugins(GLT::plugin_manager::phase::post_setup);

    {
        GLT::application app{project_path};
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

#undef MAIN_FUNC

// CLASS IMPLEMENTATION ============================================================================================

// CLASS PUBLIC ====================================================================================================

// CLASS PROTECTED =================================================================================================

// CLASS PRIVATE ===================================================================================================
