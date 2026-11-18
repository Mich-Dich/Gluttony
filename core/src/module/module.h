#pragma once

// Game modules: the project's own C++ (<project>/game), built by the project's CMakeLists.txt into
// <project>/build/<config>/bin/<name>.so and loaded by the engine after its plugins (module_manager.h).
//
// This header is shared by both sides: the engine reads a module's [descriptor] (first its [abi_stamp]) and hands the
// module a [context] in load / unload. A module does not include this directly, it includes module/game_module.h
// (or nothing at all: the project CMake generates the entry point, see cmake/gluttony_game_module.cmake).
//
// See documentation/game_module.md



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::script {
    class i_script_plugin;
}
namespace GLT::construct {
    class i_registry;
}

namespace GLT::modules {

    // CONSTANTS =======================================================================================================

    // Bump by hand when something changes that the layout hash (module/abi.h) can not see: calling conventions of the
    // descriptor's function pointers, the meaning of a field, the order of load steps a module relies on
    inline constexpr u32                                ABI_VERSION = 1;

    // MACROS ==========================================================================================================

    #if defined(PLATFORM_LINUX)
        #define GLT_MODULE_EXPORT                       __attribute__((visibility("default")))
    #elif defined(PLATFORM_WINDOWS)
        #define GLT_MODULE_EXPORT                       __declspec(dllexport)
    #endif

    // Name of the symbol every module exports (extern "C", returns const descriptor*)
    #define GLT_MODULE_DESCRIPTOR_SYMBOL                "gluttony_module_descriptor"

    // TYPES ===========================================================================================================

    enum class build_config : u8 {

        debug = 0,
        release,
        release_with_debug_info,
    };


    // The configuration this translation unit is compiled in (DEBUG / RELEASE / RELEASE_WITH_DEBUG_INFO, set by CMake)
    consteval build_config current_build_config() {

        #if defined(DEBUG)
            return build_config::debug;
        #elif defined(RELEASE_WITH_DEBUG_INFO)
            return build_config::release_with_debug_info;
        #else
            return build_config::release;
        #endif
    }


    // Directory name below <project>/build/ ("debug", "release", "release_with_debug_info")
    constexpr std::string_view build_dir_name(const build_config config) {

        switch (config) {
            case build_config::debug:                   return "debug";
            case build_config::release_with_debug_info: return "release_with_debug_info";
            case build_config::release:                 break;
        }
        return "release";
    }


    // CMAKE_BUILD_TYPE of a configuration ("Debug", "Release", "RelWithDebInfo")
    constexpr std::string_view cmake_build_type(const build_config config) {

        switch (config) {
            case build_config::debug:                   return "Debug";
            case build_config::release_with_debug_info: return "RelWithDebInfo";
            case build_config::release:                 break;
        }
        return "Release";
    }


    // @brief What engine and module must agree on before the engine trusts anything else in the descriptor
    //
    // Compared field by field: a module built with another compiler, in another configuration, against other engine
    // headers (layout_hash) or with another ABI_VERSION is refused with a message that says which one differs
    struct abi_stamp {

        u32                                             abi_version = 0;
        u32                                             descriptor_size = 0;
        u32                                             compiler = 0;           // GCC: major * 10000 + minor * 100 + patch
        u8                                              config = 0;             // build_config
        u8                                              linkage = 0;            // LINKAGE_STYLE
        u8                                              cxx11_abi = 0;          // _GLIBCXX_USE_CXX11_ABI
        u8                                              library_assertions = 0; // _GLIBCXX_ASSERTIONS (changes some layouts)
        u64                                             layout_hash = 0;        // engine types a module touches (module/abi.h)

        [[nodiscard]] constexpr bool operator==(const abi_stamp&) const = default;
    };


    // @brief Handed to a module's load / unload. Owned by the engine; [id] stays valid while the module is loaded
    //
    // Everything a module registers is tagged with [id] (scripting registrant, construct owner, editor owner): the
    // engine removes all of it after unload, so a module only has to undo what the engine can not see (its own threads,
    // files, global state)
    struct context {

        std::string_view                                id{};                   // module name ("test_project")
        std::filesystem::path                           project_dir{};
        std::filesystem::path                           library_path{};         // the built file (<project>/build/<cfg>/bin/x.so)
        script::i_script_plugin*                        scripting = nullptr;    // null: no scripting plugin loaded
        construct::i_registry*                          construct = nullptr;    // procedural construction packs
    };


    // @brief Exported by every module through [GLT_MODULE_DESCRIPTOR_SYMBOL]
    struct descriptor {

        abi_stamp                                       abi{};                  // MUST stay the first member
        const char*                                     name = nullptr;         // module id, unique per process
        const char*                                     root_namespace = nullptr;
        void                                            (*load)(context&) = nullptr;
        void                                            (*unload)(context&) = nullptr;
    };

    // FUNCTION DECLARATION ============================================================================================

    // @brief Differences between two stamps, one per line ("compiler: engine 16.2.0, module 16.1.0"). Empty if equal
    [[nodiscard]] inline std::string abi_difference(const abi_stamp& engine, const abi_stamp& module) {

        std::string out{};
        auto line = [&](std::string_view what, auto a, auto b) {
            if (a != b)
                out += std::format("{}: engine {}, module {}\n", what, a, b);
        };
        auto version = [](u32 v) { return std::format("{}.{}.{}", v / 10000, (v / 100) % 100, v % 100); };

        line("ABI version", engine.abi_version, module.abi_version);
        line("descriptor size", engine.descriptor_size, module.descriptor_size);
        if (engine.compiler != module.compiler)
            out += std::format("compiler: engine {}, module {}\n", version(engine.compiler), version(module.compiler));
        if (engine.config != module.config)
            out += std::format("configuration: engine {}, module {}\n", build_dir_name(static_cast<build_config>(engine.config)),
                build_dir_name(static_cast<build_config>(module.config)));
        line("linkage style", engine.linkage, module.linkage);
        line("_GLIBCXX_USE_CXX11_ABI", engine.cxx11_abi, module.cxx11_abi);
        line("_GLIBCXX_ASSERTIONS", engine.library_assertions, module.library_assertions);
        if (engine.layout_hash != module.layout_hash)
            out += std::format("engine headers: layout hash engine {:016x}, module {:016x} (the module was built against other "
                "engine sources: rebuild the engine or the module)\n", engine.layout_hash, module.layout_hash);
        return out;
    }

}
