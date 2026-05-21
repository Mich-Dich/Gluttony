
#include "util/pch.h"
#include "thread_pool.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::util {

	// CONSTANTS =======================================================================================================

	// MACROS ==========================================================================================================

	// TYPES ===========================================================================================================

	// STATIC VARIABLES ================================================================================================

	// FUNCTION IMPLEMENTATION =========================================================================================

	// CLASS IMPLEMENTATION ============================================================================================

	thread::thread() {

		m_worker = std::thread(&thread::queue_loop, this);
	}

	thread::~thread() {

		if (m_worker.joinable()) {

			wait();
			m_queue_mutex.lock();
			m_destroying = true;
			m_condition.notify_one();
			m_queue_mutex.unlock();
			m_worker.join();
		}
	}

	// CLASS PUBLIC ====================================================================================================

	// CLASS PROTECTED =================================================================================================

	// Add a new job to the thread's queue
	void thread::add_job(std::function<void()> function) {

		std::lock_guard<std::mutex> lock(m_queue_mutex);
		m_job_queue.push(std::move(function));
		m_condition.notify_one();
	}

	// Wait until all work items have been finished
	void thread::wait() {

		std::unique_lock<std::mutex> lock(m_queue_mutex);
		m_condition.wait(lock, [this]() { return m_job_queue.empty(); });
	}

	// CLASS PRIVATE ===================================================================================================

	void thread::queue_loop() {

		while (true) {

			std::function<void()> job;
			{
				std::unique_lock<std::mutex> lock(m_queue_mutex);
				m_condition.wait(lock, [this] { return !m_job_queue.empty() || m_destroying; });
				if (m_destroying)
					break;
				job = m_job_queue.front();
			}

			job();

			{
				std::lock_guard<std::mutex> lock(m_queue_mutex);
				m_job_queue.pop();
				m_condition.notify_one();
			}
		}
	}

	// CLASS IMPLEMENTATION ============================================================================================

	// CLASS PUBLIC ====================================================================================================

	// CLASS PROTECTED =================================================================================================

	// CLASS PRIVATE ===================================================================================================

	// Sets the number of threads to be allocated in this pool
	void thread_pool::set_thread_count(u32 count) {

		threads.clear();
		for (u32 i = 0; i < count; i++)
			threads.push_back(create_unique_ref<thread>());
	}

	// Wait until all threads have finished their work items
	void thread_pool::wait() {

		for (auto &thread : threads)
			thread->wait();
	}

}
