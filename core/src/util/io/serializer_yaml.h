#pragma once

#include "serializer_data.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::serializer {

	// CONSTANTS =======================================================================================================

	constexpr u32 NUM_OF_INDENTING_SPACES = 2;

	// MACROS ==========================================================================================================

	// TYPES ===========================================================================================================

	// STATIC VARIABLES ================================================================================================

	// FUNCTION DECLARATION ============================================================================================

	// TEMPLATE DECLARATION ============================================================================================

	// CLASS DECLARATION ===============================================================================================

	class yaml {
	public:

		yaml(const std::filesystem::path filename, const std::string& section_name, option option, bool* success = nullptr);
		
        yaml(std::string* content_buffer, const std::string& section_name, option option, bool* success = nullptr);

		~yaml();

		DELETE_COPY_AND_MOVE_CONSTRUCTOR(yaml);

		DEFAULT_GETTER(option, option);

		// @brief This function adds or looks for a subsection with the specified section name in the YAML file.
		//          If the serializer option is set to [save_to_file], it adds the subsection to the YAML content
		//          and executes the provided function within that subsection. If the serializer option is set to
		//          [load_from_file], it looks for the subsection in the YAML content, deserializes its content,
		//          and executes the provided function within that subsection.
		// @param [section_name] The name of the subsection to be added or looked for.
		// @param [sub_section_function] The function to be executed within the subsection.
		//                               The function should accept a reference to a yaml object as its parameter.
		// @return A reference to the YAML object for chaining function calls.
		yaml& sub_section(const std::string &section_name, std::function<void(serializer::yaml &)> sub_section_function);

		// @brief This function is responsible for serializing or deserializing a single variable
		//          to or from the YAML file based on the specified serialization option. If the
		//          option is set to [save_to_file], it converts the variable to its string
		//          representation and writes it to the YAML file. If the option is set to [load_from_file],
		//			it reads the variable's value from the YAML file and converts it back to
		//          its original type. For vectors, it handles serialization and deserialization
		//          of each element individually.
		// @param [key_name] The key name associated with the variable in the YAML file.
		// @param [value] Reference to the variable to be serialized or deserialized.
		// @return A reference to the YAML object for chaining function calls.
		template <typename T>
		yaml& entry(const std::string &key_name, T &value);


		// @brief This function is responsible for serializing or deserializing a vector variable to or from
		//          the YAML file based on the specified serialization option. If the option is set to save to file,
		//          it serializes each element of the vector individually and writes them to the YAML file. If the option
		//          is set to load from file, it reads each element of the vector from the YAML file and deserializes them
		//          back into the vector. Additionally, it executes a provided function for each element of the vector during
		//          serialization or deserialization.
		// @param [vector_name] The name of the vector variable in the YAML file.
		// @param [vector] Reference to the vector variable to be serialized or deserialized.
		// @param [vector_function] The function to be executed for each element of the vector during serialization
		//                           or deserialization. The function should accept a reference to a yaml object and
		//                           the current iteration index as parameters.
		// @return A reference to the YAML object for chaining function calls.
		template <typename T>
		yaml& vector(const std::string &vector_name, std::vector<T> &vector, std::function<void(serializer::yaml &, const u64 iteration)> vector_function);


		// Serializes or deserializes an unordered_map to/from YAML stored in the class's file stream.
		// When saving, writes a YAML mapping named [map_name] and writes each key/value pair as "key: value"
		// using util::to_string<T>/util::to_string<K>. When loading, finds the section named [map_name]
		// at the current indentation level, reads key/value lines until the section ends, converts each
		// string key and value back into types T and K via util::from_string, and inserts them
		// into [map].
		// @tparam T The unordered_map key type. Must be convertible to/from std::string by util::to_string<T>
		//            and util::from_string.
		// @tparam K The unordered_map mapped value type. Must be convertible to/from std::string by
		//            util::to_string<K> and util::from_string.
		// @param map_name The YAML key/name under which the map is serialized/deserialized.
		// @param map The unordered_map to write to the YAML stream (when saving) or to populate
		//            with parsed values (when loading).
		// @return A reference to this yaml serializer/deserializer to allow chaining.
		template <typename T, typename K>
		yaml& unordered_map(const std::string &map_name, std::unordered_map<T, K> &map);


		// Serializes or deserializes an unordered_set to/from YAML stored in the class's file stream.
		// When saving, writes a YAML sequence named [set_name] and writes each element as "- element".
		// When loading, finds the sequence named [set_name] at the current indentation level, reads each
		// sequence entry (lines beginning with "- "), converts the string representation to type T using
		// util::from_string, and inserts the values into [set].
		// @tparam T The element type stored in the unordered_set. Must be convertible to/from std::string
		//            via util::from_string and util::to_string<T>.
		// @param set_name The YAML key/name under which the set sequence is serialized/deserialized.
		// @param set The unordered_set to write to the YAML stream (when saving) or to populate with parsed
		//            elements (when loading).
		// @return A reference to this yaml serializer/deserializer to allow chaining.
		template <typename T>
		yaml& unordered_set(const std::string &set_name, std::unordered_set<T> &set);

	private:
	
        /**
         * @brief Target medium: file or in‑memory string.
         */
		enum class target{
			file = 0,
			string,
		};


		void serialize();

		yaml& deserialize();

		void extract_key_value(std::string &key, std::string &value, std::string &line);

        bool                                            m_initalized = false;         	// `true` after successful construction.
        static const u32                                num_of_indenting_spaces = 2;  	// Spaces per indentation level (hard‑coded).
        u32                                             m_level_of_indention = 0;     	// Current depth (0 = top‑level section).
        u64                                             vector_func_index = 0;        	// Nesting counter for vector() calls.
        std::string                                     m_prefix{};                   	// Prefix prepended before key names.
        std::string                                     m_prefix_fallback{};          	// Prefix restored after a custom vector element.

        // ---- File / buffer data -----------------------------------------------

        std::filesystem::path                           m_file_path{};              	// Associated file path (file mode).
        std::ofstream                                   m_ostream{};                	// Output stream (unused; kept for symmetry).
        std::ifstream                                   m_istream{};                	// Input stream (file mode).
        std::string*                                    m_content_buffer = nullptr; 	// Pointer to external string buffer (string mode).
        target                                          m_target = target::file;    	// Active target medium.

        // ---- Content data ----------------------------------------------------

        bool                                            m_is_correct_struct = false;	// (Reserved for future use).
        std::string                                     m_name{};                   	// Name of the top‑level section.
        option                                          m_option;                   	// Current I/O mode.
        std::stringstream                               m_file_content{};           	// Accumulated YAML content (text).
        std::unordered_map<std::string, std::string>    m_key_value_pares{};        	// Current section's key‑value pairs.
        bool                                            m_success = true;           	// Set to `false` if a section was not found.

	};

}

#include "serializer_yaml.inl"
