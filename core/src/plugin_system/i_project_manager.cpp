
#include "util/pch.h"
#include "i_project_manager.h"

#include "util/core_config.h"
#include "util/io/vfs.h"
#include "util/io/directory_iterator.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::project_manager {

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

    bool project::is_valid_project_path(const std::filesystem::path& project_file) { 
        
        std::error_code error{};
        const bool project_exists = GLT::vfs::exists(project_file, error);
        return (!project_file.empty() 
            && (project_exists && !error) 
            && project_file.extension() == GLT::config::PROJECT_EXTENTION); 
    }


    void project::serialize_projects_data(const GLT::serializer::option option) {

        std::error_code error{};
        auto iterator = GLT::vfs::directory_iterator(project_path, error);
        VALIDATE(!error, return, "", "Failed to create directory_iterator for [{}]", project_path)

        for (const auto& entry : iterator) {

            const bool entry_is_directory = entry.is_directory(error);
            if ((!error && entry_is_directory) || entry.path().extension() != GLT::config::PROJECT_EXTENTION)
                continue;

            GLT::serializer::yaml(entry.path(), "project_data", option)
                .entry(KEY_VALUE(display_name))
                .entry(KEY_VALUE(name))
                //.entry(KEY_VALUE(data.ID))
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

            break;
        }
    }
    
    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
