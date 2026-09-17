#pragma once

// FORWARD DECLARATIONS ================================================================================================

namespace GLT::thread_pool {

	// CONSTANTS =======================================================================================================

	// MACROS ==========================================================================================================

	// TYPES ===========================================================================================================

    using task = std::function<void()>;

	// STATIC VARIABLES ================================================================================================

	// FUNCTION DECLARATION ============================================================================================

    // Spawns `thread_count` worker threads (0 = std::thread::hardware_concurrency()).
    // Safe to call multiple times; subsequent calls are no-ops while the pool is running. 
	// Call once, early, before any other system tries to push work.
    void init(u32 thread_count = 0);


    // Joins all worker threads. Waits for in-flight tasks to finish, then drops anything still queued. 
	// Pending main-thread callbacks are also discarded. Safe to call multiple times.
    void shutdown();


    // Queues a task for a background worker. Fire-and-forget; if the pool is not initialised, the task runs inline so callers never lose work.
    void push(task fn);


    // Queues a task to run on whichever thread calls pump_main_thread() next.
    // Thread-safe; the intended use is for a worker to marshal results back to the frame that owns them (ImGui state, GPU resources, etc.).
    void push_main(task fn);


    // Drains the main-thread queue on the calling thread. Call once per frame from the application's main loop.
    void pump_main_thread();


    // Blocks until the worker queue is empty and all workers are idle.
    // Does NOT drain the main-thread queue (that must happen on the main thread).
    void wait_for_all();

    // --- introspection -----------------------------------------------------------------------------------------------

    [[nodiscard]] bool is_initialized();
    [[nodiscard]] u32 get_thread_count();
    [[nodiscard]] u32 get_pending_count();
    [[nodiscard]] u32 get_active_count();

	// TEMPLATE DECLARATION ============================================================================================

    // Same as push(), but returns a future so you can observe the result.
    // Use `submit(...).get()` to block, or check `.wait_for(0s)` for a poll.
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>;

	// CLASS DECLARATION ===============================================================================================

}
