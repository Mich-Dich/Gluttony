
#include "util/pch.h"
#include "project.h"

#include "application.h"

#include "util/io/serializer_yaml.h"
#include "util/io/directory_iterator.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    bool project::is_valid_project_path(const std::filesystem::path& project_dir_path) { 

        std::error_code error{};
        const bool path_is_dir = GLT::vfs::is_directory(project_dir_path, error);
        VALIDATE(path_is_dir && !error, return false, "", "Provided path is not a directory [{}]: [{}]", 
            project_dir_path.generic_string(), error.message())

        const bool project_exists = GLT::vfs::exists(project_dir_path, error);
        VALIDATE(project_exists && !error, return false, "", "Project directory does not exist [{}]: [{}]", 
            project_dir_path.generic_string(), error.message())

        bool project_file_found = false;
        const bool project_file_exists = GLT::vfs::exists(project_dir_path / (GLT::config::PROJECT_NAME + GLT::config::PROJECT_EXTENTION), error);
        VALIDATE(project_file_exists && !error, return false, "", "Project file does not exist [{}]: [{}]", 
            project_dir_path.generic_string(), error.message())

        return true;
    }


    void project::serialize_projects_data(const GLT::serializer::option option) {

        VALIDATE(!project_path.empty(), return, "", "Cant serialize project data is field [project_path] is empty")

        std::error_code error{};
        auto iterator = GLT::vfs::directory_iterator(project_path, error);
        VALIDATE(!error, return, "", "Failed to create directory_iterator for [{}]: [{}]", project_path, error.message())

        GLT::serializer::yaml(project_path / (GLT::config::PROJECT_NAME + GLT::config::PROJECT_EXTENTION), "project_data", option)
            .entry(KEY_VALUE(display_name))
            .entry(KEY_VALUE(name))
            .entry(KEY_VALUE(ID))
            .entry(KEY_VALUE(engine_version))
            .entry(KEY_VALUE(project_version))
            .entry(KEY_VALUE(build_path))
            .entry(KEY_VALUE(start_world))
            .entry(KEY_VALUE(editor_start_world))
            .entry(KEY_VALUE(last_modified))
            .entry(KEY_VALUE(description))
            .vector("tags", tags, [&](serializer::yaml& inner, u64 x) {
                inner.entry("tag", tags[x]);
            });
    }


    void project::serialize_projects_data(const std::filesystem::path& project_dir_path, const GLT::serializer::option option) {

        VALIDATE(is_valid_project_path(project_dir_path), return, "", "Provided path is not valid project path [{}]", project_dir_path)
        project_path = project_dir_path;
        serialize_projects_data(option);
    }


    std::filesystem::path project::extract_path_from_project_dir(const std::filesystem::path& full_path) {

        std::string full_path_str = full_path.string();
        std::string folder_marker = std::string(GLT::config::CONTENT_DIR);
        size_t pos = full_path_str.find(folder_marker);
        if (pos != std::string::npos) {

            std::string result_str = full_path_str.substr(pos);
            return std::filesystem::path(result_str);

        } else {

            LOG(trace, "NOT FOUND");
            return {};
        }
    }


    std::filesystem::path project::to_content_relative(const std::filesystem::path& path) {

        if (path.empty())
            return {};

        if (!path.has_root_path())                                                        // Already project-relative
            return path.lexically_normal();

        auto relative = GLT::project::extract_path_from_project_content_dir(path);        // Absolute. use project helper
        if (relative.empty())
            return {};

        return relative.lexically_normal();
    }


    std::filesystem::path project::extract_path_from_project_content_dir(const std::filesystem::path& full_path, const bool current_project) {

        if (current_project) {

            const auto project_path = GLT::application::get().get_project_path();
            const auto expected_prefix = project_path / GLT::config::CONTENT_DIR;

            // Purely lexical: full_path must live under <project_path>/<CONTENT_DIR>
            const auto rel = full_path.lexically_relative(expected_prefix);

            // empty -> different root (e.g. absolute vs relative)
            // ".." lead -> path escapes expected_prefix
            if (rel.empty() || *rel.begin() == "..")
                return {};                                  // caller can treat this as "not our project's content"

            return rel;
        }

        // Fallback: strip everything up to and including CONTENT_DIR
        std::filesystem::path result;
        bool start_adding = false;
        for (const auto& part : full_path) {

            if (start_adding)
                result /= part;

            if (part == GLT::config::CONTENT_DIR)
                start_adding = true;
        }
        return result;
    }


    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
