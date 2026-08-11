
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
        yaml(const std::filesystem::path& filename, const std::string& section_name, const option option, bool* success = nullptr);
    

        // @brief Constructs a YAML serializer for an in‑memory string buffer.
        // @param content_buffer Pointer to a string that holds the YAML content.
        // @param section_name   Name of the top‑level section to read/write.
        // @param option         Load or save mode.
        // @param success        Optional pointer to a bool that receives success status.
		yaml(std::string* content_buffer, const std::string& section_name, const option option, bool* success = nullptr);
    
		~yaml();

        DELETE_COPY_AND_MOVE_CONSTRUCTOR(yaml);
        DEFAULT_GETTER(option, 							option);


        // @brief Opens or searches for a named subsection within the YAML
        //        structure.
        //
        // When **saving**, the subsection name is emitted at the current
        // indentation level, indentation is increased, the provided function
        // is called (allowing nested entries), and then indentation is
        // restored.
        //
        // When **loading**, the file/buffer content is searched for a section
        // with the given name and correct indentation. If found, its key‑value
        // pairs are made available to the provided callback. Sub‑sections
        // inside the searched section are forwarded as text so they can be
        // parsed recursively.
        //
        // @param section_name          The name of the subsection (without colon).
        // @param sub_section_function  Callback that receives a reference to this
        //                              yaml object for adding/reading entries.
        // @return A reference to `*this` for chaining.
        yaml& sub_section(const std::string& section_name, std::function<void(serializer::yaml&)> sub_section_function);


        // @brief Serializes or deserializes a single value as a YAML key‑value
        //        pair.
        //
        // When **saving**:
        // - If `T` is a `std::vector`, a scalar sequence is written (each element
        //   on a new line prefixed with `- `).
        // - Otherwise, the value is converted to a string via
        //   `util::convertToString<T>()` and written as `keyName: value`.
        //
        // When **loading**:
        // - For `std::vector`: the sequence matching `keyName` is parsed and each
        //   element is converted back via `util::convertFromString`.
        // - Otherwise, the value is looked up in the current section’s key‑value
        //   map and converted back.
        //
        // @tparam T        Type of the value. Must be supported by
        //                  `util::convertToString` / `util::convertFromString`.
        // @param key_name  The YAML key under which the value is stored.
        // @param value     The value to write (save) or to receive (load).
        // @return A reference to `*this` for chaining.
        template <typename T>
        yaml& entry(const std::string& key_name, T& value);


        // @brief Serializes or deserializes a vector with a custom callback for
        //        each element.
        //
        // Allows complex element types that cannot be captured by a simple
        // key‑value pair (e.g., nested structures). The callback receives a
        // `yaml&` reference that can be used to add/read multiple key‑value
        // pairs per element.
        //
        // When **saving**, the vector name is written as a section, each element
        // is preceded by `- `, and the indentation is adjusted so that the
        // callback’s output appears inside the element.
        //
        // When **loading**, the vector section is located, each `- ` entry
        // defines a new element. The callback is called once per element with a
        // fresh set of key‑value pairs.
        //
        // @tparam T                Vector element type.
        // @param vector_name       The YAML key under which the vector is stored.
        // @param vector            The vector to write (save) or to receive (load).
        // @param vector_function   Callback invoked for each element. Signature:
        //                          `void(serializer::yaml&, u64 iteration)`.
        // @return A reference to   `*this` for chaining.
        template <typename T>
        yaml& vector(const std::string& vector_name, std::vector<T>& vector, std::function<void(serializer::yaml&, const u64 iteration)> vector_function);


        // @brief Serializes or deserializes an `std::unordered_map<T, K>`.
        //
        // The map is stored as a YAML mapping: each key‑value pair becomes a
        // line `key: value` indented one level deeper than the map name.
        // `util::toString` / `util::convertFromString` are used for the key and
        // value types.
        //
        // @tparam T        Key type.
        // @tparam K        Mapped value type.
        // @param map_name  The YAML key for the map section.
        // @param map       The map to write (save) or to receive (load).
        // @return A reference to `*this` for chaining.
        template <typename T, typename K>
        yaml& unordered_map(const std::string& map_name, std::unordered_map<T, K>& map);


        template<typename Key, typename Item>
		yaml& unordered_map(const std::string& map_name, std::unordered_map<Key, Item>& map, std::function<void(serializer::yaml&, Item& mapItem)> map_function);


        // @brief Serializes or deserializes an `std::unordered_set<T>`.
        //
        // The set is stored as a YAML sequence. Each element appears on a
        // new line starting with `- `.
        //
        // @tparam T        Element type.
        // @param set_name  The YAML key for the set sequence.
        // @param set       The set to write (save) or to receive (load).
        // @return A reference to `*this` for chaining.
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


        bool clean_line(std::string& line);


        bool         									m_initalized = false;       // True after successful construction
        u32          									m_level_of_indention = 0;   // Current indentation depth
        u64          									vector_func_index = 0;      // Nesting level for vector() calls
        std::string  									m_prefix{};                 // Prefix added before keys (e.g., "- " for array elements)
        std::string  									m_prefix_fallback{};        // Saved prefix to restore after a custom vector element

        // ---- File / buffer data -----------------------------------------------
        std::filesystem::path                           m_file_path{};                // Associated file path (file mode)
        std::ofstream                                   m_ostream{};                 // Output stream (unused; kept for symmetry)
        std::ifstream                                   m_istream{};                 // Input stream (file mode)
        std::string*                                    m_content_buffer = nullptr;   // Pointer to external string buffer (string mode)
        target                                          m_target = target::file;     // Active target medium

        // ---- Content data ----------------------------------------------------
        bool                                            m_is_correct_struct = false;   // (Reserved for future use)
        std::string                                     m_name{};                    // Name of the top‑level section
        option                                          m_option;                    // Current I/O mode
        std::stringstream                               m_file_content{};             // Accumulated YAML content (text)
        std::unordered_map<std::string, std::string>    m_key_value_pares{};           // Current section's key‑value pairs
        bool                                            m_success = true;            // Set to `false` if a section was not found
    };
	
}

#include "serializer_yaml.inl"
