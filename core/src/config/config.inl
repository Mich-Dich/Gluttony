#pragma once

#include "util/pch.h"
#include "config.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::config {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================
    
    FORCE_INLINE_R std::filesystem::path get_filepath_from_config_type(const type type) {

        return std::filesystem::path(config::CONFIG_DIR) / (config::file_type_to_string(type) + config::FILE_EXTENSION_CONFIG); 
    }


    FORCE_INLINE_R std::filesystem::path get_filepath_from_config_type_ini(const type type) {

        return std::filesystem::path(config::CONFIG_DIR) / (config::file_type_to_string(type) + FILE_EXTENSION_INI); 
    }

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}