
#pragma once

#include <filesystem>

// FORWARD DECLARATIONS ================================================================================================

namespace GLT {

    // CONSTANTS =======================================================================================================

    namespace config {

        inline constexpr std::string                ASSET_EXTENTION(".gltasset");       // Extension for asset files
        inline constexpr std::string                PROJECT_EXTENTION(".gltproj");      // Extension for project files
        inline constexpr std::string                FILE_EXTENSION_CONFIG(".yml");      // Extension for YAML config files
        inline constexpr std::string                FILE_EXTENSION_INI(".ini");         // Extension for INI config files

        inline const std::filesystem::path          METADATA_DIR("metadata");           // relative to executable dir
        inline const std::filesystem::path          CONFIG_DIR("config");               // relative to executable dir
        inline const std::filesystem::path          CONTENT_DIR("content");             // relative to executable dir
        inline const std::filesystem::path          SOURCE_DIR("source");               // relative to executable dir
        inline const std::filesystem::path          PLUGIN_DIR("plugin");               // relative to executable dir

        #if defined(PLATFORM_LINUX)

            inline constexpr std::string            GLT_HELPER("gluttony_helper");

        #elif defined(PLATFORM_WINDOWS)

            inline constexpr std::string            GLT_HELPER("gluttony_helper.exe");

        #endif

    }

    // MACROS ==========================================================================================================

    #if 1
        #define RENDER_API_VULKAN
    #else
        #define RENDER_API_OPENGL
    #endif

    // collect timing-data from every major function?
    #define PROFILE								    0	// general
    #define PROFILE_APPLICATION                     0
    #define PROFILE_RENDERER                        0

    // log assert and validation behaviour?
    // NOTE - expr in assert/validation will still be executed
    #define ENABLE_LOGGING_FOR_ASSERTS              1
    #define ENABLE_LOGGING_FOR_VALIDATION           1

    // #define PROJECT_PATH				            GLT::application::get().get_project_path()
    // #define PROJECT_NAME				            GLT::application::get().get_project_data().name
    // #define ENGINE_CONTENT_PATH			        GLT::util::get_executable_path() / "assets"
    // #define ENGINE_SHADER_PATH			        GLT::util::get_executable_path() / "shaders_compiled"
    // #define ENGINE_RAW_SHADER_PATH		        GLT::util::get_executable_path() / "shaders"

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
