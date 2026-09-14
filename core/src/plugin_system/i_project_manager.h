
#pragma once

#include "util/io/serializer_yaml.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::project_manager {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct project {

        std::filesystem::path        project_path{};        // system path to the project file for a gluttony project
        // meta_data

        std::string                 display_name{};         // this name will be used in the launcher and editor
        std::string                 name{};                 // this name is the name of the solution & export-folder

        GLT::UUID                   ID;
        GLT::version                engine_version{};       // version of the engine used, used by launcher, engine, editor, ...
        GLT::version                project_version{};      // version of this project, mostly useful to the user for version management

        // paths
        std::filesystem::path       build_path{};           // system path for exported builds
        std::filesystem::path       start_world{};          // system path to the first world when executing project
        std::filesystem::path       editor_start_world{};   // system path to the first world when developing the project

        // Dependencies
        // std::vector<std::string> engine_plugins;            // List of enabled or required plugins
        // std::vector<std::string> external_libraries;        // List of external libraries

        // User data
        GLT::system_time            last_modified{};        // Timestamp of when the project was last modified.
        std::string                 description{};          // A short description of this project
        std::vector<std::string>    tags{};                 // Tags or categories


        [[nodiscard]] bool is_valid_project_path(const std::filesystem::path& project_file);


        void serialize_projects_data(const GLT::serializer::option option);
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
