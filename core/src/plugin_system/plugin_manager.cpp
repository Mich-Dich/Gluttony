
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

        constexpr std::string_view                                      DYNAMIC_LIB_EXTENTION = ".so";

    #elif defined(PLATFORM_WINDOWS)

        constexpr std::string_view                                      DYNAMIC_LIB_EXTENTION = ".dll";
        
    #endif

    static constexpr const char*                                        EXPLICITLY_NONE = "__none__";

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    using underlying = std::underlying_type_t<interface>;


    struct plugin_handle {

        std::string                         name{};
        std::string                         display_name{};
        std::string                         description{};
        std::string                         category{};
        std::string                         author{};
        GLT::version                        plugin_version{};
        GLT::version                        api_version{};
        std::filesystem::path               path{};
        std::filesystem::path               logo_path{};
        void*                               module_handle = nullptr;
        GLT::ref<i_plugin>                  instance{};
        phase                               load_phase{};
        phase                               unload_phase{};
        interface                           target{};
        std::vector<std::string>            dependencies_names{};
        std::vector<interface>              dependencies_interfaces{};
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

    static std::vector<plugin_info>                                     s_discovered{};

    // Dense array instead of unordered_map<interface, string>: no hashing, no allocation.
    static std::array<std::string, INTERFACE_CACHE_SIZE>                s_plugin_names_per_target_interface{};

    static std::filesystem::path                                        s_config_path{};

    static std::atomic<bool>                                            s_config_dirty{false};

    static std::unordered_set<std::string>                              s_disabled_custom_plugins{};

    namespace detail {

        // Hot-path cache — defined here, declared extern in plugin_manager.h
        std::array<std::atomic<i_plugin*>, INTERFACE_CACHE_SIZE>        g_interface_raw_cache{};

    }

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    static plugin_load_error load_single(const plugin_info& info);


    static bool dependencies_satisfied(const plugin_info& info);


    void serialize(const std::filesystem::path& config_path, serializer::option option);


    static std::filesystem::path resolve_logo_path(const std::filesystem::path& lib_path, const std::string& plugin_name);

    // Returns true if any *currently loaded* plugin other than `self` declares a dependency on
    // `dep_name` or on `dep_iface`. Used to block unsafe single-plugin unloads. Direct edges only —
    // transitive dependents are protected by their own direct edge (A→B→C: unloading B is blocked
    // because C depends on B directly).
    static bool has_loaded_dependents(const std::string& self, const std::string& dep_name, interface dep_iface);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    #if defined(PLATFORM_LINUX)

        void* load_library(const char* path, lib_load_mode mode) {

            int flags = (mode == lib_load_mode::now) ? RTLD_NOW : RTLD_LAZY;
            return dlopen(path, flags);
        }


        void* get_symbol(void* handle, const char* name) { return dlsym(handle, name); }


        void free_library(void* handle) { dlclose(handle); }


        std::string get_dynamic_library_error() {

            const char* msg = dlerror();
            return msg ? std::string(msg) : "unknown error";
        }

    #elif defined(PLATFORM_WINDOWS)

        void* load_library(const wchar_t* path, lib_load_mode /*mode*/) { return (void*)LoadLibraryW(path); }


        void* get_symbol(void* handle, const char* name) { return GetProcAddress((HMODULE)handle, name); }


        void free_library(void* handle) { FreeLibrary((HMODULE)handle); }


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
            if (msg)
                LocalFree(msg);
            return result;
        }

    #endif

    // Load a plugin described by `info`, resolve symbols, create instance, call on_load().
    static plugin_load_error load_single(const plugin_info& info) {

        auto it = std::find_if(s_loaded_plugins.begin(), s_loaded_plugins.end(),
            [&](const plugin_handle& h) { return h.name == info.name; });
        if (it != s_loaded_plugins.end())
            return plugin_load_error::already_loaded;

        void* handle = load_library(info.library_path.c_str(), lib_load_mode::now);
        if (!handle) {
            LOG(error, "Plugin load failed [{}]", get_dynamic_library_error());
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
                if (handle)
                    free_library(handle);
            }
            destroy_plugin_func destroy_function;
            void* handle;
        };

        auto instance = std::shared_ptr<i_plugin>(raw_instance, deleter{ destroy, handle });

        // Cache population BEFORE on_load so plugins can self-look-up.
        if (info.target != interface::custom)
            detail::g_interface_raw_cache[static_cast<size_t>(info.target)].store(instance.get(), std::memory_order_release);

        s_loaded_by_name[info.name] = instance;

        instance->on_load();

        s_loaded_plugins.push_back(plugin_handle{
            .name = info.name,
            .display_name = info.display_name,
            .description = info.description,
            .author = info.author,
            .plugin_version = info.plugin_version,
            .api_version = info.api_version,
            .path = info.library_path,
            .logo_path = info.logo_path,
            .module_handle = handle,
            .instance = instance,
            .load_phase = info.load_phase,
            .unload_phase = info.unload_phase,
            .target = info.target,
            .dependencies_names = info.dependencies_names,
            .dependencies_interfaces = info.dependencies_interfaces,
        });

        // Reflect the live state back into the discovery list so the editor sees it.
        for (auto& p : s_discovered)
            if (p.name == info.name) {
                p.is_loaded = true;
                break;
            }

        s_generation.fetch_add(1, std::memory_order_acq_rel);
        GLT::logger::flush_buffer();
        return plugin_load_error::none;
    }


    // Checks if all dependencies (by name) are already loaded.
    static bool dependencies_satisfied(const plugin_info& info) {

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
        for (interface target : GLT::util::enum_values<interface>) {
            const auto idx = static_cast<size_t>(target);
            std::string buffer = "unknown";
            if (option == serializer::option::save)
                buffer = s_plugin_names_per_target_interface[idx];
            plugin_serializer.entry(std::string(GLT::util::enum_to_string(target)), buffer);
            if (option == serializer::option::load)
                s_plugin_names_per_target_interface[idx] = buffer;
        }

        // --- custom interface: separate set of disabled plugin names ---
        std::string custom_disabled_str = "unknown";
        if (option == serializer::option::save) {
            custom_disabled_str.clear();
            for (const auto& name : s_disabled_custom_plugins) {
                if (!custom_disabled_str.empty()) custom_disabled_str += ',';
                custom_disabled_str += name;
            }
        }
        plugin_serializer.entry("__custom_disabled__", custom_disabled_str);
        if (option == serializer::option::load) {
            s_disabled_custom_plugins.clear();
            if (custom_disabled_str != "unknown" && !custom_disabled_str.empty()) {
                size_t start = 0, end;
                while ((end = custom_disabled_str.find(',', start)) != std::string::npos) {
                    auto n = custom_disabled_str.substr(start, end - start);
                    if (!n.empty()) s_disabled_custom_plugins.insert(std::move(n));
                    start = end + 1;
                }
                auto n = custom_disabled_str.substr(start);
                if (!n.empty()) s_disabled_custom_plugins.insert(std::move(n));
            }
        }
    }


    static std::filesystem::path resolve_logo_path(const std::filesystem::path& lib_path, const std::string& plugin_name) {

        auto candidate = lib_path.parent_path() / (plugin_name + ".png");
        std::error_code ec;
        return std::filesystem::exists(candidate, ec) && !ec ? candidate : std::filesystem::path{};
    }


    static bool has_loaded_dependents(const std::string& self, const std::string& dep_name, interface dep_iface) {

        for (const auto& other : s_loaded_plugins) {
            if (other.name == self) continue;

            for (const auto& n : other.dependencies_names)
                if (n == dep_name) return true;

            if (dep_iface != interface::custom && dep_iface != interface::none)
                for (auto iface : other.dependencies_interfaces)
                    if (iface == dep_iface) return true;
        }
        return false;
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

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

        std::unordered_set<interface> resolved_interfaces{};
        std::unordered_map<interface, size_t> first_idx_per_interface{};

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
            VALIDATE(desc, free_library(handle); continue, "", "Failed to load descriptor function")

            const interface iface = desc->target;
            const std::string plugin_name = desc->name ? std::string(desc->name) : path.stem().string();

            plugin_info info{
                .library_path = path,
                .logo_path = resolve_logo_path(path, plugin_name),
                .name = plugin_name,
                .display_name = desc->display_name ? desc->display_name : plugin_name,
                .description = desc->description ? desc->description : "",
                .category = desc->category ? desc->category : "",
                .author = desc->author ? desc->author : "",
                .plugin_version = desc->plugin_version,
                .api_version = desc->api_version,
                .target = iface,
                .load_phase = desc->load_phase,
                .unload_phase = desc->unload_phase,
                .is_enabled = false,
                .is_loaded = false,
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

            // ---- dedup ----
            // The same .so can end up at two paths (leftover build artifact, a stray
            // copy from a previous layout, a hardlink, a debug+release pair built with
            // the same GLT_MODULE_NAME, …). Two entries with the same name cause
            // double cards in the editor AND ImGui ID collisions (both cards push the
            // same PushID). Skip the duplicate and log both paths so the user can
            // clean up the offending copy.
            const auto& existing = std::find_if(s_discovered.begin(), s_discovered.end(),
                [&](const plugin_info& p) { return p.name == plugin_name; });
            if (existing != s_discovered.end()) {
                LOG(warn, "Duplicate plugin [{}] discovered at [{}] (already discovered at [{}]) - skipping",
                    plugin_name, path.generic_string(), existing->library_path.generic_string())
                continue;
            }

            if (iface == interface::custom)
                info.is_enabled = !s_disabled_custom_plugins.contains(plugin_name);

            else {
                const auto idx = static_cast<size_t>(iface);
                const std::string& preferred = s_plugin_names_per_target_interface[idx];
                const bool explicitly_none = (preferred == EXPLICITLY_NONE);
                const bool has_preference  = !preferred.empty() && preferred != "unknown" && !explicitly_none;

                if (explicitly_none) {
                    info.is_enabled = false;
                    resolved_interfaces.insert(iface);              // don't fall back to first found

                } else if (has_preference) {

                    if (plugin_name == preferred) {
                        info.is_enabled = true;
                        resolved_interfaces.insert(iface);
                    }

                } else {

                    LOG(trace, "No preferred plugin for interface [{}], using first found [{}]",
                        GLT::util::enum_to_string(iface), plugin_name)
                    s_plugin_names_per_target_interface[idx] = plugin_name;
                    info.is_enabled = true;
                    resolved_interfaces.insert(iface);
                }

                first_idx_per_interface.try_emplace(iface, s_discovered.size());
            }

            s_discovered.push_back(std::move(info));
        }

        // --- fallback pass: interfaces whose preferred plugin isn't on disk ---
        for (auto& [iface, first_idx] : first_idx_per_interface) {

            if (resolved_interfaces.contains(iface))
                continue;

            const auto iface_idx = static_cast<size_t>(iface);
            const std::string missing = s_plugin_names_per_target_interface[iface_idx].empty()
                ? std::string{"<unset>"} : s_plugin_names_per_target_interface[iface_idx];

            LOG(warn, "Preferred plugin [{}] for interface [{}] was not found; falling back to [{}]",
                missing, GLT::util::enum_to_string(iface), s_discovered[first_idx].name)

            s_plugin_names_per_target_interface[iface_idx] = s_discovered[first_idx].name;
            s_discovered[first_idx].is_enabled = true;
        }

        serialize(s_config_path, serializer::option::save);
        s_config_dirty.store(false, std::memory_order_relaxed);
        GLT::logger::flush_buffer();
        s_shutdown.store(false, std::memory_order_relaxed);
    }


    void load_plugins(const phase current_phase) {

        if (s_shutdown.load(std::memory_order_relaxed))
            return;

        LOG(trace, "loading plugins for load phase [{}]", GLT::util::enum_to_string(current_phase))

        std::vector<plugin_info> pending;
        for (const auto& plugin : s_discovered) {
            if (!plugin.is_enabled)
                continue;
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

                if (!dependencies_satisfied(*it)) {
                    it++;
                    continue;
                }

                const auto load_error = load_single(*it);
                switch (load_error) {
                    case plugin_load_error::none:                                   [[fallthrough]];
                    case plugin_load_error::already_loaded:
                        it = pending.erase(it);
                        progress = (load_error == plugin_load_error::none);
                        break;

                    case plugin_load_error::failed_to_load:                         [[fallthrough]];
                    case plugin_load_error::failed_to_find_factory_functions:       [[fallthrough]];
                    case plugin_load_error::failed_to_create_instance:
                        LOG(error, "Failed to load plugin [{}]: [{}]", it->name, static_cast<int>(load_error));
                        it = pending.erase(it);
                        break;

                    default: ++it; break;
                }
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

            for (auto& p : s_discovered)
                if (p.name == handle.name) {
                    p.is_loaded = false;
                    break;
                }

            s_loaded_plugins.erase(s_loaded_plugins.begin() + (i - 1));
            s_generation.fetch_add(1, std::memory_order_acq_rel);
        }

        GLT::logger::flush_buffer();
    }


    bool load_plugin(std::string_view name) {

        if (s_shutdown.load(std::memory_order_relaxed)) {
            LOG(warn, "Cannot load plugin [{}]: plugin manager is shut down", name);
            return false;
        }

        const plugin_info* info = nullptr;
        for (const auto& p : s_discovered)
            if (p.name == name) { info = &p; break; }

        if (!info) {
            LOG(warn, "Cannot load plugin [{}]: not discovered", name);
            return false;
        }
        if (!info->is_enabled) {
            LOG(warn, "Cannot load plugin [{}]: disabled in configuration "
                      "(call set_plugin_enabled() first)", name);
            return false;
        }
        if (s_loaded_by_name.contains(info->name)) {
            LOG(warn, "Cannot load plugin [{}]: already loaded", name);
            return false;
        }
        if (!dependencies_satisfied(*info)) {
            LOG(warn, "Cannot load plugin [{}]: dependencies not satisfied", name);
            return false;
        }

        const auto err = load_single(*info);
        if (err != plugin_load_error::none) {
            LOG(error, "On-demand load failed for plugin [{}]: [{}]", name, static_cast<int>(err));
            return false;
        }

        LOG(info, "Plugin [{}] loaded on demand", name);
        GLT::logger::flush_buffer();
        return true;
    }


    bool load_plugin(interface targeted) {

        if (targeted == interface::custom) {
            LOG(warn, "load_plugin(interface::custom) is ambiguous — use the name overload");
            return false;
        }
        if (targeted == interface::none)
            return false;

        const auto& name = s_plugin_names_per_target_interface[static_cast<size_t>(targeted)];
        if (name.empty() || name == "unknown" || name == EXPLICITLY_NONE) {
            LOG(warn, "Cannot load plugin for interface [{}]: no plugin configured",
                GLT::util::enum_to_string(targeted));
            return false;
        }
        return load_plugin(name);
    }


    bool unload_plugin(std::string_view name) {

        if (s_shutdown.load(std::memory_order_relaxed)) {
            LOG(warn, "Cannot unload plugin [{}]: plugin manager is shut down", name);
            return false;
        }

        auto it = std::find_if(s_loaded_plugins.begin(), s_loaded_plugins.end(),
            [&](const plugin_handle& h) { return h.name == name; });

        if (it == s_loaded_plugins.end()) {
            LOG(warn, "Cannot unload plugin [{}]: not loaded", name);
            return false;
        }
        if (has_loaded_dependents(it->name, it->name, it->target)) {
            LOG(warn, "Cannot unload plugin [{}]: other loaded plugins depend on it", name);
            return false;
        }

        // Notify while caches are still valid
        if (it->instance) it->instance->on_unload();

        // Invalidate the fast-path slot before dropping the owning ref
        if (it->target != interface::custom)
            detail::g_interface_raw_cache[static_cast<size_t>(it->target)].store(nullptr, std::memory_order_release);

        s_loaded_by_name.erase(it->name);

        if (it->instance)
            it->instance.reset();
        else if (it->module_handle)
            free_library(it->module_handle);

        for (auto& p : s_discovered)
            if (p.name == it->name) { p.is_loaded = false; break; }

        s_loaded_plugins.erase(it);
        s_generation.fetch_add(1, std::memory_order_acq_rel);

        LOG(info, "Plugin [{}] unloaded on demand", name);
        GLT::logger::flush_buffer();
        return true;
    }


    bool unload_plugin(interface targeted) {

        if (targeted == interface::custom) {
            LOG(warn, "unload_plugin(interface::custom) is ambiguous — use the name overload");
            return false;
        }
        if (targeted == interface::none)
            return false;

        const auto& name = s_plugin_names_per_target_interface[static_cast<size_t>(targeted)];
        if (name.empty() || name == "unknown" || name == EXPLICITLY_NONE) {
            LOG(warn, "Cannot unload plugin for interface [{}]: no plugin configured",
                GLT::util::enum_to_string(targeted));
            return false;
        }
        return unload_plugin(name);
    }


    bool reload_plugin(std::string_view name) {

        if (!unload_plugin(name))
            return false;
        return load_plugin(name);
    }


    bool reload_plugin(interface targeted) {

        if (!unload_plugin(targeted))
            return false;
        return load_plugin(targeted);
    }


    void shutdown() {

        serialize(s_config_path, serializer::option::save);

        if (s_shutdown.exchange(true, std::memory_order_acq_rel))
            return;

        for (auto it = s_loaded_plugins.rbegin(); it != s_loaded_plugins.rend(); it++) {

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
        for (auto& p : s_discovered)
            p.is_loaded = false;
        s_discovered.clear();
        s_generation.fetch_add(1, std::memory_order_acq_rel);
    }

    
    [[nodiscard]] const std::vector<plugin_info>& get_all_plugins() { return s_discovered; }


    [[nodiscard]] const plugin_info get_plugin_info(std::string_view name) {

        if (name.empty())
            return {};

        for (const auto& info : s_discovered)
            if (info.name == name)
                return info;

        return {};
    }


    [[nodiscard]] u64 get_generation() noexcept { return s_generation.load(std::memory_order_acquire); }


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


    std::vector<dependency_status> get_dependency_status(const plugin_info& info) {

        std::vector<dependency_status> out;

        for (const auto& dep_name : info.dependencies_names) {

            dependency_status s;
            s.requirement = dep_name;

            auto it = s_loaded_by_name.find(dep_name);
            if (it != s_loaded_by_name.end() && !it->second.expired())
                s.currently_loaded = true;

            for (const auto& p : s_discovered)
                if (p.name == dep_name) {
                    s.provider_name = p.name;
                    s.provider_display = p.display_name;
                    s.satisfied = s.currently_loaded || p.is_enabled;
                    break;
                }

            if (s.provider_name.empty())
                s.provider_display = "(missing)";

            out.push_back(std::move(s));
        }

        for (auto iface_dep : info.dependencies_interfaces) {
            if (iface_dep == interface::none)
                continue;

            dependency_status s;
            s.via_interface = true;
            s.provider_iface = iface_dep;
            s.requirement = std::string("interface:") + GLT::util::enum_to_string(iface_dep);

            if (iface_dep == interface::custom) {
                s.satisfied = false;                        // custom has no 1:1 slot
            } else {
                const auto idx = static_cast<size_t>(iface_dep);
                s.currently_loaded = (detail::g_interface_raw_cache[idx].load(std::memory_order_acquire) != nullptr);
                s.satisfied = s.currently_loaded;
                if (!s.satisfied) {
                    // Fall back to "will be satisfied if an enabled plugin claims this interface"
                    for (const auto& p : s_discovered)
                        if (p.target == iface_dep && p.is_enabled) {
                            s.satisfied = true;
                            s.provider_name = p.name;
                            s.provider_display = p.display_name;
                            break;
                        }
                }
                if (s.satisfied && s.provider_name.empty()) {
                    // loaded but name unknown → look up via slot preference
                    const auto& n = s_plugin_names_per_target_interface[idx];
                    if (!n.empty() && n != "unknown" && n != EXPLICITLY_NONE) {
                        s.provider_name = n;
                        for (const auto& p : s_discovered)
                            if (p.name == n) {
                                s.provider_display = p.display_name;
                                break;
                            }
                    }
                }
            }
            if (s.provider_display.empty())
                s.provider_display = s.provider_name;
            out.push_back(std::move(s));
        }

        return out;
    }


    bool set_plugin_enabled(std::string_view name, bool enabled) {

        plugin_info* info = nullptr;
        for (auto& p : s_discovered)
            if (p.name == name) {
                info = &p;
                break;
            }
        if (!info || info->is_enabled == enabled)
            return false;

        if (info->target == interface::custom) {
            if (enabled)
                s_disabled_custom_plugins.erase(std::string(name));
            else
                s_disabled_custom_plugins.insert(std::string(name));
            info->is_enabled = enabled;

        } else {

            const auto idx = static_cast<size_t>(info->target);
            if (enabled) {
                for (auto& p : s_discovered)
                    if (p.target == info->target && p.name != info->name)
                        p.is_enabled = false;
                s_plugin_names_per_target_interface[idx] = info->name;
                info->is_enabled = true;

            } else {

                s_plugin_names_per_target_interface[idx] = EXPLICITLY_NONE;
                info->is_enabled = false;
            }
        }

        s_config_dirty.store(true, std::memory_order_release);
        return true;
    }


    bool is_config_dirty() { return s_config_dirty.load(std::memory_order_acquire); }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
