#pragma once

#include "module/module.h"

// Engine side of game modules: find, check, load and unload the project's built modules, build them, and tell the
// engine systems ("participants") when a module comes and goes.
//
//   <project>/CMakeLists.txt           project's build script (template: documentation/game_module.md)
//   <project>/game/                    the module's sources
//   <project>/build/<config>/bin/      built modules (<config>: debug, release, release_with_debug_info)
//   <project>/build/<config>/loaded/   copies the engine actually loads (the build can overwrite bin/ while loaded)
//
// The engine loads every module in bin/ of its own configuration after its plugins (application_ready) and unloads
// them before the plugins go (pre_application_shutdown). All functions: main thread only



namespace GLT::modules {

    // TYPES ===========================================================================================================

    // @brief An engine system that holds things registered by modules
    //
    // [after_load] runs after the module's own load step, [release] after its unload step and before the library is
    // closed: drop everything registered under [id] and everything that points into the module's code (descriptors
    // holding std::function, objects with vtables in the module, running tasks). Participants run in the order they
    // were added; release in reverse order. Built in: scripting, procedural construction, editor
    struct participant {

        std::string                                     name{};
        std::function<void(std::string_view id)>        after_load{};
        std::function<void(std::string_view id)>        release{};
    };


    struct module_info {

        std::string                                     id{};
        std::string                                     root_namespace{};
        std::filesystem::path                           library_path{};     // in bin/
        std::filesystem::path                           loaded_path{};      // the copy in loaded/
        std::filesystem::file_time_type                 library_time{};     // of library_path when it was loaded
    };


    struct build_status {

        bool                                            running = false;
        bool                                            last_ok = false;
        std::string                                     last_output{};      // cmake output of the last build
    };

    // FUNCTION DECLARATION ============================================================================================

    // Adds a participant (replaces one with the same name). It is told about modules loaded from now on
    void add_participant(participant p);

    void remove_participant(std::string_view name);


    // <project>/build/<config>/bin for the engine's configuration (or [config])
    [[nodiscard]] std::filesystem::path output_dir(const std::filesystem::path& project_dir,
        build_config config = current_build_config());


    // Libraries in output_dir() that export a module descriptor
    [[nodiscard]] std::vector<std::filesystem::path> find_modules(const std::filesystem::path& project_dir);


    // @brief Loads every module of the project (find_modules). Returns the number loaded
    u32 load_project_modules(const std::filesystem::path& project_dir);


    // @brief Loads one module library. Refuses (and logs why) a library without descriptor, with a different ABI stamp
    //        or with an id that is already loaded
    [[nodiscard]] std::expected<void, std::string> load_module(const std::filesystem::path& library,
        const std::filesystem::path& project_dir);


    // @brief Unload step of the module, release of all participants, then the library is closed
    void unload_module(std::string_view id);


    // Unloads all modules, last loaded first
    void unload_all();


    [[nodiscard]] std::vector<module_info> loaded_modules();


    // True if a module's file in bin/ is newer than the copy that is loaded (a build finished since)
    [[nodiscard]] bool has_newer_build(std::string_view id);


    // Engine side install: built-in participants (scripting, construct, editor) and the editor menu (Tools > Game module).
    // Called by the application after its plugins are loaded; remove() before they unload (unloads all modules)
    void install();

    void remove();

    // Build -----------------------------------------------------------------------------------------------------------

    // The commands build() runs: configure (only when the build directory has no CMakeCache.txt) and build
    [[nodiscard]] std::vector<std::string> build_commands(const std::filesystem::path& project_dir,
        build_config config = current_build_config());


    // @brief Configures and builds the project's modules in the background (cmake, output to the log)
    //
    // [on_done] runs on the main thread (thread_pool::pump_main_thread) with the result. Returns false if a build is
    // already running or the project has no CMakeLists.txt
    bool build(const std::filesystem::path& project_dir, std::function<void(bool ok)> on_done = {},
        build_config config = current_build_config());


    [[nodiscard]] build_status get_build_status();

}
