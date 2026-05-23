#pragma once

#include "serializer_data.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::serializer {

	// CONSTANTS =======================================================================================================

	// MACROS ==========================================================================================================

	// TYPES ===========================================================================================================

	// STATIC VARIABLES ================================================================================================

	// FUNCTION DECLARATION ============================================================================================

	// TEMPLATE DECLARATION ============================================================================================

	// CLASS DECLARATION ===============================================================================================

	// @brief A binary serializer/deserializer class for reading and writing data in binary format.
	//        Supports saving data to files and loading data from files with type-specific serialization.
	//        Provides templated methods for serializing various data types including primitives, strings, and containers.
	class binary {
	public:

		DELETE_COPY_AND_MOVE_CONSTRUCTOR(binary);

		DEFAULT_GETTER(option, option);		// Getter for the current operation mode (save or load)

		// @brief Constructs a binary serializer with the specified file, section name, and operation mode.
		//        Opens the file for reading or writing based on the specified option.
		// @param [filename] The path to the file to serialize to or deserialize from.
		// @param [section_name] The name of the section being serialized (for organizational purposes).
		// @param [option] The operation mode: save or load.
		binary(const std::filesystem::path filename, const std::string& section_name, option option);

		// @brief Destructor that closes the open file streams.
		//        Ensures proper cleanup of file handles when the serializer is destroyed.
		~binary();

		// @brief Serializes or deserializes a single value based on the current operation mode.
		//        Handles special cases for std::filesystem::path and std::string types.
		//        For strings, writes/reads the length followed by the character data.
		// @tparam T The type of value to serialize/deserialize.
		// @param [value] Reference to the value to be serialized (when saving) or populated (when loading).
		// @return Reference to the binary serializer for method chaining.
		template <typename T>
		binary& entry(T& value) {

			if (m_option == option::save) {

				if constexpr (std::is_same_v<T, std::filesystem::path>) {

					std::string path_str = value.generic_string();
					entry<std::string>(path_str);

				} else if constexpr (std::is_same_v<T, std::string>) {

					size_t length = value.size();
					m_ostream.write(reinterpret_cast<const char*>(&length), sizeof(length));
					m_ostream.write(reinterpret_cast<const char*>(value.data()), length);

				} else
					m_ostream.write(reinterpret_cast<const char*>(&value), sizeof(T));

			} else {

				if constexpr (std::is_same_v<T, std::filesystem::path>) {

					std::string buffer;
					entry<std::string>(buffer);
					value = std::filesystem::path(buffer);

				} else if constexpr (std::is_same_v<T, std::string>) {

					size_t length = 0;
					m_istream.read(reinterpret_cast<char*>(&length), sizeof(length));

					ASSERT(length < 65565, "", "Corrupted path length")

					value.resize(length);
					m_istream.read(value.data(), length);

				} else
					m_istream.read(reinterpret_cast<char*>(&value), sizeof(T));
			}

			return *this;
		}

		// @brief Serializes or deserializes a vector of values.
		//        For saving, writes the vector size followed by the element data.
		//        For loading, reads the vector size, resizes the vector, and reads the element data.
		// @tparam T The type of elements in the vector.
		// @param [vector] Reference to the vector to be serialized (when saving) or populated (when loading).
		// @return Reference to the binary serializer for method chaining.
		template <typename T>
		binary& entry(std::vector<T>& vector) {

			if (m_option == option::save) {

				size_t size = vector.size();
				m_ostream.write(reinterpret_cast<const char*>(&size), sizeof(size_t));
				m_ostream.write(reinterpret_cast<const char*>(vector.data()), sizeof(T)* size);

			} else {

				size_t vector_size = 0;
				m_istream.read(reinterpret_cast<char*>(&vector_size), sizeof(size_t));
				vector.resize(vector_size);
				m_istream.read(reinterpret_cast<char*>(vector.data()), sizeof(T)* vector_size);
			}

			return *this;
		}

		// @brief Serializes or deserializes a raw array.
		//        When loading, allocates memory for the array using malloc (caller assumes ownership).
		//        For saving, writes the entire array as a contiguous block of bytes.
		// @tparam T The type of elements in the array.
		// @param [array_start] Pointer to the start of the array (when saving) or pointer that will be allocated (when loading).
		// @param [array_size] The number of elements in the array.
		// @return Reference to the binary serializer for method chaining.
		template <typename T>
		binary& array(T* array_start, size_t array_size) {

			const size_t total_bytes = sizeof(T)* array_size;
			if (m_option == option::save) {

				m_ostream.write(reinterpret_cast<const char*>(array_start), total_bytes);

			} else {

				array_start = (T*)malloc(total_bytes);
				LOG(trace, "Deserializing [{}] bytes into [{}]", total_bytes, (void*)array_start)
				m_istream.read(reinterpret_cast<char*>(array_start), total_bytes);
			}

			return *this;
		}

		// @brief Serializes or deserializes a vector with a custom function for each element.
		//        Currently not implemented - intended for complex vector serialization with custom logic.
		// @tparam T The type of elements in the vector.
		// @param [vector] Reference to the vector to be serialized or deserialized.
		// @param [vector_function] Function to handle serialization/deserialization of each element.
		// @return Reference to the binary serializer for method chaining.
		template <typename T>
		binary& vector(std::vector<T>& vector, std::function<void(GLT::serializer::binary& , const u64 iteration)> vector_function) {

			LOG(error, "NOT IMPLEMENTED YET");
			return *this;
		}

	private:

		std::filesystem::path 			m_filename{};		// Path to the file being serialized to/from
		std::string						m_name{};			// Name of the section being serialized
		option							m_option;			// Current operation mode (save or load)
		std::ofstream					m_ostream{};		// Output file stream for saving data
		std::ifstream 					m_istream{};		// Input file stream for loading data
	};

}
