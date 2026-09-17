
#include "util/pch.h"
#include "thread_pool.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::thread_pool {

	// CONSTANTS =======================================================================================================

	// MACROS ==========================================================================================================

	// TYPES ===========================================================================================================

	// STATIC VARIABLES ================================================================================================

    // File-local state. Nothing outside this translation unit can see it.
    static std::vector<std::thread>         s_workers{};

	static std::deque<task>                 s_worker_queue{};

	static std::deque<task>                 s_main_queue{};

    static std::mutex                       s_worker_mutex{};

	static std::mutex                       s_main_mutex{};

	static std::condition_variable          s_work_available{};

	static std::condition_variable          s_all_idle{};

    static std::atomic<bool>                s_initialized{ false };

	static std::atomic<bool>                s_stopping{ false };

	static u32                              s_active_tasks = 0;   // guarded by s_worker_mutex

	// INTERNAL TEMPLATE DECLARATION ===================================================================================

	// INTERNAL FUNCTION DECLARATION ===================================================================================

	static void worker_loop();

	// INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

	// INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static void worker_loop() {

        for (;;) {

            task current;
            {
                std::unique_lock lock(s_worker_mutex);
                s_work_available.wait(lock, [] {
                    return s_stopping.load() || !s_worker_queue.empty();
                });

                // Drain remaining tasks before exiting so a shutdown() mid-
                // frame doesn't silently drop work.
                if (s_stopping.load() && s_worker_queue.empty())
                    return;

                current = std::move(s_worker_queue.front());
                s_worker_queue.pop_front();
                ++s_active_tasks;
            }

            // Run the task outside the lock. Exceptions are the caller's
            // problem — a task that throws takes the whole worker down unless
            // the task wraps its own body in try/catch. That is deliberate:
            // silently swallowing exceptions in a worker hides bugs.
            current();

            {
                std::unique_lock lock(s_worker_mutex);
                --s_active_tasks;
                if (s_worker_queue.empty() && s_active_tasks == 0)
                    s_all_idle.notify_all();
            }
        }
    }

	// TEMPLATE IMPLEMENTATION =========================================================================================

	// FUNCTION IMPLEMENTATION =========================================================================================

    void init(u32 thread_count) {

        bool expected = false;
        if (!s_initialized.compare_exchange_strong(expected, true))
            return;                                 // already running

        if (thread_count == 0) {
            thread_count = std::thread::hardware_concurrency();
            if (thread_count == 0)
                thread_count = 1;                   // hardware_concurrency() may return 0
        }

        s_stopping.store(false);

        s_workers.reserve(thread_count);
        for (u32 i = 0; i < thread_count; ++i)
            s_workers.emplace_back(worker_loop);
    }


    void shutdown() {

        if (!s_initialized.load())
            return;

        {
            std::unique_lock lock(s_worker_mutex);
            s_stopping.store(true);
        }
        s_work_available.notify_all();

        for (auto& t : s_workers)
            if (t.joinable())
                t.join();

        s_workers.clear();

        {
            std::unique_lock lock(s_worker_mutex);
            s_worker_queue.clear();
            s_active_tasks = 0;
        }
        {
            std::unique_lock lock(s_main_mutex);
            s_main_queue.clear();
        }

        s_initialized.store(false);
        s_stopping.store(false);
    }


    void push(task fn) {

        if (!fn) return;

        // If the pool isn't up (early startup, after shutdown, unit tests),
        // run inline so the caller still gets its work done.
        if (!s_initialized.load()) {
            fn();
            return;
        }

        {
            std::unique_lock lock(s_worker_mutex);
            s_worker_queue.push_back(std::move(fn));
        }
        s_work_available.notify_one();
    }


    void push_main(task fn) {

        if (!fn) return;

        std::unique_lock lock(s_main_mutex);
        s_main_queue.push_back(std::move(fn));
    }


    void pump_main_thread() {

        // Swap the queue into a local so we don't hold the lock while running callbacks 
		// a callback may call push_main() again, and a worker may be pushing concurrently.
        std::deque<task> local;
        {
            std::unique_lock lock(s_main_mutex);
            local.swap(s_main_queue);
        }

        for (auto& fn : local)
            if (fn) fn();
    }


    void wait_for_all() {

        if (!s_initialized.load())
            return;

        std::unique_lock lock(s_worker_mutex);
        s_all_idle.wait(lock, [] {
            return s_worker_queue.empty() && s_active_tasks == 0;
        });
    }


    bool is_initialized() { return s_initialized.load(); }


    u32 get_thread_count() { return static_cast<u32>(s_workers.size()); }


    u32 get_pending_count() {

        std::unique_lock lock(s_worker_mutex);
        return static_cast<u32>(s_worker_queue.size());
    }


    u32 get_active_count() {

        std::unique_lock lock(s_worker_mutex);
        return s_active_tasks;
    }

	// CLASS IMPLEMENTATION ============================================================================================

	// CLASS PUBLIC ====================================================================================================

	// CLASS PROTECTED =================================================================================================

	// CLASS PRIVATE ===================================================================================================

}
