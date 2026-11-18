#pragma once

// Procedural construction: common includes. The SDK (include/construct + sdk/) is engine-independent: it needs the standard
// library, glm and the engine's fixed-width types only, so construct packs, the plugin and the standalone tests all
// compile it the same way. Inside the engine the pch has already included all of this; the guards make it harmless.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <compare>
#include <cstdint>
#include <cstring>
#include <expected>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <meta>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#ifndef GLM_ENABLE_EXPERIMENTAL
    #define GLM_ENABLE_EXPERIMENTAL
#endif
#ifndef GLM_FORCE_RADIANS
    #define GLM_FORCE_RADIANS
#endif
#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
    #define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "util/data_structures/data_types.h"
