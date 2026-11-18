#pragma once

#include "module/module.h"
#include "plugin_system/plugin_manager.h"
#include "editor/i_editor.h"
#include "scripting/i_script_plugin.h"
#include "scripting/script_context.h"
#include "scripting/script_binding_component.h"
#include "procedural/construct/registry.h"

// The ABI stamp of this translation unit. Compiled into the engine (module_manager.cpp) and into every module
// (game_module.h); the engine refuses a module whose stamp differs.
//
// [layout_hash] is computed by reflection over the engine types a module works with: names, sizes and offsets of their
// data members, names and order of their virtual functions. A module built against older or newer engine headers than
// the running engine gets a different hash and is refused instead of crashing later (the "stale binary" class of bugs).
// Types not listed here are not checked: add a type when modules start using it.



namespace GLT::modules {

    namespace detail {

        consteval u64 hash_bytes(u64 h, std::string_view s) {

            for (const char c : s) {
                h ^= static_cast<u8>(c);
                h *= 1099511628211ull;                  // FNV-1a
            }
            return h;
        }


        consteval u64 hash_value(u64 h, u64 v) {

            for (u32 i = 0; i < 8; ++i) {
                h ^= (v >> (i * 8)) & 0xff;
                h *= 1099511628211ull;
            }
            return h;
        }


        consteval u64 hash_type(u64 h, std::meta::info type) {

            h = hash_bytes(h, std::meta::display_string_of(type));
            h = hash_value(h, std::meta::size_of(type));
            h = hash_value(h, std::meta::alignment_of(type));
            for (const std::meta::info m : std::meta::members_of(type, std::meta::access_context::unchecked())) {

                if (std::meta::is_nonstatic_data_member(m)) {
                    if (std::meta::has_identifier(m))
                        h = hash_bytes(h, std::meta::identifier_of(m));
                    h = hash_value(h, static_cast<u64>(std::meta::offset_of(m).bytes));
                    h = hash_value(h, std::meta::size_of(std::meta::type_of(m)));

                } else if (std::meta::is_function(m) && std::meta::is_virtual(m)) {
                    if (std::meta::is_destructor(m))
                        h = hash_bytes(h, "~");
                    else {
                        if (std::meta::has_identifier(m))
                            h = hash_bytes(h, std::meta::identifier_of(m));
                        h = hash_bytes(h, std::meta::display_string_of(std::meta::type_of(m)));
                    }
                }
            }
            return h;
        }


        // The engine types a module touches through headers (by value, by layout or through a vtable)
        consteval u64 layout_hash() {

            u64 h = 14695981039346656037ull;
            for (const std::meta::info t : {
                     ^^GLT::modules::context,
                     ^^GLT::modules::descriptor,
                     ^^GLT::plugin_manager::i_plugin,
                     ^^GLT::editor::i_editor,
                     ^^GLT::editor::asset_creator,
                     ^^GLT::script::i_script,
                     ^^GLT::script::i_script_plugin,
                     ^^GLT::script::script_class_info,
                     ^^GLT::script::script_field_info,
                     ^^GLT::script::script_method_info,
                     ^^GLT::script::default_global,
                     ^^GLT::script::i_script_host,
                     ^^GLT::script::script_host_context,
                     ^^GLT::script::script_context,
                     ^^GLT::script::script_context_access::fields,
                     ^^GLT::script::script_binding,
                     ^^GLT::script::script_binding_component,
                     ^^GLT::construct::i_registry,
                     ^^GLT::construct::pack_desc,
                     ^^GLT::construct::generator_desc,
                     ^^GLT::construct::generator_instance,
                     ^^GLT::construct::setting_desc,
                     ^^GLT::construct::role_desc,
                     ^^GLT::construct::guide_template_desc,
                     ^^GLT::construct::preset_desc,
                     ^^GLT::construct::construct_data,
                     ^^std::string,
                     ^^std::function<void()>,
                     ^^std::filesystem::path,
                 })
                h = hash_type(h, t);
            return h;
        }

    }


    // @brief The stamp of the translation unit that calls it (engine and module each call it in their own build)
    consteval abi_stamp current_abi() {

        return abi_stamp{
            .abi_version = ABI_VERSION,
            .descriptor_size = static_cast<u32>(sizeof(descriptor)),
            #if defined(__GNUC__) && !defined(__clang__)
                .compiler = __GNUC__ * 10000u + __GNUC_MINOR__ * 100u + __GNUC_PATCHLEVEL__,
            #else
                .compiler = 0,
            #endif
            .config = static_cast<u8>(current_build_config()),
            .linkage = static_cast<u8>(LINKAGE_STYLE),
            #if defined(_GLIBCXX_USE_CXX11_ABI)
                .cxx11_abi = static_cast<u8>(_GLIBCXX_USE_CXX11_ABI),
            #else
                .cxx11_abi = 0,
            #endif
            #if defined(_GLIBCXX_ASSERTIONS)
                .library_assertions = 1,
            #else
                .library_assertions = 0,
            #endif
            .layout_hash = detail::layout_hash(),
        };
    }

}
