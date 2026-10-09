#pragma once

#include <meta>

#include "asset/type.h"
#include "reflection/registry.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::reflect {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    namespace detail {
    
        // INTERNAL TEMPLATE DECLARATION ===============================================================================

        // Annotation readers ------------------------------------------------------------------------------------------

        template <typename Ann>
        consteval std::optional<Ann> find_annotation(std::meta::info owner);

        // Per-type descriptor -----------------------------------------------------------------------------------------

        template <typename T>
        consteval std::span<const member_descriptor> build_members();


        template <typename E> requires std::is_enum_v<E>
        consteval std::span<const enum_entry> build_enum_entries();


        template <typename T>
        consteval u16 descriptor_version();


        template <typename T>
        consteval type_descriptor make_descriptor();

        // Stable per-type storage. ODR-used → real address at runtime.
        template <typename T>
        inline constexpr type_descriptor descriptor_v = make_descriptor<T>();

        // INTERNAL FUNCTION DECLARATION ===============================================================================

        // Annotation readers ------------------------------------------------------------------------------------------

        consteval bool has_annotation(std::meta::info owner, std::meta::info tag_type);

        // Stable type name + hash -------------------------------------------------------------------------------------

        // Annotations win; otherwise identifier_of for class/enum, display_string_of for primitives and templates
        // Note: display_string_of is implementation defined — any type you serialize across compilers needs [[=name{"..."}]]
        consteval const char* type_name(std::meta::info type);


        consteval u32 type_hash_of(std::meta::info type);

        // Kind detection ----------------------------------------------------------------------------------------------

        consteval type_kind kind_of(std::meta::info type);

        // Per-member descriptor ---------------------------------------------------------------------------------------

        consteval member_descriptor build_member(std::meta::info member);
        
        // INTERNAL TEMPLATE IMPLEMENTATION ============================================================================

        // Annotation reader -------------------------------------------------------------------------------------------

        template <typename Ann>
        consteval std::optional<Ann> find_annotation(std::meta::info owner) {

            const std::vector<std::meta::info> anns = std::meta::annotations_of(owner);
            for (auto a : anns) {
                if (std::meta::type_of(a) == ^^Ann)
                    return std::meta::extract<Ann>(a);
            }
            return std::nullopt;
        }

        // Per-type descriptor -----------------------------------------------------------------------------------------

        template <typename T>
        consteval std::span<const member_descriptor> build_members() {

            if (!std::is_class_v<T> || std::is_enum_v<T>)
                return {};

            const std::vector<std::meta::info> member_infos =
                std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked());

            std::vector<member_descriptor> out;
            for (auto m : member_infos)
                out.push_back(build_member(m));

            return std::define_static_array(out);
        }


        template <typename E> requires std::is_enum_v<E>
        consteval std::span<const enum_entry> build_enum_entries() {

            const std::vector<std::meta::info> enum_infos = std::meta::enumerators_of(^^E);

            std::vector<enum_entry> out;
            for (auto e : enum_infos) {
                out.push_back({
                    std::define_static_string(std::meta::identifier_of(e)),
                    static_cast<i64>(std::meta::extract<E>(e))
                });
            }
            return std::define_static_array(out);
        }


        template <typename T>
        consteval u16 descriptor_version() {

            if (auto v = find_annotation<annotations::version>(^^T))
                return v->value;
            return 1;
        }


        template <typename T>
        consteval type_descriptor make_descriptor() {

            type_descriptor td{};
            td.name = type_name(^^T);
            td.hash = fnv1a_32(td.name);
            td.size = static_cast<u32>(sizeof(T));
            td.alignment = static_cast<u32>(alignof(T));
            td.kind = kind_of(^^T);
            td.version = descriptor_version<T>();

            if constexpr (std::is_class_v<T> && !std::is_enum_v<T>)     td.members = build_members<T>();
            if constexpr (std::is_enum_v<T>)                            td.enum_values = build_enum_entries<T>();
            if constexpr (std::is_default_constructible_v<T>)           td.default_construct = [](void* p) { new (p) T(); };
            if constexpr (std::is_copy_constructible_v<T>)              td.copy_construct = [](void* d, const void* s) { new (d) T(*static_cast<const T*>(s)); };
            if constexpr (std::is_destructible_v<T>)                    td.destruct = [](void* p) { static_cast<T*>(p)->~T(); };

            return td;
        }

        // INTERNAL FUNCTION IMPLEMENTATION ============================================================================

        // Annotation reader -------------------------------------------------------------------------------------------

        consteval bool has_annotation(std::meta::info owner, std::meta::info tag_type) {

            const std::vector<std::meta::info> anns = std::meta::annotations_of(owner);
            for (auto a : anns)
                if (std::meta::type_of(a) == tag_type)
                    return true;
            return false;
        }

        // Stable type name + hash -------------------------------------------------------------------------------------

        consteval const char* type_name(std::meta::info type) {

            if (auto n = find_annotation<annotations::name>(type))
                return std::define_static_string(std::string_view(n->value));

            if (std::meta::has_identifier(type))
                return std::define_static_string(std::meta::identifier_of(type));

            return std::define_static_string(std::meta::display_string_of(type));
        }


        consteval u32 type_hash_of(std::meta::info type) { return fnv1a_32(type_name(type)); }

        // Kind detection ----------------------------------------------------------------------------------------------

        consteval type_kind kind_of(std::meta::info type) {

            if (type == ^^bool)                                 return type_kind::boolean;

            if (std::meta::is_arithmetic_type(type)) {
                const auto sz = std::meta::size_of(type);
                if (std::meta::is_floating_point_type(type)) {
                    if (sz == 4)                                return type_kind::f32;
                    if (sz == 8)                                return type_kind::f64;
                } else if (std::meta::is_signed_type(type)) {
                    switch (sz) {
                        case 1:                                 return type_kind::i8;
                        case 2:                                 return type_kind::i16;
                        case 4:                                 return type_kind::i32;
                        case 8:                                 return type_kind::i64;
                    }
                } else {
                    switch (sz) {
                        case 1:                                 return type_kind::u8;
                        case 2:                                 return type_kind::u16;
                        case 4:                                 return type_kind::u32;
                        case 8:                                 return type_kind::u64;
                    }
                }
                return type_kind::unknown;
            }

            if (std::meta::is_enum_type(type))                  return type_kind::enum_type;

            if (std::meta::is_class_type(type)) {

                // string / path / handle / container — matched by identity with known types (extend as you add engine types)
                if (type == ^^std::string)                      return type_kind::string_type;
                if (type == ^^std::filesystem::path)            return type_kind::path_type;
                if (type == ^^GLT::asset::handle)               return type_kind::handle_type;
                if (type == ^^GLT::UUID)                        return type_kind::uuid_type;

                // Containers — simple trait shape
                if (std::meta::has_template_arguments(type)) {
                    auto tmpl = std::meta::template_of(type);
                    if (tmpl == ^^std::vector || tmpl == ^^std::array || tmpl == ^^std::map || tmpl == ^^std::unordered_map)
                        return type_kind::container_type;
                }

                const std::vector<std::meta::info> member_infos = std::meta::nonstatic_data_members_of(type,
                    std::meta::access_context::unchecked());
                if (!member_infos.empty())
                    return type_kind::struct_type;

                return type_kind::opaque_type;
            }

            return type_kind::unknown;
        }

        // Per-member descriptor ---------------------------------------------------------------------------------------

        consteval member_descriptor build_member(std::meta::info member) {
            
            member_descriptor md{};
            md.name = std::define_static_string(std::meta::identifier_of(member));
            md.type_hash = type_hash_of(std::meta::type_of(member));
            md.offset = static_cast<u32>(std::meta::offset_of(member).bytes);
            md.kind = kind_of(std::meta::type_of(member));

            // Flags
            u32 f = static_cast<u32>(member_flags::serialize);   // default-on
            if (has_annotation(member, ^^annotations::skip))                        f = static_cast<u32>(member_flags::skip);
            if (has_annotation(member, ^^annotations::transient))                   f |= static_cast<u32>(member_flags::transient);
            if (has_annotation(member, ^^annotations::asset_ref) ||
                kind_of(std::meta::type_of(member)) == type_kind::handle_type)      f |= static_cast<u32>(member_flags::asset_ref);
            if (has_annotation(member, ^^annotations::readonly))                    f |= static_cast<u32>(member_flags::readonly);
            if (has_annotation(member, ^^annotations::color))                       f |= static_cast<u32>(member_flags::color);
            if (has_annotation(member, ^^annotations::multiline))                   f |= static_cast<u32>(member_flags::multiline);
            if (kind_of(std::meta::type_of(member)) == type_kind::container_type)   f |= static_cast<u32>(member_flags::is_container);
            if (kind_of(std::meta::type_of(member)) == type_kind::enum_type)        f |= static_cast<u32>(member_flags::is_enum);
            md.flags = f;

            // String hints
            if (auto dn = find_annotation<annotations::display_name>(member))       md.display_name = std::define_static_string(std::string_view(dn->value));
            if (auto tt = find_annotation<annotations::tooltip>(member))            md.tooltip = std::define_static_string(std::string_view(tt->value));
            if (auto cat = find_annotation<annotations::category>(member))          md.category = std::define_static_string(std::string_view(cat->value));
            if (auto r = find_annotation<annotations::range>(member)) {
                md.range_min = r->min;
                md.range_max = r->max;
                md.flags |= static_cast<u32>(member_flags::has_range);
            }
            return md;
        }
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    template <typename T>
    inline constexpr const type_descriptor& type_descriptor_of = detail::descriptor_v<T>;


    template <typename T>
    [[nodiscard]] constexpr u32 hash_of() noexcept { return detail::descriptor_v<T>.hash; }


    template <typename T>
    [[nodiscard]] const type_descriptor* type_of() noexcept { return registry().find(detail::descriptor_v<T>.hash); }

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
