
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

	// FUNCTION IMPLEMENTATION =========================================================================================

	// CLASS IMPLEMENTATION ============================================================================================

	yaml::yaml(const std::filesystem::path file_path, const std::string& section_name, option option, bool* success)
		: m_file_path(file_path), m_name(section_name), m_option(option) {

		m_target = target::file;
		if (option == option::load_from_file) {			// check if file can be loaded

			VALIDATE(std::filesystem::exists(m_file_path), if (success) { *success = false; } return,
				"", "Can not load from provided file [{}], it does not exist", m_file_path.string())

			VALIDATE(std::filesystem::is_regular_file(m_file_path), if (success) { *success = false; } return,
				"", "Provided filepath is not a file [{}]", m_file_path.string());

		} else {										// saving to file

			// make sure the file exists
			std::filesystem::path path = file_path.parent_path();
			VALIDATE(vfs::create_directory(path), if (success) { *success = false; } return, "", "Could not create file-path");
			if (!std::filesystem::exists(m_file_path)) {

				auto file = std::ofstream(m_file_path);
				file.close();
			}
		}

		m_initalized = true;							// From here the serializer assumes that the file setup is dealt with

		if (m_option == option::load_from_file)
			deserialize();

		else {

			m_file_content << section_name << ":\n";
			m_level_of_indention = 1;
		}

		if (success) { *success = m_success; }
	}


	yaml::yaml(std::string* content_buffer, const std::string &section_name, option option, bool *success)
		: m_content_buffer(content_buffer), m_name(section_name), m_option(option) {

		m_target = target::string;
		if (option == option::load_from_file) {			// check if file can be loaded
			VALIDATE(!m_content_buffer->empty(), if (success) { *success = false; } return, 
				"", "Provided [m_content_buffer] is empty, can load from empty string")
		}

		m_initalized = true;							// From here the serializer assumes that the file setup is dealt with

		if (m_option == option::load_from_file) {

			deserialize();

		} else {

			m_file_content << section_name << ":\n";
			m_level_of_indention = 1;
		}
		if (success) {

			*success = true; 
		}
	}


	yaml::~yaml()
    {

		if (m_option == option::save_to_file)
			serialize();
	}

	// CLASS PUBLIC ====================================================================================================

	yaml& yaml::sub_section(const std::string& section_name, std::function<void(serializer::yaml&)> sub_section_function) {

		if (!m_initalized)
            return *this;

		m_level_of_indention++;
		if (m_option == serializer::option::save_to_file) {

			m_file_content << util::add_spaces(m_level_of_indention + static_cast<u32>(vector_func_index -1), NUM_OF_INDENTING_SPACES) << section_name << ":\n";
			sub_section_function(*this);

		} else {               // load from file

			// buffer [m_key_value_pares] for duration of function
			std::unordered_map<std::string, std::string> key_value_pares_buffer = m_key_value_pares;
			m_key_value_pares = {};

			// buffer [m_file_content] for duration of function
			std::stringstream file_content_buffer;
			file_content_buffer << m_file_content.str();
			m_file_content = {};

			// deserialize content of subsections
			// const u32 section_indentation = 0;
			bool found_section = false;
			std::string line;
			//m_level_of_indention++;
			while (std::getline(file_content_buffer, line)) {

				// skip empty lines or comments
				if (line.empty() || line.front() == '#')
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

				found_section = true;
				LLOG(debug, "Found Section [{}]", section_name)

				while (std::getline(file_content_buffer, line)) {

					const auto line_indent = util::measure_indentation(line, NUM_OF_INDENTING_SPACES);
					const bool exit_loop = line_indent <= 0;
					LLOG(trace, "Next Line [" << line << "] [relative indentation: " << line_indent <<
						", exit inner loop: " << util::to_string(exit_loop) << "]")
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

				if (found_section)
					break;
			}

			LVALIDATE(found_section, , "Found subsection [{}] num of loaded pairs [{}]", "Could NOT find subsection [{}]", section_name, m_key_value_pares.size())
			if (found_section)
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
		{
            return;
        }

		if (m_target == target::file)
        {
			// ============ FILE TARGET ============
			auto istream = std::ifstream(m_file_path);
			VALIDATE(istream.is_open(), return, "", "input-file-stream is not open");

			// make new stream to buffer updated file
			std::ostringstream updatedFile;

			// copy content of file that is not the focus of this serialization
			bool found = false;				// ensure only one section can be skipped
			std::string line = "";
			while (std::getline(istream, line))
            {

				// is correct section
				if (!found && (line.find(m_name + ":") != std::string::npos) && (util::measure_indentation(line, NUM_OF_INDENTING_SPACES) == 0))
                {
					found = true;
					updatedFile << m_file_content.str();          // override section with new content

					while (std::getline(istream, line))         // SKIP CONTENT
                    {
						if (line.back() == ':' && util::measure_indentation(line, NUM_OF_INDENTING_SPACES) == 0)         // still in section ??
                        {
							updatedFile << line + "\n";
							break;
						}
					}
				}
				else
					updatedFile << line + "\n";
			}

			// append if section not found
			if (!found)
			{
                updatedFile << m_file_content.str();
            }

			istream.close();
			auto ostream = std::ofstream(m_file_path);
			ASSERT(ostream.is_open(), "", "output-file-stream is not open");
			ostream << updatedFile.str();
			ostream.close();

		}
        else
        {
			// ============ STRING TARGET ============
			ASSERT(m_content_buffer != nullptr, "", "contentBuffer is null for string target");

			if (m_content_buffer->empty())
            {
				*m_content_buffer = m_file_content.str();       // If string is empty, just use our content
			}
            else
            {
				// Replace section in existing string content (similar to file logic)
				std::istringstream istream(*m_content_buffer);
				std::ostringstream updated_content;

				bool found = false;
				std::string line = "";

				while (std::getline(istream, line))
                {
					// is correct section
					if (!found && (line.find(m_name + ":") != std::string::npos) && (util::measure_indentation(line, NUM_OF_INDENTING_SPACES) == 0))
                    {
						found = true;
						updated_content << m_file_content.str();          // override section with new content
						while (std::getline(istream, line))             // SKIP CONTENT
                        {
							if (line.back() == ':' && util::measure_indentation(line, NUM_OF_INDENTING_SPACES) == 0)
                            {
								updated_content << line + "\n";
								break;
							}
						}
					}
                    else
                    {
						updated_content << line + "\n";
					}
				}

				// append if section not found
				if (!found)
                {
					updated_content << m_file_content.str();
                }

				*m_content_buffer = updated_content.str();
			}
		}
	}


	yaml& yaml::deserialize() {

		if (!m_initalized)
        {
            return *this;
        }

		ASSERT(!m_name.empty(), "", "name of section to find is empty");

		if (m_target == target::file)
        {
			// ============ FILE TARGET ============
			m_istream = std::ifstream(m_file_path);
			LVALIDATE(m_istream.is_open(), return *this, "", "file-stream is not open");

			const u32 SECTION_INDENTATION = 0;
			bool found_section = false;
			std::string line;
			while (std::getline(m_istream, line))
            {
				// skip empty lines or comments
				if (line.empty() || line.front() == '#')
                    continue;

				// if line contains desired section enter inner-loop
				if (line.find(m_name + ":") != std::string::npos && util::measure_indentation(line, NUM_OF_INDENTING_SPACES) == 0) {
					found_section = true;
					//     not end of file                   line has more leading spaces
					while (std::getline(m_istream, line) && (util::measure_indentation(line, NUM_OF_INDENTING_SPACES) > SECTION_INDENTATION)) {

						line = line.substr(NUM_OF_INDENTING_SPACES);
						//  more indented                                         is sub-section        is array-element
						if ((util::measure_indentation(line, NUM_OF_INDENTING_SPACES) > SECTION_INDENTATION) || line.back() == ':' || line.front() == '-') {

							m_file_content << line << '\n';
							continue;
						}

						std::string key, value;
						extract_key_value(key, value, line);
						m_key_value_pares[key] = value;
					}
				}

				// exit outer loop if inner-loop already done
				if (found_section)
                    break;
			}

            // if section not found, set success to false
            if (!found_section)
            {
                m_success = false;
            }

		} else {

			// ============ STRING TARGET ============
			ASSERT(m_content_buffer != nullptr, "", "contentBuffer is null for string target");
			ASSERT(!m_content_buffer->empty(), "", "contentBuffer is empty for string target");

			std::istringstream stringStream(*m_content_buffer);
			const u32 SECTION_INDENTATION = 0;
			bool found_section = false;
			std::string line;
			while (std::getline(stringStream, line)) {

				// skip empty lines or comments
				if (line.empty() || line.front() == '#')
                    continue;

				// if line contains desired section enter inner-loop
				if (line.find(m_name + ":") != std::string::npos && util::measure_indentation(line, NUM_OF_INDENTING_SPACES) == 0) {

					found_section = true;
					//     not end of string content          line has more leading spaces
					while (std::getline(stringStream, line) && (util::measure_indentation(line, NUM_OF_INDENTING_SPACES) > SECTION_INDENTATION)) {

						line = line.substr(NUM_OF_INDENTING_SPACES);
						//  more indented                                         is sub-section        is array-element
						if ((util::measure_indentation(line, NUM_OF_INDENTING_SPACES) > SECTION_INDENTATION) || line.back() == ':' || line.front() == '-') {

							m_file_content << line << '\n';
							continue;
						}

						std::string key, value;
						extract_key_value(key, value, line);
						m_key_value_pares[key] = value;
					}
				}

				// exit outer loop if inner-loop already done
				if (found_section)
					break;

			}
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

}
