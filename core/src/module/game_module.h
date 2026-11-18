#pragma once

#include "module/abi.h"
#include "scripting/i_script_plugin.h"
#include "procedural/construct/reflect.h"

// Included by a game module (the project's C++). Usually only by the entry point CMake generates
// (cmake/gluttony_game_module.cmake), which includes every header of the module and then expands GLT_GAME_MODULE.
//
// One call registers everything below the module's root namespace, each item with the system it belongs to:
//   - classes deriving from script::i_script with a kind annotation   -> scripting   (registrant = module id)
//   - namespaces annotated [[=construct::pack{...}]]                   -> construct   (owner = module id)
//   - root_namespace::on_module_load(modules::context&)                -> called last, for everything else
//   - root_namespace::on_module_unload(modules::context&)              -> called first on unload
//
// What was registered under the module id is removed by the engine after on_module_unload; a module only undoes
// what the engine can not see (threads it started, global state, files).
//
// IMPORTANT: reflection sees what the entry point's translation unit sees. A script class or pack defined only in a
// .cpp file is invisible to it: declare them in headers (the generated entry point includes all of them).



namespace GLT::modules {

    namespace detail {

        // A function named [name] declared directly in namespace [ns], or a null reflection
        consteval std::meta::info find_hook(std::meta::info ns, std::string_view name) {

            for (const std::meta::info m : std::meta::members_of(ns, std::meta::access_context::unchecked()))
                if (std::meta::is_function(m) && std::meta::has_identifier(m) && std::meta::identifier_of(m) == name)
                    return m;
            return std::meta::info{};
        }

    }


    // @brief Registers everything below [Root] (see the top of this file). Called by the engine through descriptor::load
    template<std::meta::info Root>
    void register_module(context& ctx) {

        static_assert(std::meta::is_namespace(Root), "register_module<^^ns>: ns must be a namespace");

        if (ctx.scripting)
            ctx.scripting->register_scripts_in_namespace<Root>(ctx.id);

        if (ctx.construct)
            construct::register_packs_in<Root>(*ctx.construct, ctx.id);

        constexpr std::meta::info hook = detail::find_hook(Root, "on_module_load");
        if constexpr (hook != std::meta::info{})
            [:hook:](ctx);
    }


    // @brief The module's own unload step. The engine then removes everything registered under ctx.id
    template<std::meta::info Root>
    void unregister_module(context& ctx) {

        constexpr std::meta::info hook = detail::find_hook(Root, "on_module_unload");
        if constexpr (hook != std::meta::info{})
            [:hook:](ctx);
    }

}


// @brief The entry point of a game module: exports the descriptor the engine looks for
//
// [root_ns]: the module's root namespace (everything the module registers is below it). GLT_MODULE_NAME (set by the
// project CMake) is the module id
#define GLT_GAME_MODULE(root_ns)                                                                                            \
    extern "C" GLT_MODULE_EXPORT const ::GLT::modules::descriptor* gluttony_module_descriptor() {                            \
        static constexpr ::GLT::modules::descriptor descriptor{                                                              \
            .abi = ::GLT::modules::current_abi(),                                                                            \
            .name = GLT_MODULE_NAME,                                                                                        \
            .root_namespace = #root_ns,                                                                                     \
            .load = [](::GLT::modules::context& ctx) { ::GLT::modules::register_module<^^root_ns>(ctx); },                    \
            .unload = [](::GLT::modules::context& ctx) { ::GLT::modules::unregister_module<^^root_ns>(ctx); },                \
        };                                                                                                                  \
        return &descriptor;                                                                                                 \
    }
