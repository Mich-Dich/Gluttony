
#include "util/pch.h"

#if defined(PLATFORM_LINUX)
    #include <dlfcn.h>
#elif defined(PLATFORM_WINDOWS)
    #include <windows.h>
#endif

#include "util/util.h"
#include "util/io/serializer_yaml.h"
#include "util/io/vfs.h"
#include "util/io/directory_iterator.h"
#include "config/config.h"
#include "plugin_manager.h"



// FORWARD DECLARATIONS ================================================================================================


namespace GLT::plugin_manager {

    // CONSTANTS =======================================================================================================

    #if defined(PLATFORM_LINUX)

        constexpr std::string_view          DYNAMIC_LIB_EXTENTION = ".so";

    #elif defined(PLATFORM_WINDOWS)

        constexpr std::string_view          DYNAMIC_LIB_EXTENTION = ".dll";
        
    #endif

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    using underlying = std::underlying_type_t<interface>;


    // Internal handle for a loaded plugin.
    struct plugin_handle {

        std::string                                         name{};
        std::filesystem::path                               path{};
        void*                                               module_handle = nullptr;
        std::shared_ptr<i_plugin>                           instance;   // uses custom deleter
        phase                                               load_phase{};
        phase                                               unload_phase{};
        interface                                           target{};
        std::vector<std::string>                            dependencies_names{};
        std::vector<interface>                              dependencies_interfaces{};
    };


    // TODO: add descriptive comment
    struct discovered_info {

        std::filesystem::path                               path{};
        std::string                                         name{};
        phase                                               load_phase{};
        phase                                               unload_phase{};
        interface                                           target{};               // is a [u16]
        std::vector<std::string>                            dependencies_names{};
        std::vector<interface>                              dependencies_interfaces{};
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

    static std::atomic<bool>                                            s_shutdown{false};

    static std::atomic<u64>                                             s_generation{0};

    static std::vector<plugin_handle>                                   s_loaded_plugins{};

    static std::unordered_map<std::string, std::weak_ptr<i_plugin>>     s_loaded_by_name{}; // O(1) name → owning-ref lookup. Populated on load, erased on unload.

    static std::vector<discovered_info>                                 s_discovered{};

    // Dense array instead of unordered_map<interface, string>: no hashing, no allocation.
    static std::array<std::string, INTERFACE_CACHE_SIZE>                s_plugin_names_per_target_interface{};

    static std::filesystem::path                                        s_config_path{};


    // Hot-path cache — defined here, declared extern in plugin_manager.h
    namespace detail {

        std::array<std::atomic<i_plugin*>, INTERFACE_CACHE_SIZE> g_interface_raw_cache{};
    }

    // HELPER FUNCTIONS ===============================================================================================

    #if defined(PLATFORM_LINUX)

        void* load_library(const char* path, lib_load_mode mode) {
            int flags = (mode == lib_load_mode::now) ? RTLD_NOW : RTLD_LAZY;
            return dlopen(path, flags);
        }


        void* get_symbol(void* handle, const char* name) {
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


        void* get_symbol(void* handle, const char* name) {
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

        auto it = std::find_if(s_loaded_plugins.begin(), s_loaded_plugins.end(),
            [&](const plugin_handle& h) { return h.name == info.name; });
        if (it != s_loaded_plugins.end())
            return plugin_load_error::already_loaded;

        void* handle = load_library(info.path.c_str(), lib_load_mode::now);
        if (!handle) {
            const auto error_str = get_dynamic_library_error();
            LOG(error, "Plugin load failed [{}]", error_str);
            return plugin_load_error::failed_to_load;
        }

        auto create = reinterpret_cast<create_plugin_func>(get_symbol(handle, "create_plugin"));
        auto destroy = reinterpret_cast<destroy_plugin_func>(get_symbol(handle, "destroy_plugin"));
        if (!create || !destroy) {
            LOG(error, "Plugin {} missing required symbols.", info.name.c_str());
            free_library(handle);
            return plugin_load_error::failed_to_find_factory_functions;
        }

        i_plugin* raw_instance = create();
        if (!raw_instance) {
            free_library(handle);
            return plugin_load_error::failed_to_create_instance;
        }

        struct deleter {
            void operator()(i_plugin* p) const {
                destroy_function(p);
                if (handle) free_library(handle);
            }
            destroy_plugin_func destroy_function;
            void* handle;
        };

        auto instance = std::shared_ptr<i_plugin>(raw_instance, deleter{ destroy, handle });

        // --- cache population (BEFORE on_load, so plugins can self-look-up) ---
        if (info.target != interface::custom) {
            detail::g_interface_raw_cache[static_cast<size_t>(info.target)]
                .store(instance.get(), std::memory_order_release);
        }
        s_loaded_by_name[info.name] = instance;

        instance->on_load();

        s_loaded_plugins.push_back(plugin_handle{
            .name = info.name,
            .path = info.path,
            .module_handle = handle,
            .instance = instance,
            .load_phase = info.load_phase,
            .unload_phase = info.unload_phase,
            .target = info.target,
            .dependencies_names = info.dependencies_names,
            .dependencies_interfaces = info.dependencies_interfaces,
        });

        s_generation.fetch_add(1, std::memory_order_acq_rel);
        GLT::logger::flush_buffer();
        return plugin_load_error::none;
    }


    // Checks if all dependencies (by name) are already loaded.
    static bool dependencies_satisfied(const discovered_info& info) {

        // Name dependencies
        for (const auto& dep_name : info.dependencies_names) {
            auto it = s_loaded_by_name.find(dep_name);
            if (it == s_loaded_by_name.end() || it->second.expired())
                return false;
        }

        // Interface dependencies
        for (auto iface_dep : info.dependencies_interfaces) {

            if (iface_dep == interface::none)
                continue;

            if (iface_dep == interface::custom) {
                LOG(warn, "Plugin [{}] has unsatisfiable dependency on interface::custom (shared slot)", info.name);
                return false;
            }
            if (detail::g_interface_raw_cache[static_cast<size_t>(iface_dep)].load(std::memory_order_acquire) == nullptr)
                return false;
        }

        return true;
    }


    void serialize(const std::filesystem::path& config_path, serializer::option option) {

        GLT::serializer::yaml plugin_serializer(config_path, "plugin_settings", option);
        for (GLT::plugin_manager::interface target : GLT::util::enum_values<GLT::plugin_manager::interface>) {

            const auto idx = static_cast<size_t>(target);

            std::string buffer = "unknown";
            if (option == serializer::option::save)
                buffer = s_plugin_names_per_target_interface[idx];

            plugin_serializer.entry(std::string(GLT::util::enum_to_string(target)), buffer);

            if (option == serializer::option::load)
                s_plugin_names_per_target_interface[idx] = buffer;
        }
    }

    // CLASS/FUNCTION IMPLEMENTATION ==================================================================================

    // PUBLIC API =====================================================================================================

    void discover_plugins(const std::filesystem::path& project_dir_path) {

        const auto plugin_dir = GLT::util::get_executable_path() / config::PLUGIN_DIR;
        std::error_code error{};
        VALIDATE(GLT::vfs::exists(plugin_dir, error) && !error, return, "", "Plugin dir is invalid [{}]", (plugin_dir))

        s_config_path = project_dir_path / (GLT::config::PROJECT_NAME + GLT::config::PROJECT_EXTENTION);
        if (GLT::vfs::exists(s_config_path, error) && !error)
            serialize(s_config_path, serializer::option::load);
        else
            LOG(error, "Project file does not exist [{}]: [{}]", project_dir_path.generic_string(), error.message())

        s_discovered.clear();

        std::unordered_map<interface, discovered_info> first_candidate_per_interface{};
        std::unordered_set<interface> preferred_plugin_found{};

        GLT::vfs::recursive_directory_iterator iterator(plugin_dir, GLT::vfs::directory_options::skip_permission_denied, error);
        VALIDATE(!error, return, "", "Fail to create recursive_directory_iterator for [{}]", (plugin_dir.generic_string()))

        for (const auto& entry : iterator) {

            const bool regular_file = entry.is_regular_file(error);
            if (!regular_file || error)
                continue;

            const auto& path = entry.path();
            if (path.extension() != DYNAMIC_LIB_EXTENTION)
                continue;

            void* handle = load_library(path.c_str(), lib_load_mode::lazy);
            VALIDATE(handle, continue, "", "load_library failed: [{}]", (get_dynamic_library_error()))

            auto desc_fn = reinterpret_cast<descriptor_func>(get_symbol(handle, "gluttony_plugin_descriptor"));
            VALIDATE(desc_fn, free_library(handle); continue, "", "get_symbol failed: [{}]",
                (get_dynamic_library_error()))

            const plugin_descriptor* desc = desc_fn();
            VALIDATE(desc, continue; free_library(handle), "", "Failed to load descriptor function")

            const interface   iface = desc->target;
            const std::string plugin_name = desc->name ? std::string(desc->name) : path.stem().string();

            discovered_info info{
                .path = path,
                .name = plugin_name,
                .load_phase = desc->load_phase,
                .unload_phase = desc->unload_phase,
                .target = iface,
            };

            info.dependencies_names.reserve(desc->dependency_names_count);
            for (int i = 0; i < desc->dependency_names_count; i++)
                if (desc->dependency_names && desc->dependency_names[i])
                    info.dependencies_names.emplace_back(desc->dependency_names[i]);

            info.dependencies_interfaces.reserve(desc->dependency_interface_count);
            for (int i = 0; i < desc->dependency_interface_count; i++)
                if (desc->dependency_interfaces)
                    info.dependencies_interfaces.push_back(desc->dependency_interfaces[i]);

            free_library(handle);

            if (iface == interface::custom) {
                s_discovered.push_back(std::move(info));
                continue;
            }

            const auto idx = static_cast<size_t>(iface);
            const std::string& preferred_plugin_name = s_plugin_names_per_target_interface[idx];
            const bool has_preference = !preferred_plugin_name.empty() && preferred_plugin_name != "unknown";

            if (!has_preference) {

                LOG(trace, "No preferred plugin for interface [{}], using first found [{}]", GLT::util::enum_to_string(iface), plugin_name)
                s_plugin_names_per_target_interface[idx] = plugin_name;
                s_discovered.push_back(std::move(info));
            
            } else if (plugin_name == preferred_plugin_name) {

                preferred_plugin_found.insert(iface);
                s_discovered.push_back(std::move(info));

            } else {
                first_candidate_per_interface.try_emplace(iface, std::move(info));
            }
        }

        for (auto& [iface, info] : first_candidate_per_interface) {

            if (preferred_plugin_found.contains(iface))
                continue;

            const auto idx = static_cast<size_t>(iface);
            const std::string missing = s_plugin_names_per_target_interface[idx].empty() 
                ? std::string{"<unset>"} : s_plugin_names_per_target_interface[idx];

            LOG(warn, "Preferred plugin [{}] for interface [{}] was not found; falling back to [{}]",
                missing, GLT::util::enum_to_string(iface), info.name)

            s_plugin_names_per_target_interface[idx] = info.name;
            s_discovered.push_back(std::move(info));
        }

        serialize(s_config_path, serializer::option::save);
        GLT::logger::flush_buffer();
        s_shutdown.store(false, std::memory_order_relaxed);
    }


    void load_plugins(const phase current_phase) {

        if (s_shutdown.load(std::memory_order_relaxed))
            return;

        LOG(trace, "loading plugins for load phase [{}]", GLT::util::enum_to_string(current_phase))

        std::vector<discovered_info> pending;
        for (const auto& plugin : s_discovered) {

            if (plugin.load_phase != current_phase)
                continue;
            if (s_loaded_by_name.contains(plugin.name))
                continue;
            pending.push_back(plugin);
        }

        bool progress = true;
        while (!pending.empty() && progress) {
            progress = false;
            for (auto it = pending.begin(); it != pending.end(); ) {

                if (dependencies_satisfied(*it)) {
                    const auto load_error = load_single(*it);
                    switch (load_error) {

                        case plugin_load_error::none:
                            it = pending.erase(it);
                            progress = true;
                            break;

                        case plugin_load_error::failed_to_load:                     [[fallthrough]];
                        case plugin_load_error::failed_to_find_factory_functions:   [[fallthrough]];
                        case plugin_load_error::failed_to_create_instance:
                            LOG(error, "Failed to load plugin [{}]: [{}]", it->name, static_cast<int>(load_error));
                            it = pending.erase(it);
                            break;

                        case plugin_load_error::already_loaded:
                            it = pending.erase(it);
                            break;

                        default: break;
                    }
                } else
                    it++;
            }
        }

        if (!pending.empty())
            for (const auto& p : pending)
                LOG(error, "Plugin [{}] could not be loaded: unsatisfied dependencies or load error", p.name);

        GLT::logger::flush_buffer();
    }


    void unload_plugins(const phase current_phase) {

        for (size_t i = s_loaded_plugins.size(); i > 0; --i) {

            auto& handle = s_loaded_plugins[i - 1];
            if (handle.unload_phase != current_phase)
                continue;

            // Notify plugin while its caches are still valid (self-lookup is legal in on_unload)
            if (handle.instance) handle.instance->on_unload();

            // Invalidate caches BEFORE the refcount drops so other unloads in the same phase see fresh state
            if (handle.target != interface::custom)
                detail::g_interface_raw_cache[static_cast<size_t>(handle.target)].store(nullptr, std::memory_order_release);
            s_loaded_by_name.erase(handle.name);

            // Release the library
            if (handle.instance)
                handle.instance.reset();
            else if (handle.module_handle)
                free_library(handle.module_handle);

            s_loaded_plugins.erase(s_loaded_plugins.begin() + (i - 1));
            s_generation.fetch_add(1, std::memory_order_acq_rel);
        }

        GLT::logger::flush_buffer();
    }


    void shutdown() {

        serialize(s_config_path, serializer::option::save);

        if (s_shutdown.exchange(true, std::memory_order_acq_rel))
            return;

        for (auto it = s_loaded_plugins.rbegin(); it != s_loaded_plugins.rend(); ++it) {

            if (it->instance)
                it->instance->on_unload();

            if (it->target != interface::custom)
                detail::g_interface_raw_cache[static_cast<size_t>(it->target)].store(nullptr, std::memory_order_release);

            if (it->instance)
                it->instance.reset();
            else if (it->module_handle)
                free_library(it->module_handle);
        }

        s_loaded_plugins.clear();
        s_loaded_by_name.clear();
        s_discovered.clear();
        s_generation.fetch_add(1, std::memory_order_acq_rel);
    }


    [[nodiscard]] ref<i_plugin> get_plugin_base(const std::string& name) {
        auto it = s_loaded_by_name.find(name);
        if (it == s_loaded_by_name.end())
            return {};
        return it->second.lock();
    }


    [[nodiscard]] ref<i_plugin> get_plugin_base(const interface targeted) {

        if (targeted == interface::custom)
            return {};                 // shared slot — use name lookup
        const auto& name = s_plugin_names_per_target_interface[static_cast<size_t>(targeted)];
        if (name.empty())
            return {};

        auto it = s_loaded_by_name.find(name);
        if (it == s_loaded_by_name.end())
            return {};
        return it->second.lock();
    }


    [[nodiscard]] u64 get_generation() noexcept { return s_generation.load(std::memory_order_acquire); }

}
