
#pragma once


#include "serializer_data.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::serializer {

	// CONSTANTS =======================================================================================================

    // @brief Number of spaces used per indentation level in YAML output.
    constexpr u32 					NUM_OF_INDENTING_SPACES = 2;

	// MACROS ==========================================================================================================

	// TYPES ===========================================================================================================

	// STATIC VARIABLES ================================================================================================

	// FUNCTION DECLARATION ============================================================================================

	// TEMPLATE DECLARATION ============================================================================================

	// CLASS DECLARATION ===============================================================================================

    // @brief YAML serializer/deserializer for a specific section of a YAML document.
    //        Supports both file‑based and in‑memory string targets.
    class yaml {
    public:

        // @brief Constructs a YAML serializer for a file.
        // @param filename      Path to the YAML file.
        // @param section_name  Name of the top‑level section to read/write.
        // @param option        Load or save mode.
        // @param success       Optional pointer to a bool that receives success status.
        yaml(const std::filesystem::path filename, const std::string& section_name, option option, bool* success = nullptr);
    

        // @brief Constructs a YAML serializer for an in‑memory string buffer.
        // @param content_buffer Pointer to a string that holds the YAML content.
        // @param section_name   Name of the top‑level section to read/write.
        // @param option         Load or save mode.
        // @param success        Optional pointer to a bool that receives success status.
		yaml(std::string* content_buffer, const std::string& section_name, option option, bool* success = nullptr);
    
		~yaml();

        DELETE_COPY_AND_MOVE_CONSTRUCTOR(yaml);
        DEFAULT_GETTER(option, 							option);


        // @brief Enters a subsection (nested YAML mapping) and executes a user function inside it.
        // @param section_name           Name of the subsection.
        // @param sub_section_function   Function that receives a reference to this YAML object.
        // @return Reference to this YAML object for chaining.
        yaml& sub_section(const std::string& section_name, std::function<void(serializer::yaml&)> sub_section_function);


		// @brief Serializes or deserializes a single variable (or a vector) to/from YAML.
		//        For vectors, writes a sequence of "- value" lines or reads them back.
		// @tparam T Type of the variable (must support util::to_string / util::from_string).
		// @param key_name The key under which the value is stored in YAML.
		// @param value    Reference to the variable to read/write.
		// @return Reference to this YAML object for chaining.
        template <typename T>
        yaml& entry(const std::string& key_name, T& value);


		// @brief Serializes or deserializes a std::vector as a YAML sequence.
		//        Each element is handled by the user‑provided function, which can
		//        contain further serialization logic (e.g., subsections, entries).
		// @tparam T Element type of the vector.
		// @param vector_name     Name of the sequence in YAML.
		// @param vector          The vector to read/write.
		// @param vector_function Function called for each element. Receives a yaml reference
		//                        and the current iteration index.
		// @return Reference to this YAML object for chaining.
        template <typename T>
        yaml& vector(const std::string& vector_name, std::vector<T>& vector, std::function<void(serializer::yaml&, const u64 iteration)> vector_function);


		// @brief Serializes or deserializes an unordered_map as a YAML mapping.
		// @tparam T Key type (must be convertible to/from string).
		// @tparam K Value type (must be convertible to/from string).
		// @param map_name Name of the mapping in YAML.
		// @param map      The unordered_map to read/write.
		// @return Reference to this YAML object for chaining.
        template <typename T, typename K>
        yaml& unordered_map(const std::string& map_name, std::unordered_map<T, K>& map);


		// @brief Serializes or deserializes an unordered_set as a YAML sequence.
		// @tparam T Element type (must be convertible to/from string).
		// @param set_name Name of the sequence in YAML.
		// @param set      The unordered_set to read/write.
		// @return Reference to this YAML object for chaining.
        template <typename T>
        yaml& unordered_set(const std::string& set_name, std::unordered_set<T>& set);

    private:

        // @brief Target medium: either a physical file or an in‑memory string.
        enum class target {
			file = 0,
			string
		};


		// @brief Writes the accumulated content (m_file_content) to the target (file or string buffer).
		//        If the target already contains a section with the same name, it replaces that section's content.
		//        Otherwise appends the new section at the end.
        void serialize();


		// @brief Reads the target content (file or string buffer) and populates m_key_value_pares
		//        and m_file_content with the data of the section named m_name.
		// @return Reference to this YAML object for chaining.
        yaml& deserialize();


		// @brief Extracts key and value from a line formatted as "key: value".
		//        Handles leading indentation and removes the colon.
		// @param key   Output parameter for the key string (without trailing colon).
		// @param value Output parameter for the value string (without leading spaces).
		// @param line  Input line to parse (modified by removing leading spaces).
        void extract_key_value(std::string& key, std::string& value, std::string& line);


        bool         									m_initalized = false;       // True after successful construction.
        u32          									m_level_of_indention = 0;   // Current indentation depth.
        u64          									vector_func_index = 0;      // Nesting level for vector() calls.
        std::string  									m_prefix{};                 // Prefix added before keys (e.g., "- " for array elements).
        std::string  									m_prefix_fallback{};        // Saved prefix to restore after a custom vector element.

        // ---- File / buffer data -----------------------------------------------
        std::filesystem::path 							m_file_path{};        		// File path when target is file.
        std::string*          							m_content_buffer = nullptr; // External string buffer when target is string.
        target                							m_target = target::file;    // Active target medium.

        // ---- Content data ----------------------------------------------------
        std::string                                     m_name{};            		// Name of the top‑level section.
        option                                          m_option;            		// Load or save mode.
        std::stringstream                               m_file_content{};    		// Accumulated YAML content.
        std::unordered_map<std::string, std::string>    m_key_value_pares{}; 		// Key‑value pairs of the current section.
        bool                                            m_success = true;    		// False if a required section was not found.
    };
	
}

#include "serializer_yaml.inl"
