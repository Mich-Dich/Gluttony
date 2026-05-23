
#pragma once

#include <new>  // For placement new

// Core Language Features
#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <concepts>

// Strings and Text Manipulation
#include <string>
#include <string_view>
#include <cstring>
#include <sstream>
#include <regex>
#include <iomanip>
#include <format>
// #include <print>

// Input/Output and Filesystem
#include <iostream>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <stdio.h>

// Data Structures and Containers
#include <array>
#include <deque>
#include <forward_list>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Multithreading and Concurrency
#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
#include <shared_mutex>

// Time and Randomness
#include <chrono>
#include <random>

// System and Low-Level Utilities
#include <system_error>
#include <csignal>
#include <signal.h>
#include <xmmintrin.h>

// Variadic and Debugging Utilities
#include <cstdarg>
#include <cassert>
#include <cstdlib>

#if defined(PLATFORM_LINUX)
    #include <sys/wait.h>
#elif defined(PLATFORM_WINDOWS)
    
#else
    #error unsupported platform
#endif

// glm math
#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/random.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/ext/matrix_transform.hpp>			// glm::translate, glm::rotate, glm::scale
#include <glm/ext/matrix_clip_space.hpp>		// glm::perspective
#include <glm/ext/scalar_constants.hpp>			// glm::pi
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

// ------------------------- independent files (single includes)-------------------------
#include "util/core_config.h"
#include "util/data_structures/data_types.h"
#include "util/macros.h"
#include "util/io/logger.h"
#include "util/io/serializer_data.h"
#include "util/io/vfs.h"
#include "util/system.h"
#include "util/crash_handler.h"
#include "util/math/math.h"
#include "util/timing/interval_controller.h"
#include "util/timing/stopwatch.h"
#include "util/util.h"

#include "config/config.h"

// #include "event/event.h"
// #include "event/event_bus.h"

// #include "platform/window.h"
// #include "layer/layer.h"
// #include "layer/layer_stack.h"
// #include "event/event.h"
// #include "event/input_event.h"
// #include "event/application_event.h"
// #include "data_structures/UUID.h"
// #include "data_structures/path_manipulation.h"
