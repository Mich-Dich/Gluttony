
#pragma once

// #include <event/application_event.h>

#include "util/io/logger.h"         // expose logger for all plugins


// FORWARD DECLARATIONS ================================================================================================

namespace GLT {
    class update_event;
}


namespace GLT::plugin_manager {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    #define EXPORT_PLUGIN_CLASS(plugin_class, descriptor_var)                                                           \
        extern "C" {                                                                                                    \
            const GLT::plugin_manager::plugin_descriptor* gluttony_plugin_descriptor() { return &descriptor_var; }      \
            GLT::plugin_manager::i_plugin* create_plugin() { return new plugin_class(); }                               \
            void destroy_plugin(GLT::plugin_manager::i_plugin* p) { delete p; }                                         \
        }

    // TYPES ===========================================================================================================
    
    // Defines the exact moment in the engine's lifecycle when a plugin should be loaded.
    // Plugins declare their required phase via their descriptor; the plugin manager
    // loads all plugins belonging to the current phase in dependency order.
    enum class load_phase : u16 {

        // ----- ENTRY POINT / EARLY GLOBAL SETUP ----------------------------------------------------------------------

        // After plugin discovery, before any core subsystems are initialized.
        // Guaranteed: none (only the plugin system itself).
        // Plugin actions: register custom configuration schemas, hook into early crash handling,
        // or provide alternative logging backends before they are chosen.
        // Constraints: No logging, no config access, no windowing. Very limited environment.
        earliest_possible = 0,

        // After configuration system is initialized, before logging starts.
        // Guaranteed: config::init() completed, config values can be read.
        // Plugin actions: override config defaults, validate custom config sections,
        // or set up state that depends strictly on config data.
        // Constraints: Logger not yet available – do NOT log messages.
        post_config_init,

        // After logger and crash handler are fully set up, before application construction.
        // Guaranteed: logger::init() done, crash_handler attached, logging labels active.
        // Plugin actions: perform logging-heavy initialization, register crash-time callbacks,
        // or prepare per‑thread state (logging labels already registered for main thread).
        // Constraints: No window or event system available yet.
        post_setup,

        // ----- APPLICATION CONSTRUCTION (WINDOW & CORE) --------------------------------------------------------------

        // Inside application constructor, before the native window is created.
        // Guaranteed: core application object exists, configuration and logging ready.
        // Plugin actions: override window creation parameters (size, title, vsync) via
        // attribute callbacks, or register early input handlers.
        // Constraints: No window handle, no graphics context, no event loop.
        pre_application,

        // After native window creation, before event subscriptions are added.
        // Guaranteed: Native window handle exists (HWND, X11 Window, etc.).
        // Plugin actions: attach low-level window hooks, initialize graphics API (Vulkan/GL),
        // or set up platform‑specific resources tied to the window.
        // Constraints: Event bus not yet subscribed to window close events; application main loop not running.
        post_window,

        // End of application constructor – all core systems are ready.
        // Guaranteed: Event bus operational, window shown (or ready to show), FPS controller configured.
        // Plugin actions: start background threads, register deferred work, or allocate resources
        // that depend on the fully constructed application.
        // Constraints: Main loop not yet started; no frame processing.
        application_ready,

        // ----- MAIN LOOP ---------------------------------------------------------------------------------------------

        // Inside application::run(), before the window is shown and before the main loop begins.
        // Guaranteed: Application is about to enter its run state, window is still hidden.
        // Plugin actions: show splash screens, perform one‑time pre‑loop setup (e.g., allocate frame buffers),
        // or subscribe to per‑frame events.
        // Constraints: No frames have been processed; input events not yet polled.
        pre_application_run,

        // After the main loop exits (application::run() ends), but before destruction starts.
        // Guaranteed: Main loop terminated, window may still be visible, but no more frame updates.
        // Plugin actions: save runtime state, flush logs, release frame‑specific resources,
        // or display a shutdown screen.
        // Constraints: Window still exists, event bus active, but no new events are being processed.
        post_application_run,

        // ----- SHUTDOWN SEQUENCE -------------------------------------------------------------------------------------

        // Beginning of application destructor – before the native window is destroyed.
        // Guaranteed: Application object alive, window still fully functional.
        // Plugin actions: notify remote services of shutdown, unregister window callbacks,
        // or perform any last operations that require a valid window handle.
        // Constraints: After this phase, the window will be destroyed and many resources invalidated.
        pre_application_shutdown,

        // After the native window is destroyed, before the logging subsystem shuts down.
        // Guaranteed: Window handle released, but logger and crash handler still active.
        // Plugin actions: clean up graphics resources, release plugin‑specific OS handles,
        // or write final diagnostic information to the log.
        // Constraints: No windowing operations possible; event bus may still exist.
        post_window_destroy,

        // After plugin_manager::shutdown() has been called.
        // Guaranteed: All plugins have been unloaded, no further plugin callbacks will be invoked.
        // Plugin actions: This phase is mostly informational – plugins are already unloaded,
        // so no actions from plugins themselves are possible. Reserved for the engine’s own cleanup.
        // Constraints: No plugin code should run; the phase exists for engine consistency.
        post_application_shutdown,

        // ----- GLOBAL CLEANUP ----------------------------------------------------------------------------------------

        // After application destruction, but before plugin_manager::shutdown().
        // Guaranteed: Application gone, but plugin system still holds loaded plugin instances.
        // Plugin actions: perform cross‑plugin finalization that requires other plugins to be still alive,
        // or flush any remaining shared state.
        // Constraints: No window, no event system, no logging (if LOG_SHUTDOWN already called).
        final_cleanup,
    };


    // Identifies which engine subsystem a plugin replaces or extends.
    // The plugin manager uses this to enforce that only one plugin per interface is active
    // (the one selected in the configuration file). Plugins with `custom` are free‑form.
    enum class interface : u16 {
        none = 0,
        logger,
        window,
        memory_manager,
        configuration,
        input_system,
        virtual_file_system,
        renderer,
        asset_registry,
        resource_cache,
        ecs,
        scene_manager,
        physics,
        audio,
        scripting,
        ui_system,
        editor_core,
        networking,
        online_subsystem,
        game_loop,
        custom,                     // needs to be last!
    };

    // Descriptor that every plugin must export via `gluttony_plugin_descriptor()`.
    // It provides the plugin manager with metadata: name, load phase, interface target,
    // and dependencies (by name or by interface). Dependencies are resolved automatically
    // during the load phase.
    struct plugin_descriptor {
        const char*                     name{};                     // Human‑readable plugin name, must be unique.
        load_phase                      phase{};                    // When this plugin should be loaded.
        interface              target{};                   // Which core interface this plugin implements.
        int                             dependency_names_count{};   // Number of plugins this plugin depends on (by name).
        const char* const*              dependency_names{};         // Array of plugin names this plugin requires.
        int                             dependency_interface_count{}; // Number of interface dependencies.
        const interface*       dependency_interfaces{};    // Array of interfaces this plugin depends on.
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    
    // Base interface for all plugins. Every plugin implements on_load() and on_unload(),
    // which are called at the appropriate moments. The plugin manager manages the lifecycle.
    class i_plugin {
    public:

        virtual ~i_plugin() = default;

        // Called when the module is loaded and ready to hook into the engine.
        // Use this to register systems, bind events, extend menus, etc.
        virtual void on_load() = 0;

        // Called when the engine shuts down or the plugin is unloaded.
        // Clean up everything you registered in onLoad().
        virtual void on_unload() = 0;


        virtual void update(const GLT::update_event&) {}

    };

}
