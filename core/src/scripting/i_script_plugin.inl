#pragma once

#include "scripting/i_script_plugin.h"
#include "reflection/annotations.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::script {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // annotation helpers ----------------------------------------------------------------------------------------------

    template <typename Ann>
    consteval bool has_ann(std::meta::info owner);


    // NOTE: `Owner` must be a non-type template parameter (not a function parameter) because GCC requires
    // `std::meta::annotations_of` to receive a manifest constant expression. Function parameters, even in
    // `consteval` functions, are not manifest constants for reflection purposes.
    template <typename Ann, std::meta::info Owner>
    consteval Ann get_ann();

    // kind / label ----------------------------------------------------------------------------------------------------

    template <typename T>
    consteval script_kind kind_of_script();


    template <typename T>
    consteval const char* editor_label_of();

    // fields ----------------------------------------------------------------------------------------------------------

    template <typename T>
    consteval std::span<const script_field_info> build_fields();

    // callable methods ------------------------------------------------------------------------------------------------

    template <typename T>
    consteval std::span<const script_method_info> build_methods();

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // annotation helpers ----------------------------------------------------------------------------------------------

    template <typename Ann>
    consteval bool has_ann(std::meta::info owner) {

        template for (constexpr auto a : std::define_static_array(std::meta::annotations_of(owner))) {
            if (std::meta::type_of(a) == ^^Ann)
                return true;
        }
        return false;
    }


    template <typename Ann, std::meta::info Owner>
    consteval Ann get_ann() {

        template for (constexpr auto a : std::define_static_array(std::meta::annotations_of(Owner))) {
            if (std::meta::type_of(a) == ^^Ann)
                return std::meta::extract<Ann>(a);
        }
        return Ann{};
    }

    // kind / label ----------------------------------------------------------------------------------------------------

    template <typename T>
    consteval script_kind kind_of_script() {

        int n = 0;
        script_kind k{};
        if constexpr (has_ann<GLT::reflect::annotations::entity_script>(^^T)) {
            k = script_kind::entity_script;
            n++;
        }
        if constexpr (has_ann<GLT::reflect::annotations::global_script>(^^T)) {
            k = script_kind::global_script;
            n++;
        }
        if constexpr (has_ann<GLT::reflect::annotations::world_generator>(^^T)) {
            k = script_kind::world_generator;
            n++;
        }
        if constexpr (has_ann<GLT::reflect::annotations::editor_action>(^^T)) {
            k = script_kind::editor_action;
            n++;
        }
        return n == 1 ? k : static_cast<script_kind>(0xFF);
    }


    template <typename T>
    consteval const char* editor_label_of() {

        if constexpr (has_ann<GLT::reflect::annotations::editor_action>(^^T)) {
            constexpr auto ann = get_ann<GLT::reflect::annotations::editor_action, ^^T>();
            return std::define_static_string(std::string_view{ ann.label });
        }
        return "";
    }

    // fields ----------------------------------------------------------------------------------------------------------

    template <typename T>
    consteval std::span<const script_field_info> build_fields() {

        auto td = reflect::type_descriptor_of<T>;
        std::vector<script_field_info> out;

        template for (constexpr auto member : std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()))) {

            // locate matching member_descriptor by offset
            const reflect::member_descriptor* md = nullptr;
            for (const auto& m : td.members) {
                if (m.offset == static_cast<u32>(std::meta::offset_of(member).bytes)) {
                    md = &m;
                    break;
                }
            }
            if (!md)
                continue;

            constexpr bool is_editor = has_ann<GLT::reflect::annotations::editor>(member);
            constexpr bool is_persist = has_ann<GLT::reflect::annotations::persist>(member);
            constexpr bool is_transient = has_ann<GLT::reflect::annotations::transient>(member);

            script_field_info fi{ .member = md, .role = script_field_role::runtime, .editor_category = "" };

            if constexpr (is_transient)
                fi.role = script_field_role::transient;
            else if constexpr (is_editor && is_persist)
                fi.role = script_field_role::editor_persist;
            else if constexpr (is_editor) {
                fi.role = script_field_role::editor;
                if constexpr (auto c = get_ann<GLT::reflect::annotations::editor, member>(); true)
                    fi.editor_category = std::define_static_string(std::string_view{ c.category });
            }
            else if constexpr (is_persist)
                fi.role = script_field_role::persist;

            out.push_back(fi);
        }
        return std::define_static_array(out);
    }

    // callable methods ------------------------------------------------------------------------------------------------

    template <typename T>
    consteval std::span<const script_method_info> build_methods() {

        std::vector<script_method_info> out;

        template for (constexpr auto f : std::define_static_array(
            std::meta::members_of(^^T, std::meta::access_context::unchecked()))) {

            if constexpr (std::meta::is_function(f) && has_ann<GLT::reflect::annotations::editor_callable>(f)) {

                // MVP: only (script_context&) is accepted. The (script_context&, const P&)
                // overload lands in Phase 4 (see roadmap item D2).
                static_assert(std::meta::parameters_of(f).size() == 1,
                    "[[=editor_callable]] methods must currently take only (script_context&)");

                constexpr const char* name = std::define_static_string(std::meta::identifier_of(f));
                constexpr const char* label = std::define_static_string(
                    std::string_view{ get_ann<GLT::reflect::annotations::editor_callable, f>().label });

                out.push_back(script_method_info{
                    .name = name,
                    .label = label,
                    .invoke = [](i_script* inst, script_context& ctx, std::span<const std::byte>) {
                        (inst->[:f:])(ctx);
                    },
                });
            }
        }
        return std::define_static_array(out);
    }

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    template <typename T> requires std::derived_from<T, i_script>
    void i_script_plugin::register_script_class(std::string_view registrant) {

        static_assert(!std::is_abstract_v<T>, "Script classes must be concrete");

        constexpr script_kind kind = kind_of_script<T>();
        static_assert(static_cast<int>(kind) <= static_cast<int>(script_kind::editor_action),
            "Script class needs exactly one kind annotation");

        script_class_info info{
            .name = reflect::type_name(^^T),
            .kind = kind,
            .editor_label = editor_label_of<T>(),
            .registrant = registrant,
            .fields = build_fields<T>(),
            .callable_methods = build_methods<T>(),
            .create = []() -> i_script* { return new T(); },
            .destroy = [](i_script* p)    { delete p; },
        };
        register_script_class_impl(info);
    }


    template <auto Namespace>
    void i_script_plugin::register_scripts_in_namespace(std::string_view registrant) {

        template for (constexpr auto member : std::define_static_array(
            std::meta::members_of(Namespace, std::meta::access_context::unchecked()))) {

            if constexpr (std::meta::is_namespace(member))
                register_scripts_in_namespace<member>(registrant);

            else if constexpr (std::meta::is_type(member)) {
                using M = [:std::meta::type_of(member):];
                if constexpr (std::is_class_v<M> && std::derived_from<M, i_script> && !std::is_abstract_v<M>)
                    register_script_class<M>(registrant);
            }
        }
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
