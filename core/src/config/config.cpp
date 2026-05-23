
#include "util/pch.h"

#include "plugin_system/i_window_plugin.h"
#include "util/io/serializer_yaml.h"

#include "config.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::config {

    // CONSTANTS =======================================================================================================

    constexpr std::array<std::string_view, static_cast<size_t>(type::launcher) +1> s_file_type_names = {
        "ui",
        "imgui",
        "input",
        "app_settings",
        "plugin",
        "project",
        "launcher",
    };

    // MACROS ==========================================================================================================

    #define REMOVE_WHITE_SPACE(line)                                                                                    \
        line.erase(std::remove_if(line.begin(), line.end(),                                                             \
            [](char c) { return c == '\r' || c == '\n' || c == '\t'; }),                                                \
            line.end());

    #define BUILD_CONFIG_PATH(x)                                                                                        \
        ( CONFIG_DIR / (file_type_to_string(x) + FILE_EXTENSION_CONFIG) )

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    void init() {

        create_config_files(GLT::util::get_executable_path());
    }


	void serialize_window_attributes(GLT::platform::window_attributes& attributes, const serializer::option option) {

        const auto config_path = GLT::util::get_executable_path() / BUILD_CONFIG_PATH(type::app_settings);
        std::error_code error{};
        vfs::create_file(config_path, error);
        const auto buffer = error.message();
        ASSERT(!error, "", "Failed to open/create config file: [{}]: [{}]", config_path.generic_string(), error.message())

        serializer::yaml(config_path, "window", option)
            .entry(KEY_VALUE(attributes.title))
            .entry(KEY_VALUE(attributes.width))
            .entry(KEY_VALUE(attributes.height))
            .entry(KEY_VALUE(attributes.pos_x))
            .entry(KEY_VALUE(attributes.pos_y))
            .entry(KEY_VALUE(attributes.vsync))
            .entry(KEY_VALUE(attributes.size_state));
    }


    bool create_config_files(const std::filesystem::path& project_path) {

        const auto config_path = project_path / CONFIG_DIR;
        std::error_code error{};
        vfs::create_directories(config_path, error);
        VALIDATE(!error, return false, "", "Failed to create config dir [{}]: [{}]", config_path.generic_string(), error.message());

        bool success = true;
        for (int i = 0; i <= static_cast<int>(type::launcher); ++i) {

            std::filesystem::path file_path = config_path / (file_type_to_string(static_cast<type>(i)) + FILE_EXTENSION_CONFIG);
            std::error_code error{};
            vfs::create_file(file_path, error);
            VALIDATE(!error, success = false; continue, "", "Failed to open/create config file: [{}]: [{}]", 
                file_path.generic_string(), error.message())
        }

        return success;
    }


    [[nodiscard]] std::string file_type_to_string(const type value) {

        auto targeted_index = static_cast<size_t>(value);
        if (targeted_index < s_file_type_names.size())
            return std::string(s_file_type_names[targeted_index]);
        return "unknown";
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
