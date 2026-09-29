#pragma once


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    FORCE_INLINE_R constexpr std::string_view type_to_string(const type& t) noexcept {
        return type_to_string<^^core_types>(t);
    }


    FORCE_INLINE_R constexpr std::optional<type> string_to_type(std::string_view str) noexcept {
        return string_to_type<^^core_types>(str);
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    template <auto Namespace>
    FORCE_INLINE_R consteval auto make_type_entries() {

        std::vector<type_entry> result;

        for (auto m : std::meta::members_of(Namespace, std::meta::access_context::unchecked())) {

            if (std::meta::is_variable(m)
                && std::meta::remove_cv(std::meta::type_of(m)) == ^^type) {

                result.push_back({
                    std::define_static_string(std::meta::identifier_of(m)),
                    std::meta::extract<type>(m)
                });
            }
        }

        return std::define_static_array(result);
    }


    template <auto Namespace>
    FORCE_INLINE_R constexpr std::string_view type_to_string(const type& t) {
        for (const auto& e : type_entries<Namespace>)
            if (e.value == t) return e.name;      // const char* -> string_view

        return "<unnamed>";
    }


    template <auto Namespace>
    FORCE_INLINE_R constexpr std::optional<type> string_to_type(std::string_view str) {
        for (const auto& e : type_entries<Namespace>)
            if (str == e.name) return e.value;    // string_view == const char*  is fine

        return std::nullopt;
    }

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
