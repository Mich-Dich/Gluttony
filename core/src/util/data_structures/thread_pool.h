#pragma once

// FORWARD DECLARATIONS ================================================================================================

namespace GLT::util {

	// CONSTANTS =======================================================================================================

	// MACROS ==========================================================================================================

	// TYPES ===========================================================================================================

	// STATIC VARIABLES ================================================================================================

	// FUNCTION DECLARATION ============================================================================================

	// TEMPLATE DECLARATION ============================================================================================

	// CLASS DECLARATION ===============================================================================================

	// @brief A worker thread class that processes jobs from a queue.
	//        Manages its own job queue and executes jobs in the order they are added.
	//        The thread runs continuously until destroyed, waiting for new jobs when the queue is empty.
	class thread {
	public:

		// @brief Constructs a thread and starts its worker loop. The thread immediately begins waiting for jobs to be added to its queue.
		thread();


		// @brief Destroys the thread, ensuring all pending jobs complete. Signals the worker thread to stop and waits for it to join.
		~thread();


		// @brief Adds a new job to the thread's queue for execution. The job is stored as a function with no arguments and no return value.
		//        Notifies the worker thread that a new job is available.
		// @param [function] The function to execute as a job.
		void add_job(std::function<void()> function);


		// @brief Waits until all jobs in the thread's queue have been completed. Blocks the calling thread until the job queue is empty.
		void wait();

	private:

		bool 								m_destroying = false;		// Flag indicating the thread should stop
		std::thread 						m_worker;					// The underlying thread object
		std::queue<std::function<void()>> 	m_job_queue;				// Queue of pending jobs
		std::mutex 							m_queue_mutex;				// Mutex for thread-safe queue access
		std::condition_variable 			m_condition;				// Condition variable for job notification

		// @brief The main loop that runs in the worker thread. Continuously waits for jobs, executes them, and removes them from the queue.
		//        Exits when the destroying flag is set to true.
		void queue_loop();
	};


	// @brief A thread pool that manages multiple worker threads. Provides a way to allocate a specific number of threads and wait for all of them to complete.
	class thread_pool {
	public:

		// @brief Sets the number of worker threads in the pool. Clears any existing threads and creates new ones with the specified count.
		// @param [count] The number of threads to allocate in the pool.
		void set_thread_count(u32 count);

		// @brief Waits until all threads in the pool have finished processing their jobs. Calls wait() on each thread in the pool, blocking until all queues are empty.
		void wait();

		// @brief Collection of unique references to the worker threads in the pool. Each thread is independently managed and processes its own job queue.
		std::vector<unique_ref<thread>> 	threads;
	};

}
