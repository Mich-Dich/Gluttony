
#pragma once

#include "plugin_system/i_plugin.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::plugin_manager {

    // CONSTANTS =======================================================================================================

    // Number of slots in the interface→plugin cache. `interface::custom` is the last slot; it is
    // intentionally left unpopulated because multiple plugins can share `interface::custom`.
    inline constexpr size_t                     INTERFACE_CACHE_SIZE = static_cast<size_t>(interface::custom) + 1;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct plugin_info {

        std::filesystem::path                   library_path{};
        std::filesystem::path                   logo_path{};            // empty → no logo
        std::string                             name{};
        std::string                             display_name{};
        std::string                             description{};
        std::string                             category{};
        std::string                             author{};
        GLT::version                            plugin_version{};
        GLT::version                            api_version{};
        interface                               target = interface::none;
        phase                                   load_phase = phase::earliest_possible;
        phase                                   unload_phase = phase::earliest_possible;
        std::vector<std::string>                dependencies_names{};
        std::vector<interface>                  dependencies_interfaces{};
        bool                                    is_enabled{};
        bool                                    is_loaded{};
    };


    struct dependency_status {

        std::string                             requirement;            // "jolt_physics" or "interface:world"
        bool                                    via_interface = false;
        bool                                    satisfied = false;      // will be met on next load
        bool                                    currently_loaded = false;   // met right now
        std::string                             provider_name;
        std::string                             provider_display;
        interface                               provider_iface = interface::none;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // Scans the plugin directory (usually [./plugins/] relative to the executable) for shared libraries (*.so, *.dll)
    // Temporarily loads each library to read its [plugin_descriptor], filters plugins according to the configuration file,
    // and populates an internal discovery list. Does NOT load the plugins yet. Must be called once before any load_plugins() invocations
    void discover_plugins(const std::filesystem::path& project_dir_path);


    // @brief Convenience wrapper that fires a load and an unload pass for one phase
    //
    // Equivalent to calling [load_plugins(current_phase)] immediately followed by [unload_plugins(current_phase)]
    // Intended for phases that are pure "run some code and tear down" moments (e.g. [final_cleanup]),
    // where a plugin's whole lifetime fits inside one phase
    //
    // @param current_phase  The phase to load and immediately unload
    FORCE_INLINE void enter_phase(const phase current_phase);


    // Loads all discovered plugins that match the given [current_phase], respecting inter‑plugin dependencies 
    // (both by name and by interface). Plugins whose dependencies are not yet satisfied will be deferred until later in the same
    // load phase. Calls on_load() on each successfully loaded plugin. This function may be called multiple times with different phases
    void load_plugins(phase current_phase);


    // @brief Unloads every plugin whose [unload_phase] matches
    //
    // Iterates the loaded list in reverse order so dependents are torn down before their dependencies
    // Calls [on_unload()] on each matching plugin, then releases its shared-library handle
    //
    // @param current_phase  The phase to unload
    void unload_plugins(const phase current_phase);


    // Gracefully unloads all loaded plugins in reverse dependency order. Calls on_unload() on each plugin, then releases
    // the shared library handles. Also persists the current plugin configuration (which plugin implements which interface)
    // to disk. After shutdown, no further load_plugins() calls have effect
    void shutdown();


    [[nodiscard]] const std::vector<plugin_info>& get_all_plugins();


    [[nodiscard]] const plugin_info get_plugin_info(std::string_view name);


    // CAUTION, this will create an owning ref, only use when you know what you are doing Returns a shared (owning) reference
    // to the loaded plugin instance with the given [name]. If no such plugin is loaded, returns an empty [ref<i_plugin>]
    [[nodiscard]] ref<i_plugin> get_plugin_base(const std::string& name);


    // CAUTION, this will create an owning ref, only use when you know what you are doing Returns a shared (owning) reference
    // to the plugin that implements the given [interface]. The mapping from interface to plugin name is defined in the configuration file
    [[nodiscard]] ref<i_plugin> get_plugin_base(const interface targeted);


    // @brief Monotonically-increasing counter bumped on every plugin load/unload
    //
    // Use it to invalidate any raw plugin pointer you cached:
    //```cpp
    //     if (plugin_manager::get_generation() != m_cached_gen) {
    //         m_audio = plugin_manager::get_plugin_raw<audio::i_audio_plugin>(interface::audio);
    //         m_cached_gen = plugin_manager::get_generation();
    //     }
    //```
    [[nodiscard]] u64 get_generation() noexcept;


    // CAUTION, this will create an owning ref, only use when you know what you are doing
    [[nodiscard]] ref<i_plugin> get_plugin_base(const std::string& name);


    [[nodiscard]] ref<i_plugin> get_plugin_base(const interface targeted);


    // Detailed dependency report for the UI. `satisfied` = "will be met on next restart";
    // `currently_loaded` = "met by a plugin that is loaded right now".
    [[nodiscard]] std::vector<dependency_status> get_dependency_status(const plugin_info& info);


    // Toggle a plugin. Custom plugins are independent; non-custom plugins auto-disable
    // any other enabled plugin for the same interface. Does NOT load/unload — restart required.
    // Returns true if the state actually changed.
    bool set_plugin_enabled(std::string_view name, bool enabled);


    // True if any set_plugin_enabled() call has happened since startup.
    [[nodiscard]] bool is_config_dirty();

    // TEMPLATE DECLARATION ============================================================================================

    template<typename T = i_plugin>
    FORCE_INLINE_R ref<T> get_plugin_ref(const std::string& name);


    template<typename T = i_plugin>
    FORCE_INLINE_R ref<T> get_plugin_ref(const interface targeted);


    // @brief Unchecked cast for interfaces where the mapping is trusted (i.e. the descriptor is right).
    //        Skips RTTI. Use only when you know T matches what the config maps `targeted` to.
    template<typename T>
    FORCE_INLINE_R ref<T> get_plugin_ref_unsafe(interface targeted);


    template<typename T = i_plugin>
    FORCE_INLINE_R weak_ref<T> get_plugin(const std::string& name);


    template<typename T = i_plugin>
    FORCE_INLINE_R weak_ref<T> get_plugin(const interface targeted);


    // @brief Hot-path accessor: raw pointer to the plugin implementing `targeted`, or nullptr.
    //
    // Thread-safe (one acquire load), zero refcount traffic, zero RTTI.
    // Returns nullptr for `interface::custom` — use `get_plugin_base("name")` for custom plugins.
    //
    // For maximum performance, cache the returned pointer at phase transitions and check
    // `get_generation()` for invalidation instead of calling this every frame.
    template<typename T = i_plugin>
    FORCE_INLINE_R T* get_plugin_raw(interface targeted) noexcept;


    // Inline fast-path (single acquire load, no function-call overhead when this header is included in the TU that also
    // links plugin_manager.cpp)
    namespace detail {

        extern std::array<std::atomic<i_plugin*>, INTERFACE_CACHE_SIZE> g_interface_raw_cache;

        FORCE_INLINE_R i_plugin* get_cached_interface_ptr(interface targeted) noexcept;

    }

    // CLASS DECLARATION ===============================================================================================

}

#include "plugin_manager.inl"
