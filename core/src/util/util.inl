#pragma once

#include <cstring>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::util {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    std::string format_bytes(u64 bytes) {

        constexpr const char* units[] = { "B", "KB", "MB", "GB", "TB" };
        double v = static_cast<double>(bytes);
        int u = 0;
        while (v >= 1024.0 && u < 4) { 
            v /= 1024.0; 
            u++;
        }

        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.2f %s", v, units[u]);
        return buf;
    }
    

    // Mix several 64-bit values into one. XOR + FNV-1a-style multiply; deterministic.
    u64 mix_u64(u64 acc, u64 value) {

        acc ^= value;
        acc *= 0x100000001B3ull;
        return acc;
    }


    // FNV-1a-64 over a string_view.
    u64 fnv1a_64(std::string_view s) {

        u64 h = 14695981039346656037ull;
        for (const char c : s) {
            h ^= static_cast<u8>(c);
            h *= 0x100000001B3ull;
        }
        return h;
    }

    
    // TEMPLATE IMPLEMENTATION =========================================================================================

    template <typename E>
    requires std::is_enum_v<E>
    constexpr std::string_view enum_to_string(E value) {

        constexpr std::size_t N = std::meta::enumerators_of(^^E).size();
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            std::string_view result = "<unnamed>";
            ((value == [:std::meta::enumerators_of(^^E)[Is]:]
                ? (result = std::meta::identifier_of(std::meta::enumerators_of(^^E)[Is]), true)
                : false) || ...);
            return result;
        }(std::make_index_sequence<N>{});
    }


    template <typename E>
    requires std::is_enum_v<E>
    constexpr std::optional<E> string_to_enum(std::string_view str) {

        constexpr std::size_t N = std::meta::enumerators_of(^^E).size();
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            std::optional<E> result;
            ((str == std::meta::identifier_of(std::meta::enumerators_of(^^E)[Is])
                ? (result = [:std::meta::enumerators_of(^^E)[Is]:], true)
                : false) || ...);
            return result;
        }(std::make_index_sequence<N>{});
    }


    template<std::unsigned_integral T>
    FORCE_INLINE constexpr void set_bytes(std::vector<std::byte>& array, T value) noexcept {

        static_assert(sizeof(T) <= 8, "Only up to 64-bit unsigned integers are supported");

        const auto buffer = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);

        if constexpr (std::endian::native == std::endian::little) {

            /** Native layout is already little-endian: emit bytes in memory order */
            for (const auto b : buffer)
                array.push_back(b);

        } else {

            /** Serialize as little-endian: emit low byte first */
            for (size_t i = sizeof(T); i > 0; --i)
                array.push_back(buffer[i - 1]);
        }
    }


    template<typename T> requires(std::is_arithmetic_v<T> && sizeof(T) <= 8)
    FORCE_INLINE_R constexpr T get_bytes(const std::vector<std::byte>& array, const size_t startIndex) {

        static_assert(sizeof(T) <= 8, "Only up to 64-bit unsigned integers are supported");

        const auto minSize = sizeof(T);
        if (startIndex + minSize > array.size())
            throw std::out_of_range("Not enough bytes in vector");

        T value = 0;
        if constexpr (std::endian::native == std::endian::little)
            std::memcpy(&value, array.data() + startIndex, sizeof(T));      /** Direct copy for little endian systems */

        else {

            /** Byte reversal for big endian systems */
            for (size_t i = 0; i < sizeof(T); ++i)
                value |= static_cast<T>(array[startIndex + i]) << (i * 8);
        }
        return value;
    }

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
