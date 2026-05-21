
#pragma once

#include "util/system.h"
#include "util/timing/stopwatch.h"
#include "util/timing/interval_controller.h"
#include "util/io/vfs.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}


namespace std {

    // For standard containers with begin()/end() methods
    template <typename C, typename T>
    FORCE_INLINE bool contains(const C& container, const T& item) {

        return std::find(container.begin(), container.end(), item) != container.end();
    }

    // Overload for C-style arrays
    template <typename T, size_t N>
    bool contains(const T (&array)[N], const T& item) {
        return std::find(array, array + N, item) != array + N;
    }

    template<>
    struct formatter<filesystem::path> : formatter<string_view> {
        auto format(const filesystem::path& p, format_context& ctx) const {
            return formatter<string_view>::format(p.string(), ctx);
        }
    };

}
