
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Generic resource manager template that handles loading, caching, and lifecycle management of game resources.
    // Supports both synchronous and background cleanup with thread-safe operations.
    // @tparam resource_type The type of resource being managed (e.g., material, mesh, texture).
    // @tparam resource_specific_header The type of resource-specific header data.
    // @tparam key_type The type used to identify resources (default: filesystem path).
    template <typename resource_type, typename resource_specific_header, typename key_type = std::filesystem::path>
    class resource_manager {
    public:

        using resource_ref = std::shared_ptr<resource_type>;
        using load_function = std::function<resource_ref(const key_type&, asset_file_header&, resource_specific_header&)>;
        using unload_function = std::function<void(resource_ref)>;

        // Constructs a resource manager with the specified load and unload functions.
        // @param loadFunc Function called to load a resource when not found in cache.
        // @param unloadFunc Optional function called when a resource is unloaded.
        // @param useBackgroundCleanup Whether to enable automatic background cleanup of unused resources.
        resource_manager(load_function loadFunc, unload_function unloadFunc = nullptr, bool useBackgroundCleanup = false);


        // Destroys the resource manager and releases all managed resources.
        ~resource_manager();


        // Initializes the resource manager and starts background cleanup thread if enabled.
        void init();


        // Shuts down the resource manager, stops background thread, and releases all resources.
        void shutdown();


        // Retrieves a resource by key, loading it if not already cached.
        // @param key The unique identifier for the resource.
        // @return Shared pointer to the requested resource, or nullptr if loading failed.
        resource_ref get(const key_type& key);


        // Finds the key associated with a resource reference.
        // @param resource_ref The resource reference to search for.
        // @return The key associated with the resource, or default key if not found.
        key_type get_key_from_ref(resource_ref resource_ref);


        // Checks if a resource with the specified key is currently cached.
        // @param key The key to check for existence in the cache.
        // @return True if the resource is cached, false otherwise.
        bool contains(const key_type& key) const;


        // Releases all cached resources and clears the cache.
        void release_all();

    private:

        // Removes resources from cache that are no longer referenced elsewhere.
        void cleanup_unused();


        // Background thread function that periodically cleans up unused resources.
        void cleanup_thread_func();


        std::unordered_map<key_type, resource_ref>      m_resources;                // Cache of loaded resources
        mutable std::mutex                              m_mutex;                    // Thread synchronization mutex
        load_function                                   m_load;                     // Resource loading function
        unload_function                                 m_unload;                   // Resource unloading function

        // Background cleanup members
        std::thread                                     m_cleanup_thread;           // Background cleanup thread
        std::condition_variable                         m_condition;                // Thread synchronization condition
        std::atomic<bool>                               m_running{false};           // Thread running state
        std::atomic<bool>                               m_stop_requested{false};    // Thread stop request flag
        bool                                            m_useBackgroundCleanup;     // Whether background cleanup is enabled
    };

}

#include "manager_template.inl"
