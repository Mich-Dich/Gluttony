
#include "util/pch.h"
#include "serializer_yaml.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::serializer {

	// CONSTANTS =======================================================================================================

	// MACROS ==========================================================================================================

	// local logging override (only needed when debugging)
	#if 0

		#define LLOG(severity, message, ...)									LOG(severity, message __VA_OPT__(,) __VA_ARGS__)
		#define LVALIDATE(expr, command, messageSuccess, messageFailure, ...)	VALIDATE(expr, command, messageSuccess, messageFailure __VA_OPT__(,) __VA_ARGS__)
		
	#else

		#define LLOG(severity, message, ...)
		#define LVALIDATE(expr, command, messageSuccess, messageFailure, ...)	if (!(expr)) { command; }

	#endif

	// TYPES ===========================================================================================================

	// STATIC VARIABLES ================================================================================================

	// INTERNAL FUNCTION DECLARATION ===================================================================================

	// INTERNAL FUNCTION IMPLEMENTATION ================================================================================

	// FUNCTION IMPLEMENTATION =========================================================================================

	// CLASS IMPLEMENTATION ============================================================================================

	yaml::yaml(const std::filesystem::path& file_path, const std::string& section_name, const option option, bool* success)
		: m_file_path(file_path), m_name(section_name), m_option(option) {

		m_target = target::file;
        std::error_code error{};
		if (option == option::load) {			// check if file can be loaded
			VALIDATE(GLT::vfs::exists(m_file_path, error), /* DEBUG_BREAK(); */ if (success) { *success = false; } return,
				"", "Can not load from provided file [{}], it does not exist", m_file_path.generic_string())

            error.clear();
			VALIDATE(GLT::vfs::is_regular_file(m_file_path, error), if (success) { *success = false; } return,
				"", "Provided filepath is not a file [{}]", m_file_path.generic_string());

		} else {								// saving to file

			// make sure the file exists
			std::filesystem::path path = file_path.parent_path();
            error.clear();
            GLT::vfs::create_directories(path, error);
			VALIDATE(!error, if (success) { *success = false; } return, "",
                "Failed to create directories for [{}] [{}]", path.generic_string(), error.message());

            error.clear();
            GLT::vfs::create_file(m_file_path, error);
            VALIDATE(!error, if (success) { *success = false; } return, "",
                "Failed to create file for [{}] [{}]", path.generic_string(), error.message());
		}

		m_initalized = true;							// From here the serializer assumes that the file setup is dealt with

		if (m_option == option::load)
			deserialize();

		else {

			m_file_content << section_name << ":\n";
			m_level_of_indention = 1;
		}

		if (success)								
			*success = m_success;
	}


	yaml::yaml(std::string* content_buffer, const std::string &section_name, const option option, bool* success)
		: m_content_buffer(content_buffer), m_name(section_name), m_option(option) {

		m_target = target::string;
		if (option == option::load) {			// check if file can be loaded

			VALIDATE(!m_content_buffer->empty(), if (success) { *success = false; } return, "", "Provided [m_content_buffer] is empty, can load from empty string")
		}

		m_initalized = true;							// From here the serializer assumes that the file setup is dealt with

		if (m_option == option::load)
			deserialize();

		else {

			m_file_content << section_name << ":\n";
			m_level_of_indention = 1;
		}
		if (success)
			*success = true;
	}


	yaml::~yaml() {

		if (m_option == option::save)
			serialize();
	}

	// CLASS PUBLIC ====================================================================================================

	yaml& yaml::sub_section(const std::string& section_name, std::function<void(serializer::yaml&)> sub_section_function) {

		if (!m_initalized)				return *this;

		m_level_of_indention++;
		if (m_option == serializer::option::save) {

			m_file_content << GLT::util::add_spaces(m_level_of_indention + static_cast<u32>(vector_func_index -1), NUM_OF_INDENTING_SPACES) << section_name << ":\n";
			sub_section_function(*this);

		} else {                // load from file

			// buffer [m_key_value_pares] for duration of function
			std::unordered_map<std::string, std::string> key_value_pares_buffer = m_key_value_pares;
			m_key_value_pares = {};

			// buffer [m_file_content] for duration of function
			std::stringstream file_content_buffer{};
			file_content_buffer << m_file_content.str();
			m_file_content = {};

			// deserialize content of subsections
			// const u32 section_indentation = 0;
			bool foundSection = false;
			std::string line;
			//m_level_of_indention++;
			while (std::getline(file_content_buffer, line)) {

                // Apply cleaning; skip if line becomes empty
                if (!clean_line(line))			
					continue;

				// if line contains desired section enter inner-loop
				//   has incorrect indentaion											ends NOT with double-point
				if ((util::measure_indentation(line, NUM_OF_INDENTING_SPACES) != 0) || (line.back() != ':'))
                    continue;

				LLOG(debug, "line: [{}]", line)

				// remove leading and trailing whitespace
				auto trimmed = line;
				trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char ch) { return !std::isspace(ch); }));
				trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), trimmed.end());

				// Remove the trailing colon
				if (!trimmed.empty() && trimmed.back() == ':')
                    trimmed.pop_back();

				if (trimmed != section_name)     	// has incorrect section_name
                    continue;

				foundSection = true;
				LLOG(debug, "Found Section [{}]", section_name)

				while (std::getline(file_content_buffer, line)) {

                    // Apply cleaning; skip if line becomes empty
                    if (!clean_line(line))
                        continue;

					const auto line_indent = util::measure_indentation(line, NUM_OF_INDENTING_SPACES);
					const bool exit_loop = line_indent <= 0;
					LLOG(trace, "Next Line [{}] - relative indentation: [{}] exit inner loop: [{}]", 
						line, line_indent, GLT::util::to_string(exit_loop))

					if (exit_loop)	// exit inner loop after section is finished
                        break;

					line = line.substr(NUM_OF_INDENTING_SPACES);

					//  more indented																		beginning of new sub-section
					if (util::measure_indentation(line, NUM_OF_INDENTING_SPACES) > m_level_of_indention -1 || line.back() == ':' || line.front() == '-') {

						m_file_content << line << "\n";
						continue;
					}

					std::string key, value;
					extract_key_value(key, value, line);
					m_key_value_pares[key] = value;
				}
				LLOG(debug, "Finished Section")

				if (foundSection)
					break;
			}

			LVALIDATE(foundSection, , "Found subsection [{1}] num of loaded pairs [{2}]", "Could NOT find subsection [{3}]",
				section_name, m_key_value_pares.size(), section_name)

			if (foundSection)
				sub_section_function(*this);

			m_key_value_pares = key_value_pares_buffer;
			m_file_content << file_content_buffer.str();
		}

		m_level_of_indention--;
		return *this;
	}

	// CLASS PROTECTED =================================================================================================

	// CLASS PRIVATE ===================================================================================================

	void yaml::serialize() {

        if (!m_initalized)
			return;

		// Lambda that replaces the top‑level section named m_name in 'input'
		// with the content of m_file_content. Returns the resulting string.
		auto replaceSectionInContent = [&](const std::string& input) -> std::string {

			std::istringstream istream(input);
			std::ostringstream output;
			bool found = false;
			std::string line;

			while (std::getline(istream, line))
            {
                // Apply cleaning; skip if line becomes empty
                if (!clean_line(line))
                    continue;

				if (!found &&
					line.find(m_name + ":") != std::string::npos &&
					util::measure_indentation(line, NUM_OF_INDENTING_SPACES) == 0) {

					found = true;
					output << m_file_content.str();          // Write new section content

					// Skip the old content of this section
					while (std::getline(istream, line)) {

                        // Apply cleaning; skip if line becomes empty
                        if (!clean_line(line))
                            continue;

						if (line.back() == ':' && util::measure_indentation(line, NUM_OF_INDENTING_SPACES) == 0) {

							output << line << '\n';
							break;
						}
					}
				}
                else
                    output << line << '\n';
			}

			// If section not found, append new content at the end
			if (!found)
                output << m_file_content.str();

			return output.str();
		};

		if (m_target == target::file) {

            std::error_code error{};
			std::string existing_content = GLT::vfs::read_text_file(m_file_path, error);     // Read existing file content

            if (error) {

                m_success = false;
                return;
            }
			std::string updated_content = replaceSectionInContent(existing_content);

			if (!GLT::vfs::write_text_file(m_file_path, updated_content))
				m_success = false;   												// Optionally signal failure

		} else {     															   	// target::string

			if (!m_content_buffer)
                return;

			if (m_content_buffer->empty())
                *m_content_buffer = m_file_content.str();
			else
                *m_content_buffer = replaceSectionInContent(*m_content_buffer);
		}
    }


	yaml& yaml::deserialize() {

		if (!m_initalized || m_name.empty())
            return *this;

		// Lambda that parses YAML content from a string, fills m_file_content and
		// m_key_value_pares with the data of the section named m_name.
		// Returns true if the section was found, false otherwise.
		auto parse_content = [this](const std::string& content) -> bool {

			std::istringstream stream(content);
			const u32 SECTION_INDENTATION = 0;
			bool found_section = false;
			std::string line;

			while (std::getline(stream, line)) {

                // Apply cleaning; skip if line becomes empty
                if (!clean_line(line))
                    continue;

				if (line.find(m_name + ":") != std::string::npos &&
					util::measure_indentation(line, NUM_OF_INDENTING_SPACES) == 0) {

					found_section = true;
					while (std::getline(stream, line)) {

						if (util::measure_indentation(line, NUM_OF_INDENTING_SPACES) <= SECTION_INDENTATION 
							&& !line.empty())		// line holds data but indentation says the section is over
							break;

						if (!clean_line(line))		// Apply cleaning; skip if line becomes empty (also catches empty lines)
                            continue;

						line = line.substr(NUM_OF_INDENTING_SPACES);
						// More indented or a sub-section or array element
						if (util::measure_indentation(line, NUM_OF_INDENTING_SPACES) > SECTION_INDENTATION 
							|| line.back() == ':' 
							|| line.front() == '-') {

							m_file_content << line << '\n';
							continue;
						}

						std::string key, value;
						extract_key_value(key, value, line);
						m_key_value_pares[key] = value;
					}
					break;
				}
			}
			return found_section;
		};

		if (m_target == target::file) {

            std::error_code error{};
			std::string file_content = vfs::read_text_file(m_file_path, error);
			if (file_content.empty()) {

                if (error)
                    m_success = false;

				return *this;
			}
			if (!parse_content(file_content))
                m_success = false;

		}
        else     								// target::string
        {

			if (!m_content_buffer || m_content_buffer->empty()) {

				m_success = false;
				return *this;
			}

			if (!parse_content(*m_content_buffer))
                m_success = false;
		}

		return *this;
	}


	void yaml::extract_key_value(std::string& key, std::string& value, std::string& line) {

		std::istringstream iss(line);
		std::getline(iss, key, ':');
		std::getline(iss, value);

		if (const u32 indentation = util::measure_indentation(key, 1); indentation > 0)
            key = key.substr(indentation);

		if (!value.empty() && value.front() == ' ')
            value.erase(0, 1);
	}


    bool yaml::clean_line(std::string& line) {

        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());    // Remove carriage return characters

        size_t commentPos = line.find('#');                 // Strip YAML comment (everything after first '#')
        if (commentPos != std::string::npos)
            line = line.substr(0, commentPos);

        line.erase(std::find_if(line.rbegin(), line.rend(), // Trim trailing whitespace (keep leading spaces for indentation)
            [](unsigned char ch) { return !std::isspace(ch); }).base(),
            line.end());

        return !line.empty();                               // Return false if the line is now empty
    };

}
