#pragma once

// #include <imgui.h>

#include "util/data_structures/UUID.h"

// FORWARD DECLARATIONS ================================================================================================

namespace GLT::util
{

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    #define EXTRACT_AFTER_GLT(path) ([](const std::string& str) -> std::string {        \
            const std::string delimiter = "GLT";                                        \
            size_t pos = str.find(delimiter);                                           \
            if (pos != std::string::npos) {                                             \
                pos = str.find(delimiter, pos + delimiter.length());                    \
                if (pos != std::string::npos) {                                         \
                    return str.substr(pos + delimiter.length() + 1);                    \
                }                                                                       \
            }                                                                           \
            return ""; }(path))

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // @brief Searches for the last occurrence of the specified delimiter in the input string,
    //          and if found, extracts the substring after the delimiter into the 'dest' string.
    //          If the delimiter is not found, the 'dest' string remains unchanged.
    // @param [dest] Reference to a string where the extracted part will be stored.
    // @param [input] The input string from which the part is to be extracted.
    // @param [delimiter] The character delimiter used to identify the part to extract.
    // @return None
    void extract_part_after_delimiter(std::string& dest, const std::string& input, const char* delimiter);


    // @brief Searches for the last occurrence of the specified delimiter in the input string,
    //          and if found, extracts the substring before the delimiter into the 'dest' string.
    //          If the delimiter is not found, the 'dest' string remains unchanged.
    // @param [dest] Reference to a string where the extracted part will be stored.
    // @param [input] The input string from which the part is to be extracted.
    // @param [delimiter] The character delimiter used to identify the part to extract.
    // @return None
    void extract_part_befor_delimiter(std::string& dest, const std::string& input, const char* delimiter);


    // @brief Given a string representing a variable access chain (e.g., "object1->object2.variable"),
    //          this function extracts and returns the name of the variable ("variable" in this example).
    //          The extraction process considers both "->" and "." as delimiters for nested access.
    // @param [input] The input string representing the variable access chain.
    // @return A string containing the name of the variable extracted from the input string.
    std::string extract_variable_name(const std::string& input);


    // @brief Converts a string to a boolean value.
    //        Returns true if the string equals "true" (case-sensitive), false otherwise.
    // @param [string] The string to convert.
    // @return true if the string is "true", false otherwise.
    constexpr bool str_to_bool(const std::string& string) { return (string == "true") ? true : false; }


    // @brief Converts a boolean value to a string.
    //        Returns "true" for true values and "false" for false values.
    // @param [boolean] The boolean value to convert.
    // @return [const char*] "true" if the boolean value is true, "false" otherwise.
    constexpr const char* bool_to_str(bool boolean) { return boolean ? "true" : "false"; }


    // Lowercase a copy — used to normalize file extensions.
    std::string to_lower(std::string s);


    // @brief Creates a string consisting of multiple indentation levels of spaces.
    //        The total number of spaces is calculated as multiple_of_indenting_spaces * num_of_indenting_spaces.
    // @param [multiple_of_indenting_spaces] The number of indentation levels to generate.
    // @param [num_of_indenting_spaces] The number of spaces per indentation level (default is 2).
    // @return A string containing the calculated number of space characters.
    std::string add_spaces(const u32 multiple_of_indenting_spaces, u32 num_of_indenting_spaces = 2);


    // @brief Measures the indentation level of a string by counting leading spaces.
    //        The indentation level is calculated by dividing the number of leading spaces
    //        by the specified number of spaces per indentation level.
    // @param [str] The input string whose indentation is to be measured.
    // @param [num_of_indenting_spaces] The number of spaces per indentation level (default is 2).
    // @return The indentation level as an integer (number of indentation units).
    u32 measure_indentation(const std::string& str, u32 num_of_indenting_spaces = 2);


    // @brief Counts the number of lines in a null-terminated character array.
    //        The function scans up to 256 characters or until a null terminator is encountered.
    //        It counts newline characters and includes the last line even if it doesn't end with a newline.
    // @param [text] A null-terminated character array containing the text to analyze.
    // @return The number of lines in the text. Returns 1 for an empty string.
    int count_lines(const char* text);

    // TEMPLATE DECLARATION ============================================================================================

    // @brief Removes all occurrences of a substring from a character array.
    //        The function also replaces double quotes (") with single quotes (') in the result.
    // @tparam N The size of the source character array.
    // @tparam K The size of the remove character array.
    // @param source The source character array from which to remove the substring.
    // @param remove The substring to remove from the source array.
    // @return A character array with the specified substring removed and quotes replaced.
    template <size_t N, size_t K>
    constexpr auto remove_substring(const char (&source)[N], const char (&remove)[K]);


    // @brief Converts a string to a numeric value of the specified type.
    //        Uses stringstream for the conversion, supporting all standard numeric types.
    // @tparam T The numeric type to convert to (e.g., int, float, double).
    // @param [str] The string representation of the number.
    // @return The numeric value parsed from the string.
    template <typename T>
    T str_to_num(const std::string& str);


    // @brief Converts a numeric value to its string representation.
    //        Uses stringstream for the conversion, supporting all standard numeric types.
    // @tparam T The numeric type of the value to convert.
    // @param [num] The numeric value to convert to a string.
    // @return A string representation of the numeric value.
    template <typename T>
    std::string num_to_str(const T& num);


    // @brief Converts a type name to a human-readable string.
    //        Handles compiler-specific type name mangling (MSVC, GCC, Clang) and
    //        removes namespace qualifiers for cleaner output.
    // @tparam T The type whose name should be converted.
    // @param [typename_string] Reference to a string that will receive the type name.
    // @return None
    template <typename T>
    void convert_typename_to_string(std::string& typename_string);


    // @brief Formats multiple arguments into a single string by concatenating their string representations.
    //        Uses fold expression to efficiently concatenate all arguments.
    // @tparam Args Variadic template parameter pack representing the types of arguments.
    // @param [args] The arguments to format and concatenate.
    // @return A single string containing the concatenated string representations of all arguments.
    template <typename... Args>
    std::string format_string(Args&& ...args);


    // @brief Converts a value of type T to its string representation.
    //        Can handle conversion from various types such as: arithmetic types, boolean, glm::vec2,
    //        glm::vec3, glm::vec4, ImVec2, ImVec4, and glm::mat4, as well as custom types like version,
    //        system_time, filesystem::path, and UUID. If the input value type is not supported,
    //        a DEBUG_BREAK() is triggered.
    // @param [src_value] The value to be converted to a string.
    // @param [dest_string] Reference to a string that will receive the conversion result.
    // @tparam T The type of the value to be converted.
    // @return None
    template <typename T>
    constexpr void to_string(const T& src_value, std::string& dest_string);


    // @brief Converts a value of type T to its string representation and returns it.
    //        This is a convenience wrapper around the void version of to_string.
    // @tparam T The type of the value to be converted.
    // @param [src_value] The value to be converted to a string.
    // @return A string representing the input value.
    template <typename T>
    constexpr std::string to_string(const T& src_value);


    // @brief Converts a string representation to a value of type T.
    //        Can handle conversion into various types such as: arithmetic types, boolean, glm::vec2,
    //        glm::vec3, glm::vec4, ImVec2, ImVec4, and glm::mat4, as well as custom types like version,
    //        system_time, filesystem::path, and UUID. If the input value type is not supported,
    //        a DEBUG_BREAK() is triggered.
    // @param [src_string] The string to be converted to a value.
    // @param [dest_value] Reference to the variable that will store the converted value.
    // @tparam T The type of the value the string should be converted to.
    // @return None
    template <typename T>
    constexpr void from_string(const std::string& src_string, T& dest_value);


    // @brief Converts a string representation to a value of type T and returns it.
    //        This is a convenience wrapper around the void version of from_string.
    // @tparam T The type of the value the string should be converted to.
    // @param [src_string] The string to be converted to a value.
    // @return The value of type T converted from the string.
    template <typename T>
    constexpr T from_string(const std::string& src_string);

    // CLASS DECLARATION ===============================================================================================

}

#include "string_manipulation.inl"
