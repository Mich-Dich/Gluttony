
#include "util/pch.h"
#include "i_window_plugin.h"

#include "util/io/serializer_yaml.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::platform {

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

	void serialize_window_attributes(const std::filesystem::path& path, GLT::platform::window_attributes& attributes, 
        const serializer::option option) {

        std::error_code error{};
        VALIDATE(GLT::vfs::is_directory(path, error) && !error, return, "", "Provided path is not a directory [{}]: [{}]", 
            path.generic_string(), error.message())

        VALIDATE(GLT::vfs::exists(path, error) && !error, return, "", "Project path does not exist [{}]: [{}]", 
            path.generic_string(), error.message())

        const auto config_path = path / GLT::config::CONFIG_DIR / 
            (GLT::config::file_type_to_string(GLT::config::type::app_settings) + GLT::config::FILE_EXTENSION_CONFIG);

        GLT::vfs::create_file(config_path, error);
        const auto buffer = error.message();
        VALIDATE(!error, return, "", "Failed to open/create config file: [{}]: [{}]", 
            config_path.generic_string(), error.message())

        GLT::serializer::yaml(config_path, "window", option)
            .entry(KEY_VALUE(attributes.title))
            .entry(KEY_VALUE(attributes.width))
            .entry(KEY_VALUE(attributes.height))
            .entry(KEY_VALUE(attributes.pos_x))
            .entry(KEY_VALUE(attributes.pos_y))
            .entry(KEY_VALUE(attributes.vsync))
            .entry(KEY_VALUE(attributes.size_state));
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

