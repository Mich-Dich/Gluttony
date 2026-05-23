
#include "util/pch.h"

#if defined(PLATFORM_LINUX)
    #include <dlfcn.h>
#elif defined(PLATFORM_WINDOWS)
    #include <windows.h>
#endif

#include "util/io/serializer_yaml.h"
#include "config/config.h"
#include "plugin_manager.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::plugin_manager {

    // CONSTANTS =======================================================================================================

    #if defined(PLATFORM_LINUX)

        constexpr std::string_view          dynamic_lib_extention = ".so";

    #elif defined(PLATFORM_WINDOWS)

        constexpr std::string_view          dynamic_lib_extention = ".dll";
        
    #endif

    
    constexpr std::array<std::string_view, static_cast<size_t>(targeted_interface::custom) +1> s_targeted_interface_names = {
        "none", 
        "logger", 
        "window", 
        "memory_manager", 
        "configuration", 
        "input_system", 
        "virtual_file_system", 
        "renderer", 
        "asset_registry", 
        "resource_cache", 
        "ecs", 
        "scene_manager", 
        "physics", 
        "audio", 
        "scripting", 
        "ui_system", 
        "editor_core", 
        "networking", 
        "online_subsystem", 
        "custom",
    };

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    using underlying = std::underlying_type_t<targeted_interface>;


    // Internal handle for a loaded plugin.
    struct plugin_handle {
        std::string                             name;
        std::filesystem::path                   path;
        void*                                   module_handle = nullptr;
        std::shared_ptr<i_plugin>               instance;   // uses custom deleter
        load_phase                              phase;
        std::vector<std::string>                dependencies_names;
        std::vector<targeted_interface>         dependencies_interfaces;
    };


    // TODO: add descriptive comment
    struct discovered_info {
        std::filesystem::path                                   path;
        std::string                                             name;
        load_phase                                              phase;
        targeted_interface                                      target{};               // is a [u16]
        std::vector<std::string>                                dependencies_names;
        std::vector<targeted_interface>                         dependencies_interfaces;
    };


    // C‑style function signatures exported by plugins.
    using create_plugin_func = i_plugin* (*)();


    // TODO: add descriptive comment
    using destroy_plugin_func = void (*)(i_plugin*);


    // Optional descriptor function: if present, it's called after load_library to get metadata.
    using descriptor_func = const plugin_descriptor* (*)();


    enum class plugin_load_error : u8 {
        none = 0,
        already_loaded,
        failed_to_load,
        failed_to_find_factory_functions,
        failed_to_create_instance,
    };

    
    enum class lib_load_mode {
        now = 0,
        lazy
    };

    // STATIC VARIABLES ================================================================================================

    static bool                                                 s_shutdown = false;
    static std::vector<plugin_handle>                           s_loaded_plugins{};
    static std::vector<discovered_info>                         s_discovered{};
    static std::unordered_map<targeted_interface, std::string>  s_plugin_names_per_target_interface{};
    static std::filesystem::path                                s_config_path{};

    // HELPER FUNCTIONS ===============================================================================================

    #if defined(PLATFORM_LINUX)

        void* load_library(const char* path, lib_load_mode mode) {
            int flags = (mode == lib_load_mode::now) ? RTLD_NOW : RTLD_LAZY;
            return dlopen(path, flags);
        }


        void* get_function(void* handle, const char* name) {
            return dlsym(handle, name);
        }


        void free_library(void* handle) {
            dlclose(handle);
        }


        std::string get_dynamic_library_error() {
            const char* msg = dlerror();
            return msg ? std::string(msg) : "unknown error";
        }

    #elif defined(PLATFORM_WINDOWS)

        void* load_library(const wchar_t* path, lib_load_mode /*mode*/) {
            return (void*)LoadLibraryW(path);
        }


        void* get_function(void* handle, const char* name) {
            return GetProcAddress((HMODULE)handle, name);
        }


        void free_library(void* handle) {
            FreeLibrary((HMODULE)handle);
        }


        std::string get_dynamic_library_error() {
            DWORD error = GetLastError();
            char* msg = nullptr;
            FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                error,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPSTR)&msg,
                0,
                nullptr
            );
            std::string result(msg ? msg : "unknown error");
            if (msg) LocalFree(msg);
            return result;
        }

    #endif

    // Load a plugin described by `info`, resolve symbols, create instance, call on_load().
    static plugin_load_error load_single(const discovered_info& info) {

        // Check if already loaded (by name).
        auto it = std::find_if(s_loaded_plugins.begin(), s_loaded_plugins.end(),
            [&](const plugin_handle& h) { return h.name == info.name; });
        if (it != s_loaded_plugins.end()) {
            return plugin_load_error::already_loaded;
        }

        void* handle = load_library(info.path.c_str(), lib_load_mode::now);
        if (!handle) {
            const auto error_str = get_dynamic_library_error();
            LOG(error, "Plugin load failed: %s", error_str);     // Log error and fail.
            return plugin_load_error::failed_to_load;
        }

        // Get factory functions.
        auto create = reinterpret_cast<create_plugin_func>(get_function(handle, "create_plugin"));
        auto destroy = reinterpret_cast<destroy_plugin_func>(get_function(handle, "destroy_plugin"));
        if (!create || !destroy) {
            LOG(error, "Plugin '%s' missing required symbols.", info.name.c_str());
            free_library(handle);
            return plugin_load_error::failed_to_find_factory_functions;
        }

        i_plugin* raw_instance = create();                              // Create the plugin instance.
        if (!raw_instance) {
            free_library(handle);
            return plugin_load_error::failed_to_create_instance;
        }

        // Wrap in shared_ptr with a custom deleter that calls destroy_plugin,
        // then (if the shared_ptr is the last owner) free_library.
        // We need to capture the handle and keep it alive as long as the instance exists.
        struct deleter {
            void operator()(i_plugin* p) const {
                destroy_function(p);
                if (handle) free_library(handle);
            }
            destroy_plugin_func destroy_function;
            void* handle;
        };

        auto instance = std::shared_ptr<i_plugin>(raw_instance, deleter{ destroy, handle });
        instance->on_load();                                            // Call the startup hook.
        s_loaded_plugins.push_back(plugin_handle{                       // Store the handle.
            .name                       = info.name,
            .path                       = info.path,
            .module_handle              = handle,
            .instance                   = instance,
            .phase                      = info.phase,
            .dependencies_names         = info.dependencies_names,
            .dependencies_interfaces    = info.dependencies_interfaces,
        });
        
        GLT::logger::flush_buffer();
        return plugin_load_error::none;
    }


    // Checks if all dependencies (by name) are already loaded.
    static bool dependencies_satisfied(const discovered_info& info) {

        // Check name dependencies
        for (const auto& dep_name : info.dependencies_names) {
            bool found = false;
            for (const auto& loaded : s_loaded_plugins) {
                if (loaded.name == dep_name) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }

        // Check interface dependencies
        for (auto iface_dep : info.dependencies_interfaces) {

            if (iface_dep == targeted_interface::none)
                continue;

            auto it = s_plugin_names_per_target_interface.find(iface_dep);
            if (it == s_plugin_names_per_target_interface.end())
                return false;   // no plugin configured for this interface
            const std::string& required_plugin_name = it->second;

            bool found = false;
            for (const auto& loaded : s_loaded_plugins) {
                if (loaded.name == required_plugin_name) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }

        return true;
    }

    
    constexpr std::string to_string(targeted_interface targeted) {

        auto targeted_index = static_cast<size_t>(targeted);
        if (targeted_index < s_targeted_interface_names.size())
            return std::string(s_targeted_interface_names[targeted_index]);
        return "unknown";
    }


    void serialize(const std::filesystem::path& config_path, serializer::option option) {
        
        GLT::serializer::yaml plugin_serializer(config_path, "plugin_settings", option);
        for (underlying index = 0; index < static_cast<underlying>(targeted_interface::custom); index++) {
            auto targeted = static_cast<targeted_interface>(index);
            std::string buffer = "unknown";

            if (option == serializer::option::save_to_file)
                buffer = s_plugin_names_per_target_interface[targeted];

            plugin_serializer.entry(to_string(targeted), buffer);

            if (option == serializer::option::load_from_file)
                s_plugin_names_per_target_interface[targeted] = buffer;
        }
    }           // serializer dies here

    // CLASS/FUNCTION IMPLEMENTATION ==================================================================================

    // PUBLIC API =====================================================================================================

    void discover_plugins() {

        const auto plugin_dir = GLT::util::get_executable_path() / config::PLUGIN_DIR;
        VALIDATE(std::filesystem::exists(plugin_dir), return, "", "Plugin dir is invalid [{}]", plugin_dir)

        s_config_path = GLT::util::get_executable_path() / GLT::config::get_filepath_from_config_type(GLT::config::type::plugin);
        serialize(s_config_path, serializer::option::load_from_file);       // load settings

        // discover plugins             TODO: only discover plugins that fit the config
        s_discovered.clear();
        for (const auto& entry : std::filesystem::recursive_directory_iterator(plugin_dir)) {

            if (!entry.is_regular_file())
                continue;

            const auto& path = entry.path();
            if (path.extension() != dynamic_lib_extention)
                continue;

            // Temporarily load the library to get the descriptor.
            void* handle = load_library(path.c_str(), lib_load_mode::lazy);
            VALIDATE(handle, continue, "", "load_library failed: [{}]", get_dynamic_library_error());

            auto desc_fn = reinterpret_cast<descriptor_func>(get_function(handle, "gluttony_plugin_descriptor"));
            VALIDATE(desc_fn, free_library(handle); continue, "", "get_function failed: [{}]", get_dynamic_library_error());

            const plugin_descriptor* desc = desc_fn();
            VALIDATE(desc, continue; free_library(handle), "", "Failed to load descriptor function");

            // Filter based on user configuration 
            targeted_interface iface = desc->target;                                // Retrieve the interface this plugin targets.
            const auto it = s_plugin_names_per_target_interface.find(iface);        // Look up what the user configured for this interface.
            const bool has_config = (it != s_plugin_names_per_target_interface.end());
            const std::string plugin_name = desc->name ? std::string(desc->name) : path.stem().string();
            std::string preferred_plugin_name = has_config ? it->second : "";

            // Reject the plugin if [configured name] != [this plugin’s name]
            if (!has_config || preferred_plugin_name.empty() || preferred_plugin_name == "unknown") {

                LOG(trace, "No preferred plugin found for interface [{}], setting to first found [{}]", to_string(iface), plugin_name)
                s_plugin_names_per_target_interface[iface] = plugin_name;
            
            } else if (plugin_name != preferred_plugin_name) {                             // Not the user’s chosen plugin – skip.

                free_library(handle);
                continue;
            }

            // Plugin passes the filter
            discovered_info info {
                .path   = path,
                .name   = plugin_name,
                .phase  = desc->phase,
                .target = iface,
            };

            // Name dependencies
            info.dependencies_names.reserve(desc->dependency_names_count);
            for (int i = 0; i < desc->dependency_names_count; ++i) {
                if (desc->dependency_names && desc->dependency_names[i])
                    info.dependencies_names.emplace_back(desc->dependency_names[i]);
            }

            // Interface dependencies
            info.dependencies_interfaces.reserve(desc->dependency_interface_count);
            for (int i = 0; i < desc->dependency_interface_count; ++i) {
                if (desc->dependency_interfaces)
                    info.dependencies_interfaces.push_back(desc->dependency_interfaces[i]);
            }

            s_discovered.push_back(std::move(info));
            free_library(handle);   // close temporary handle
        }

        GLT::logger::flush_buffer();
    }


    void load_plugins(const load_phase currentPhase) {

        if (s_shutdown)
            return;

        // Filter discovered plugins by phase and not yet loaded.
        std::vector<discovered_info> pending;
        for (const auto& plugin : s_discovered) {

            if (plugin.phase != currentPhase)
                continue;
                
            // Check if already loaded (shouldn't be, but safe).
            if (std::any_of(s_loaded_plugins.begin(), s_loaded_plugins.end(),
                    [&](const plugin_handle& h) { return h.name == plugin.name; })) {
                continue;
            }
            pending.push_back(plugin);
        }

        // Repeatedly load plugins whose dependencies are satisfied.
        bool progress = true;
        while (!pending.empty() && progress) {
            progress = false;
            for (auto it = pending.begin(); it != pending.end(); ) {
                if (dependencies_satisfied(*it)) {
                    plugin_load_error load_error = load_single(*it);

                    switch (load_error) {

                        case plugin_load_error::none: {
                            it = pending.erase(it);
                            progress = true;       // we made progress, keep trying others
                            break;
                        }
                        case plugin_load_error::failed_to_load:                             [[fallthrough]];
                        case plugin_load_error::failed_to_find_factory_functions:           [[fallthrough]];
                        case plugin_load_error::failed_to_create_instance: {
                            // Permanent failure – remove from list, log, and continue.
                            LOG(error, "Failed to load plugin [{}]: [{}]", it->name, static_cast<int>(load_error));
                            it = pending.erase(it);
                            break;
                        }
                        case plugin_load_error::already_loaded: {
                            it = pending.erase(it);
                            break;
                        }
                        default:                                                            break;
                    }
                } else {
                    ++it;
                }
            }
        }

        if (!pending.empty()) {
            for (const auto& p : pending) {                 // Some plugins could not be loaded due to missing dependencies.
                LOG(error, "Plugin [{}] could not be loaded: unsatisfied dependencies or load error", p.name);
            }
        }

        GLT::logger::flush_buffer();
    }


    void shutdown() {

        serialize(s_config_path, serializer::option::save_to_file);

        if (s_shutdown) return;
        s_shutdown = true;

        // Unload in reverse order to respect dependencies.
        for (auto it = s_loaded_plugins.rbegin(); it != s_loaded_plugins.rend(); ++it) {
            if (it->instance) {
                it->instance->on_unload();
                // The shared_ptr will call its custom deleter, which calls destroy_plugin and free_library.
                it->instance.reset();                       // triggers deletion and library close
            } else if (it->module_handle) {
                free_library(it->module_handle);
            }
        }
        s_loaded_plugins.clear();
        s_discovered.clear();                               // no longer needed
    }


    [[nodiscard]] ref<i_plugin> get_plugin_base(const std::string& name) {

        for (auto& h : s_loaded_plugins) {
            if (h.name == name && h.instance) {
                return h.instance;
            }
        }
        return {};
    }

    
    [[nodiscard]] ref<i_plugin> get_plugin_base(const targeted_interface targeted) {

        auto it = s_plugin_names_per_target_interface.find(targeted);
        if (it != s_plugin_names_per_target_interface.end()) {
            return get_plugin_base(it->second);
        }
        return {};
    }

}
