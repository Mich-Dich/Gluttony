#pragma once

#include <meta>

// FORWARD DECLARATIONS ================================================================================================

namespace GLT::util {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

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

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
