#pragma once

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::serializer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    #define VALIDATE_INIT()		do { if (!m_initalized) return *this; } while (0);

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================
		
    template<typename T>
    yaml& yaml::entry(const std::string& key_name, T& value) {

        VALIDATE_INIT();

        if (m_option == serializer::option::save_to_file) {

            std::string buffer{};
            if constexpr (is_vector<T>::value) {                    // value is a vector
            
                m_file_content << util::add_spaces(m_level_of_indention) << m_prefix << key_name << ":\n";
                for (auto interation : value) {

                    util::to_string<typename T::value_type>(interation, buffer);
                    m_file_content << util::add_spaces(m_level_of_indention + 1) << "- " << buffer << "\n";
                }
            
            } else {

                util::to_string<T>(value, buffer);
                m_file_content << util::add_spaces(m_level_of_indention) << m_prefix << key_name << ": " << buffer << "\n";
            }
        
        } else {                           				            // load from file
        
            if constexpr (is_vector<T>::value) {					    // value is a vector
            
                // deserialize content of subsections
                typename T::value_type buffer{};
                bool found_section = false;
                std::string line;
                while (std::getline(m_file_content, line)) {

                    if (line.empty() || line.front() == '#')		// skip empty lines or comments
                        continue;

                    // if line contains desired section enter inner-loop
                    //   has correct indentation                                 has correct sectionName                      ends with double-point
                    if ((util::measure_indentation(line) == 0) && (line.find(key_name) != std::string::npos) && (line.back() == ':')) {

                        found_section = true;
                        value.clear();								// clear previous data when section found
                        //     not end of content                     has correct indentation	         		doesn't end in double-points
                        while (std::getline(m_file_content, line) && (util::measure_indentation(line) == 0) && (line.back() != ':')) {

                            // 				   remove indentation		 remove "- " (array element marker)
                            line = line.substr(NUM_OF_INDENTING_SPACES);
                            util::from_string(line, buffer);
                            value.emplace_back(buffer);
                        }
                    }

                    if (found_section)								// skip rest of content if section found
                        break;
                }

            } else {

                std::string buffer{};
                auto iterator = m_key_value_pares.find(key_name);
                if (iterator == m_key_value_pares.end())			// key is not in map
                    return *this;
                
                buffer = iterator->second;
                util::from_string(buffer, value);
            }
        }

        m_prefix = m_prefix_fallback;
        return *this;
    }


    template<typename T>
    yaml& yaml::vector(const std::string& vector_name, std::vector<T>& vector, std::function<void(serializer::yaml&, const u64 iteration)> vector_function) {

        VALIDATE_INIT();
        vector_func_index++;

        if (vector_func_index != 1)
            m_level_of_indention++;

        if (m_option == serializer::option::save_to_file) {           // save to file
        
            const u32 indent_buffer = vector_func_index != 1 ? m_level_of_indention - 1 : m_level_of_indention;
            m_file_content << util::add_spaces(indent_buffer) << m_prefix << vector_name << ":\n";
            for (u64 x = 0; x < vector.size(); x++) {

                // start of array element
                m_prefix = "- ";
                m_prefix_fallback = "  ";
                vector_function(*this, x);
            }
            m_prefix = "";
            m_prefix_fallback = "";

        } else {                                   		            // load from file
        
            // buffer [m_key_value_pares] for duration of function
            std::unordered_map<std::string, std::string> key_value_pares_buffer = m_key_value_pares;
            std::vector<std::unordered_map<std::string, std::string>> vector_of_key_value_pares{};
            m_key_value_pares = {};

            // buffer [m_file_content] for duration of function
            std::stringstream file_content_buffer;
            std::vector<std::stringstream> vector_of_file_content{};		// for array element in file
            file_content_buffer << m_file_content.str();
            m_file_content = {};

            // deserialize content of subsections
            i64 index = -1;
            std::string line;
            while (std::getline(file_content_buffer, line)) {

                // skip empty lines or comments
                if (line.empty() || line.front() == '#')
                    continue;

                // if line contains desired section enter inner-loop
                if ((util::measure_indentation(line, NUM_OF_INDENTING_SPACES) == 0)		// has correct indentation
                    && (line.find(vector_name) != std::string::npos)					// has correct vector_name
                    && (line.back() == ':')) {											// ends with double-point
                
                    //     not end of content
                    while (std::getline(file_content_buffer, line)) {

                        if (line.front() == '-') {

                            vector_of_key_value_pares.push_back({});
                            vector_of_file_content.push_back({});
                            index++;
                        }

                        if (line.back() == ':' && util::measure_indentation(line, NUM_OF_INDENTING_SPACES) == 0)
                            break;		// end of vector content

                        line = line.substr(NUM_OF_INDENTING_SPACES);                // remove array-prefix "- " or "  "

                        //  more indented                                        beginning of new sub-section
                        if (util::measure_indentation(line, NUM_OF_INDENTING_SPACES) != 0 || line.back() == ':' || line.front() == '-') {

                            //m_file_content << line << "\n";
                            vector_of_file_content[index] << line << "\n";
                            continue;
                        }

                        std::string key, value;
                        extract_key_value(key, value, line);
                        vector_of_key_value_pares[index][key] = value;
                    }
                }
            }

            ASSERT(vector_of_key_value_pares.size() == vector_of_file_content.size(), "", "two buffers are of different size");

            if (vector_of_key_value_pares.size() > 0) {

                vector.resize(vector_of_key_value_pares.size());
                for (u64 x = 0; x < vector.size(); x++) {

                    m_key_value_pares = vector_of_key_value_pares[x];
                    m_file_content = {};
                    auto temp_buffer = vector_of_file_content[x].str();
                    m_file_content << temp_buffer;
                    vector_function(*this, x);
                }
            }

            // restore
            m_key_value_pares = key_value_pares_buffer;
            auto temp_buffer = file_content_buffer.str();
            m_file_content << temp_buffer;
        }

        if (vector_func_index != 1)
            m_level_of_indention--;

        vector_func_index--;
        return *this;
    }


    template<typename T, typename K>
    yaml& yaml::unordered_map(const std::string& map_name, std::unordered_map<T, K>& map) {

        VALIDATE_INIT();

        if (m_option == serializer::option::save_to_file) {										// Serialize the map

            m_file_content << util::add_spaces(m_level_of_indention) << map_name << ":\n";
            for (const auto& [key, value] : map)
                m_file_content << util::add_spaces(m_level_of_indention + 1) << util::to_string<T>(key) << ": " << util::to_string<K>(value) << "\n";
            
        } else {																				// Deserialize the map
    
            // Deserialize map from YAML
            std::unordered_map<std::string, std::string> temp_map;
            std::string line;

            // Read until we find the map section
            while (std::getline(m_file_content, line)) {

                if (line.find(map_name + ":") != std::string::npos && util::measure_indentation(line) == m_level_of_indention)
                    break;
            }

            while (std::getline(m_file_content, line)) {   							// Read key-value pairs
            
                if (util::measure_indentation(line) <= m_level_of_indention)		    // End of map section
                    break;

                std::string key, value;
                extract_key_value(key, value, line);
                temp_map.emplace(std::move(key), std::move(value));
            }

            // Convert strings to actual types
            for (const auto& [key_str, value_str] : temp_map) {

                T key;
                K value;
                util::from_string(key_str, key);
                util::from_string(value_str, value);
                map.emplace(std::move(key), std::move(value));
            }
        }
        return *this;
    }


    template<typename T>
    yaml& yaml::unordered_set(const std::string& set_name, std::unordered_set<T>& set) {

        if (m_option == option::save_to_file) {

            // Serialize the set as a YAML sequence
            m_file_content << util::add_spaces(m_level_of_indention) << set_name << ":\n";
            for (const auto& element : set) {

                std::string buffer;
                util::to_string<T>(element, buffer);
                m_file_content << util::add_spaces(m_level_of_indention) << "- " << buffer << "\n";
            }

        } else {																	// Deserialize the set from YAML

            std::unordered_set<T> temp_set;
            std::string line;
            while (std::getline(m_file_content, line)) {								// Read until we find the set section
            
                if (line.find(set_name + ":") != std::string::npos && util::measure_indentation(line) == 0)
                    break;
            }

            while (std::getline(m_file_content, line)) {							    // Read sequence elements
                if (util::measure_indentation(line) < 0 || line.back() == ':') 		// End of set section
                    break;

                if (line.find("- ") == std::string::npos) 							// Extract element value
                    continue;

                size_t dash_pos = line.find("- ");
                std::string elementStr = line.substr(dash_pos + 2);
                T element;
                util::from_string(elementStr, element);
                temp_set.insert(element);
            }
            set = std::move(temp_set);
        }
        return *this;
    }

    #undef VALIDATE_INIT

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}