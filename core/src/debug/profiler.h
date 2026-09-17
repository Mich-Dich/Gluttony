#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::debug {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Arbitrary values a *specific* system contributes to the HUD. The HUD
    // does not know what any of these mean — it renders them as a name/value
    // list under a section header matching the provider's `name`.
    struct system_stats {

        struct value {

            std::string                         name{};
            f32                                 value = 0.0f;
            const char*                         format = "%.2f";
        };
        std::vector<value>                      custom{};
    };


    struct render_stats {

        f32                                     gpu_time_ms = 0.0f;     // time spent on the GPU (from timestamp queries)

        // rendering ---------------------------------------------------------------------------------------------------
        u32                                     draw_calls = 0;
        u32                                     triangles = 0;
        u32                                     vertices = 0;
        u32                                     render_passes = 0;

        // memory (absolute, not per-frame) ----------------------------------------------------------------------------
        u64                                     vram_bytes = 0;
        u64                                     ram_bytes = 0;

        // resources (absolute counts) ---------------------------------------------------------------------------------
        u32                                     texture_count = 0;
        u32                                     buffer_count = 0;
        u32                                     descriptor_set_count = 0;
        u32                                     pipeline_count = 0;
    };


    // Application-wide statistics. One global instance; systems write their
    // fields directly (see app_stats()). Per-frame fields are reset by
    // begin_frame(); absolute fields (vram, counts) persist across frames.
    //
    // Thread-safety: the HUD and every writer run on the main thread during
    // the frame. Do not write these from worker threads without your own
    // synchronisation.
    struct application_stats {

        // timing ------------------------------------------------------------------------------------------------------
        f32                                     frame_time_ms = 0.0f;   // CPU wall-clock for the frame
        f32                                     cpu_time_ms = 0.0f;     // time spent in CPU work
        f32                                     fps = 0.0f;

        render_stats                            render{};
    };


    // A registered system provider. `name` is the section header in the HUD;
    // `fill` populates the system's own `system_stats` (custom values only).
    struct stats_provider {

        std::string                             name{};
        std::function<void(system_stats&)>      fill{};
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // Global instance. Mutate fields directly, e.g. GLT::debug::app_stats().draw_calls = m_frame_draw_calls;
    // No registration needed.
    void update_app_stats(const application_stats& stats);


    [[nodiscard]] application_stats get_application_stats();


    // Register a provider. Returns an opaque id that can be passed to unregister_provider(). 
    // Safe to call from any thread, but registration is expected to happen during plugin on_load().
    [[nodiscard]] handle register_provider(const stats_provider provider);


    // Remove a previously registered provider. Pass the id returned from register_provider().
    void unregister_provider(const handle id);


    // Called by the HUD each frame to collect every provider's custom values.
    // Returns (name, system_stats) pairs in registration order.
    [[nodiscard]] std::vector<std::pair<std::string, system_stats>> collect_systems();

    // scoped CPU timing -----------------------------------------------------------------------------------------------

    // Lightweight RAII timer. Accumulates elapsed time into a named bucket.
    // Buckets are drained by drain_scope_timings(); a typical pattern is to
    // register a "CPU Breakdown" provider that drains them into custom values.
    //
    // Coarse by design: one bucket per name, no call-stack reconstruction.
    // For deep analysis, forward to Tracy via the TRACY_ENABLE path.
    struct scope_timer {

        scope_timer(const char* n);
        ~scope_timer();
        
        const char*                             name;
        u64                                     start_ns;
    };


    // Retrieve and clear all scope timings accumulated since the last call.
    // Returns name -> milliseconds.
    [[nodiscard]] std::vector<std::pair<std::string, f32>>    drain_scope_timings();

    // CLASS DECLARATION ===============================================================================================

}

// Profiling macros ----------------------------------------------------------------------------------------------------

#if defined(DEBUG) || defined(PROFILING_ENABLED)

    #define PROFILE_SCOPE(name)     GLT::debug::scope_timer _glt_profile_scope_##__LINE__(name)
    #define PROFILE_FUNCTION()      PROFILE_SCOPE(__FUNCTION__)

#else

    #define PROFILE_SCOPE(name)     ((void)0)
    #define PROFILE_FUNCTION()      ((void)0)

#endif
