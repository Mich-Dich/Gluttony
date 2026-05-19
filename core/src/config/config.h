#pragma once

#include <type_traits>
#include <glm/glm.hpp>


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::platform {

	struct window_attributes;
}


namespace GLT::config {

	// CONSTANTS =======================================================================================================

	// MACROS ==========================================================================================================

	// TYPES ===========================================================================================================

	// Represents different configuration file types used by the system (underlying type: u8).
	enum class type : u8 {
		ui = 0,			// UI related configuration.
		imgui,			// ImGui specific configuration.
		input,			// Input bindings / input-related configuration.
		app_settings,	// Application-wide settings.
		plugin,
		project,
		launcher,
	};


	// Represents operations that can be performed on configuration files (underlying type: u8).
	enum class operation : u8 {
		save,			// Save/write operation.
		load,			// Load/read operation.
	};

	// STATIC VARIABLES ================================================================================================

	// FUNCTION DECLARATION ============================================================================================

	// @brief Initializes the configuration files by creating necessary directories and default files.
	// @param dir The root directory where configuration files and the CONFIG_DIR will be created.
	// @return void This function does not have a return value.
	void init();

	
	void serialize_window_attributes(GLT::platform::window_attributes& attributes, const serializer::option option);


	// @brief Creates configuration files for a specific project by ensuring the project's CONFIG_DIR exists and creating empty config files.
	// @param project_dir The project directory where project-specific configuration files will be stored.
	// @return void This function does not have a return value.
	void create_config_files_for_project(const std::filesystem::path& project_dir);


	// @brief Converts a configuration type enum value to its string representation.
	// @param type The type enum value to convert.
	// @return std::string The string name corresponding to the type enum (e.g., "ui", "imgui"). Returns "unknown" if the type is not recognized.
    [[nodiscard]] std::string file_type_to_string(const type value);


	// @brief Resolves a configuration file path for a given root directory and configuration file type.
	// @param root The root directory containing the CONFIG_DIR.
	// @param type The configuration file type to resolve.
	// @return std::filesystem::path The full path to the configuration type with the configured extension (e.g., CONFIG_DIR/<type>.config).
	FORCE_INLINE_R std::filesystem::path get_filepath_from_config_type(const type type);


	// @brief Resolves a configuration file path (INI extension) for a given root directory and configuration file type.
	// @param root The root directory containing the CONFIG_DIR.
	// @param type The configuration file type to resolve.
	// @return std::filesystem::path The full path to the configuration INI type (e.g., CONFIG_DIR/<type>.ini).
	FORCE_INLINE_R std::filesystem::path get_filepath_from_config_type_ini(const type type);


	// @brief Checks for the existence of a configuration entry in the specified configuration type. If found and override==true, the existing value is replaced.
	//        If found and override==false the current value is loaded into the provided value reference. If not found, the key/value pair is appended.
	// @param target_config_file The configuration file type to inspect (enum type).
	// @param section The section name in the configuration file where the key/value is expected (e.g., "ui").
	// @param key The key to search for inside the section.
	// @param value Reference to a string that will be updated with the existing value (when override==false) or used to overwrite/append when override==true.
	// @param override If true, existing value is replaced with the provided value; if false, the provided value is overwritten by the existing value if found.
	// @return bool true if the key was found (and possibly updated/appended), false if not found or on failure.
	bool check_for_configuration(const type target_config_file, const std::string& section, const std::string& key, 
		std::string& value, const bool override);

	// TEMPLATE DECLARATION ============================================================================================

	// CLASS DECLARATION ===============================================================================================

}

#include "config.inl"
