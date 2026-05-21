#pragma once

#include "plugin_interface.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::plugin_manager {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // Scans the plugin directory (usually `./plugins/` relative to the executable)
    // for shared libraries (*.so, *.dll). Temporarily loads each library to read its
    // `plugin_descriptor`, filters plugins according to the configuration file,
    // and populates an internal discovery list. Does NOT load the plugins yet.
    // Must be called once before any load_plugins() invocations.
    void discover_plugins();

    
    // Loads all discovered plugins that match the given `currentPhase`, respecting
    // inter‑plugin dependencies (both by name and by interface). Plugins whose
    // dependencies are not yet satisfied will be deferred until later in the same
    // load phase. Calls on_load() on each successfully loaded plugin.
    // This function may be called multiple times with different phases.
    void load_plugins(load_phase currentPhase);

    
    // Gracefully unloads all loaded plugins in reverse dependency order.
    // Calls on_unload() on each plugin, then releases the shared library handles.
    // Also persists the current plugin configuration (which plugin implements which interface)
    // to disk. After shutdown, no further load_plugins() calls have effect.
    void shutdown();


    // CAUTION, this will create an owning ref, only use when you know what you are doing
    // Returns a shared (owning) reference to the loaded plugin instance with the given `name`.
    // If no such plugin is loaded, returns an empty `ref<i_plugin>`.
    [[nodiscard]] ref<i_plugin> get_plugin_base(const std::string& name);


    // CAUTION, this will create an owning ref, only use when you know what you are doing
    // Returns a shared (owning) reference to the plugin that implements the given `targeted_interface`.
    // The mapping from interface to plugin name is defined in the configuration file.
    [[nodiscard]] ref<i_plugin> get_plugin_base(const targeted_interface targeted);


    // CAUTION, this will create an owning ref, only use when you know what you are doing
    // Template version of get_plugin_base that automatically casts the returned pointer
    // to the requested plugin type `T` (which must derive from i_plugin).
    // Example: auto win = get_plugin_ref<platform::i_window_plugin>(targeted_interface::window);
    template<typename T = i_plugin>
    FORCE_INLINE_R ref<T> get_plugin_ref(const std::string& name);

    
    // CAUTION, this will create an owning ref, only use when you know what you are doing
    // Template version of get_plugin_base that takes a targeted_interface instead of a name.
    template<typename T = i_plugin>
    FORCE_INLINE_R ref<T> get_plugin_ref(const targeted_interface targeted);

    
    // Returns a weak (non‑owning) reference to the plugin with the given `name`,
    // automatically downcast to type `T`. Use this when you need to observe a plugin
    // without extending its lifetime. The returned weak_ref may expire if the plugin
    // is unloaded.
    template<typename T = i_plugin>
    FORCE_INLINE_R weak_ref<T> get_plugin(const std::string& name);


    // Returns a weak (non‑owning) reference to the plugin that implements the given
    // `targeted_interface`, downcast to type `T`. The lifetime is not extended.
    template<typename T = i_plugin>
    FORCE_INLINE_R weak_ref<T> get_plugin(const targeted_interface targeted);

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

#include "plugin_manager.inl"
