
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // @brief In-memory representation of a [.gltproject] file
    //
    // Holds the persistent identity of a project: its metadata, its paths, and the entry-world references used by the
    // launcher and the editor. Serialized to / from YAML alongside the project directory, and treated as the source of
    // truth for [PROJECT_CONTENT_DIR-relative lookups
    struct project {

        std::filesystem::path       project_path{};        // system path to the project file for a gluttony project
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
        GLT::system_time            last_modified{};        // Timestamp of when the project was last modified
        std::string                 description{};          // A short description of this project
        std::vector<std::string>    tags{};                 // Tags or categories


        // @brief Checks that a path points at a real Gluttony project
        //
        // Requires the path to be an existing directory that contains the project marker file (<PROJECT_NAME><PROJECT_EXTENTION>)
        //
        // @param project_dir_path  Directory to test
        // @return true if the directory exists and hosts a project file
        [[nodiscard]] bool is_valid_project_path(const std::filesystem::path& project_dir_path);


        // @brief Serializes the project data of the currently-loaded project
        //
        // Writes to [<project_path>/<PROJECT_NAME><PROJECT_EXTENTION>] as YAML Requires [project_path] to be set
        //
        // @param option  Read, write, or both (see GLT::serializer::option)
        void serialize_projects_data(const GLT::serializer::option option);


        // @brief Loads (or saves) the project data at a specific directory
        //
        // Sets [project_path] to [project_dir_path], then delegates to the other overload. Fails silently (with a log)
        // if the directory isn't a valid project
        //
        // @param project_dir_path  Directory containing the project file
        // @param option            Read, write, or both
        void serialize_projects_data(const std::filesystem::path& project_dir_path, const GLT::serializer::option option);


        // @brief Strips everything up to and including the content directory
        //
        // Used when a caller has an absolute path and needs the project-relative portion. If [CONTENT_DIR] isn't present,
        // returns an empty path
        //
        // @param full_path  Path to strip
        // @return The portion of [full_path] starting at [CONTENT_DIR], or an empty path
        static std::filesystem::path extract_path_from_project_dir(const std::filesystem::path& full_path);


        // @brief Normalizes a path into the registry's content-relative dialect
        //
        // Accepts either a project-relative path (returned as-is) or an absolute path that lives under PROJECT_CONTENT_DIR
        // (converted). Anything else is rejected
        //
        // @param path  Path to normalize
        // @return A lexically-normalized content-relative path, or an empty path if [path] isn't inside the project
        static std::filesystem::path to_content_relative(const std::filesystem::path& path);


        // @brief Extracts the content-dir-relative part of an absolute path
        //
        // When [current_project] is true, compares lexically against [<application::get_project_path()>/<CONTENT_DIR>]; 
        // paths that escape that root (via [..]) are reported as empty
        //
        // When [current_project] is false, falls back to scanning the path for a [CONTENT_DIR] component and stripping
        // everything up to it
        //
        // @param full_path Path to strip
        // @param current_project Whether to compare against the live project root
        // @return The content-dir-relative remainder, or an empty path if none could be derived
        static std::filesystem::path extract_path_from_project_content_dir(const std::filesystem::path& full_path, const bool current_project = true);

    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
