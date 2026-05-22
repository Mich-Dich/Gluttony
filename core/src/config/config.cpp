
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

        const std::filesystem::path root_dir = GLT::util::get_executable_path() / CONFIG_DIR;
        vfs::create_directories(root_dir);
        LOG(trace, "Checking Engine config files at [{}]", (root_dir).string());
        
        for (int i = 0; i <= static_cast<int>(type::input); ++i) {

            std::filesystem::path file_path = root_dir / CONFIG_DIR / (file_type_to_string(static_cast<type>(i)) + FILE_EXTENSION_CONFIG);
            std::ofstream config_file(file_path, std::ios::app);
            VALIDATE(config_file.is_open(), return, "", "Failed to open/create config file: [{}]", file_path)
            config_file.close();
        }
    }


	void serialize_window_attributes(GLT::platform::window_attributes& attributes, const serializer::option option) {

        const auto config_path = GLT::util::get_executable_path() / BUILD_CONFIG_PATH(type::app_settings);
        vfs::create_file(config_path);
        serializer::yaml(config_path, "window", option)
            .entry(KEY_VALUE(attributes.title))
            .entry(KEY_VALUE(attributes.width))
            .entry(KEY_VALUE(attributes.height))
            .entry(KEY_VALUE(attributes.pos_x))
            .entry(KEY_VALUE(attributes.pos_y))
            .entry(KEY_VALUE(attributes.vsync))
            .entry(KEY_VALUE(attributes.size_state));
    }


    void create_config_files_for_project(const std::filesystem::path& project_dir) {

        vfs::create_directory(project_dir / CONFIG_DIR);
        LOG(trace, "Checking project config files at [{}]", project_dir / CONFIG_DIR);
        for (int i = 0; i <= static_cast<int>(type::input); ++i) {

            std::filesystem::path file_path = project_dir / CONFIG_DIR /
                (file_type_to_string(static_cast<type>(i)) + FILE_EXTENSION_CONFIG);
            std::ofstream config_file(file_path, std::ios::app);
            VALIDATE(config_file.is_open(), return, "", "Failed to open/create config file: [{}]", file_path)
            config_file.close();
        }
    }


    [[nodiscard]] std::string file_type_to_string(const type value) {

        auto targeted_index = static_cast<size_t>(value);
        if (targeted_index < s_file_type_names.size())
            return std::string(s_file_type_names[targeted_index]);
        return "unknown";
    }


    bool check_for_configuration(const type target_config_file, const std::string& section, const std::string& key,
        std::string& value, const bool override) {

        std::filesystem::path file_path = BUILD_CONFIG_PATH(target_config_file);
        std::ifstream configFile(file_path, std::ios::in | std::ios::binary);
        VALIDATE(configFile.is_open(), return false, "", "Failed to open file: [{}]", file_path);

        bool found_key = false;
        bool section_found = false;
        std::string line;
        std::ostringstream updatedConfig;
        while (std::getline(configFile, line)) {

            REMOVE_WHITE_SPACE(line);

            // Check if the line contains the desired section
            if (line.find("[" + section + "]") != std::string::npos) {

                section_found = true;
                updatedConfig << line << '\n';

                // Read and update the lines inside the section until a line with '[' is encountered
                while (std::getline(configFile, line) && (line.find('[') == std::string::npos)) {

                    REMOVE_WHITE_SPACE(line);

                    std::size_t found = line.find('=');
                    if (found != std::string::npos) {

                        std::string currentKey = line.substr(0, found);
                        if (currentKey == key) {

                            found_key = true;
                            if (override) {

                                // Update the value for the specified key
                                line.clear();
                                line = key + "=" + value;

                            } else {

                                value.clear();
                                value = line.substr(found + 1);
                            }
                        }
                    }
                    updatedConfig << line << '\n';
                }

                if (!found_key) {

                    updatedConfig << key + "=" + value << '\n';
                    found_key = true;
                }

                if (!line.empty()) {

                    REMOVE_WHITE_SPACE(line);
                    updatedConfig << line << '\n';
                }

            } else {

                if (!line.empty()) {
                    if (line.find("[") == std::string::npos)
                        updatedConfig << line << '\n';
                    else
                        updatedConfig << line << '\n';
                }
            }
        }

        // Close the original file
        configFile.close();

        if (!section_found || found_key || override) {

            // Open the file in truncation mode to clear its content
            std::ofstream outFile(file_path, std::ios::trunc);
            VALIDATE(outFile.is_open(), return false, "", "problems opening file")

            // Write the updated content to the file
            outFile << updatedConfig.str();
            if (!section_found) {

                outFile << "[" << section << "]" << '\n';
                outFile << key << "=" << value << '\n';
            }

            outFile.close();
            // LOG(trace, "File [{}] updated with [{:20} / {:25}]: [{}]", file_path, section, key, value);
        }
        return false; // Key not found
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
