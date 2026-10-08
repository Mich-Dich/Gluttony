#pragma once


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::render {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    template <auto Namespace>
    consteval auto make_render_mode_entries() {

        std::vector<render_mode_info> out;
        for (auto m : std::meta::members_of(Namespace, std::meta::access_context::unchecked())) {

            if (!std::meta::is_variable(m))
                continue;

            if (std::meta::remove_cv(std::meta::type_of(m)) != ^^render_mode)
                continue;

            const auto value = std::meta::extract<render_mode>(m);

            render_mode_info info{};
            info.id = value.id;
            info.is_default = value.is_default;
            info.key = std::define_static_string(std::meta::identifier_of(m));

            for (auto a : std::meta::annotations_of(m)) {

                const auto at = std::meta::type_of(a);
                if (at == ^^reflect::annotations::display_name) {

                    const auto v = std::meta::extract<reflect::annotations::display_name>(a);
                    info.display_name = std::define_static_string(std::string_view(v.value));

                } else if (at == ^^reflect::annotations::tooltip) {

                    const auto v = std::meta::extract<reflect::annotations::tooltip>(a);
                    info.tooltip = std::define_static_string(std::string_view(v.value));

                } else if (at == ^^reflect::annotations::category) {

                    const auto v = std::meta::extract<reflect::annotations::category>(a);
                    info.category = std::define_static_string(std::string_view(v.value));
                }
            }

            if (!info.display_name)
                info.display_name = info.key;

            out.push_back(info);
        }

        return std::define_static_array(out);
    }

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
