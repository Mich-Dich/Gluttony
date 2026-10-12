
// need to catch signals related to termination to shutdown gracefully (eg: flush remaining log messages). Was inspired by reckless_log: https://github.com/mattiasflodin/reckless

/* Copyright 2015 - 2020 Mattias Flodin <git@codepentry.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files(the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions :
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#include "util/pch.h"
#include "util/io/logger.h"
// #include "util/util.h"

#if defined(PLATFORM_WINDOWS)
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
    #include <windows.h>
#endif

#include "crash_handler.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::crash_handler {

	// CONSTANTS =======================================================================================================

	// MACROS ==========================================================================================================

	// TYPES ===========================================================================================================

	// STATIC VARIABLES ================================================================================================

	// FUNCTION IMPLEMENTATION =========================================================================================

	static std::vector<std::function<void()>> s_user_functions;


	u64 subscribe(std::function<void()> function) {
		
		s_user_functions.push_back(function);
		return s_user_functions.size() - 1;
	}


	void unsubscribe(u64 index) {

		if (index < s_user_functions.size()) {
			s_user_functions[index] = nullptr;
		}
	}


	void execute_user_functions() {

		LOG(trace, "Executing [{}] registered functions", s_user_functions.size());

		// Execute in reverse order of registration
		for (auto it = s_user_functions.rbegin(); it != s_user_functions.rend(); it++) {
			if (!*it)
				continue;

			try {
				(*it)();
			} catch (const std::exception& e) {
				LOG(error, "Exception in crash handler function: {}", e.what());
			} catch (...) {
				LOG(error, "Unknown exception in crash handler function");
			}
		}
		LOG(trace, "User functions have been executed");
	}


	#if defined(PLATFORM_LINUX)

		const std::initializer_list<int> signals = {
			SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGABRT, SIGFPE, SIGKILL, SIGSEGV, SIGPIPE, SIGALRM, SIGTERM, SIGUSR1, SIGUSR2,    // POSIX.1-1990 signals
			SIGBUS, SIGPOLL, SIGPROF, SIGSYS, SIGTRAP, SIGVTALRM, SIGXCPU, SIGXFSZ,                                             // SUSv2 + POSIX.1-2001 signals
			SIGIOT, SIGSTKFLT, SIGIO, SIGPWR,                                                                                   // Various other signals
		};
		std::vector<std::pair<int, struct sigaction>> g_old_sig_actions;


		void detach() {

			while(!g_old_sig_actions.empty()) {
				auto const& p = g_old_sig_actions.back();
				auto signal = p.first;
				auto const& oldact = p.second;
				if(0 != sigaction(signal, &oldact, nullptr))
					throw std::system_error(errno, std::system_category());
				g_old_sig_actions.pop_back();
			}
		}


		void signal_handler(const int signal) {

			std::cout << "signal caught => terminating" << std::endl;
			LOG(fatal, "crash_handler caught signal [{}]", signal)
			execute_user_functions();
			std::quick_exit(1);			// Directly terminate without clean shutdown
		}


		void attach() {

			struct sigaction act;
			std::memset(&act, 0, sizeof(act));
			act.sa_handler = &signal_handler;
			sigfillset(&act.sa_mask);
			act.sa_flags = SA_RESETHAND;

			// Some signals are synonyms for each other. Some are explicitly specified
			// as such, but others may just be implemented that way on specific
			// systems. So we'll remove duplicate entries here before we loop through
			// all the signal numbers.
			std::vector<int> unique_signals(signals);
			sort(begin(unique_signals), end(unique_signals));
			unique_signals.erase(unique(begin(unique_signals), end(unique_signals)), end(unique_signals));
			try {
				g_old_sig_actions.reserve(unique_signals.size());
				for(auto signal : unique_signals) {
					struct sigaction oldact;
					if(0 != sigaction(signal, nullptr, &oldact))
						throw std::system_error(errno, std::system_category());
					if(oldact.sa_handler == SIG_DFL) {
						if(0 != sigaction(signal, &act, nullptr)) {
							if(errno == EINVAL)             // If we get EINVAL then we assume that the kernel does not know about this particular signal number.
								continue;

							throw std::system_error(errno, std::system_category());
						}
						g_old_sig_actions.push_back({signal, oldact});
					}
				}
			}

			catch (const std::exception& e) {
				std::cout << "Exception caught [" << e.what() << "]" << std::endl;
				detach();
				throw;
			} catch (...) {
				std::cout << "Unknown exception caught." << std::endl;
				detach();
				throw;
			}
		}

	#elif defined(PLATFORM_WINDOWS)

		LPTOP_LEVEL_EXCEPTION_FILTER old_exception_filter = nullptr;
		static PVOID g_vectoredExceptionHandle = nullptr;


		LONG WINAPI vectored_exception_handler(_EXCEPTION_POINTERS* ExceptionInfo) {
			if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT) {
				execute_user_functions();
				TerminateProcess(GetCurrentProcess(), 1);  // Skip CRT cleanup
				return EXCEPTION_EXECUTE_HANDLER;
			}
			return EXCEPTION_CONTINUE_SEARCH;  // Let other handlers process
		}


		LONG WINAPI exception_filter(_EXCEPTION_POINTERS* ExceptionInfo) {

			execute_user_functions();

			// Save the old filter and detach the crash handler
			LPTOP_LEVEL_EXCEPTION_FILTER old_filter = old_exception_filter;
			detach();

			if (old_filter)
				return old_filter(ExceptionInfo);
			else
				return EXCEPTION_CONTINUE_SEARCH;
		}


		void attach() {

			assert(old_exception_filter == nullptr);
			old_exception_filter = SetUnhandledExceptionFilter(&exception_filter);
			g_vectoredExceptionHandle = AddVectoredExceptionHandler(1, vectored_exception_handler);				// 1 = call first
		}


		void detach() {

			if (old_exception_filter) {
				SetUnhandledExceptionFilter(old_exception_filter);
				old_exception_filter = nullptr;
			}

			if (g_vectoredExceptionHandle) {
				RemoveVectoredExceptionHandler(g_vectoredExceptionHandle);
				g_vectoredExceptionHandle = nullptr;
			}
		}

	#endif

	// CLASS IMPLEMENTATION ============================================================================================

	// CLASS PUBLIC ====================================================================================================

	// CLASS PROTECTED =================================================================================================

	// CLASS PRIVATE ===================================================================================================

}
