
// FORWARD DECLARATIONS ================================================================================================

namespace GLT::math {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // @brief Performs linear interpolation between two values
    // @tparam T Type of values to interpolate between (floating point or integer)
    // @param a Starting value
    // @param b Ending value
    // @param time Interpolation factor (0.0 returns a, 1.0 returns b)
    // @return Interpolated value between a and b based on time
    template<typename T>
    static FORCE_INLINE T lerp(const T a, const T b, const float time) {
        if constexpr (std::is_floating_point_v<T>)
            return static_cast<T>(a * (1.0f - time) + b * time);        // More accurate floating-point formula
        else
            return static_cast<T>(a + (b - a) * time);                  // Original formula for integers
    }


    // @brief Combines hash values with improved avalanche properties
    // @tparam T The type of the value to hash.
    // @tparam Rest Additional types to hash.
    // @param seed The seed value for the hash.
    // @param v The value to hash.
    // @param rest Additional values to hash.
    template <typename T, typename... Rest>
    constexpr void hash_combine(std::size_t& seed, const T& v, const Rest&... rest);


    // Base case for single argument
    template <typename T>
    constexpr void hash_combine(std::size_t& seed, const T& v);

}
