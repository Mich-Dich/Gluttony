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


    FORCE_INLINE_R constexpr std::string_view type_to_extension(GLT::asset::type type) noexcept {

        return std::string("glt_") + std::string(type_to_string(type));
    }


    FORCE_INLINE_R constexpr GLT::asset::type extension_to_type(std::string_view extension) noexcept {

        // Accept both "glt_material" and ".glt_material"
        if (!extension.empty() && extension.front() == '.')
            extension.remove_prefix(1);

        // Anything without the "glt_" prefix isn't one of ours
        // this covers source files (.cpp, .fbx, …) that live in the content dir but were never imported
        if (extension.size() <= 4 || extension.substr(0, 4) != "glt_")
            return core_types::invalid;

        const auto resolved = string_to_type(extension.substr(4));
        return resolved.value_or(core_types::invalid);
    }


    // FORCE_INLINE_R GLT::asset::type extension_to_type(std::string_view extension) noexcept {

    //     if (auto registry = GLT::asset::registry::get_ref()) {
    //         extension = (extension.size() && extension.front() == '.') ? extension.substr(1) : extension;
    //         if (extension.size() > 4 && extension.substr(0, 4) == "glt_") {
    //             if (auto t = registry->type_from_name(extension.substr(4)))
    //                 return *t;
    //         }
    //     }
    //     return GLT::asset::extension_to_type(extension);   // core fallback
    // }

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
