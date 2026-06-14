
#pragma once

// INCLUDES ============================================================================================================

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::argument_parser {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Supported value types
    using value = std::variant<std::string, int, double, bool, std::filesystem::path, GLT::UUID>;


    struct argument_spec {

        std::string                                 name;                   // e.g., "file", "uuid", "help"
        std::string                                 short_name;              // optional, e.g. "f"
        bool                                        required = false;
        bool                                        positional = false;     // if true, position matters; otherwise named flag/option
        int                                         position = -1;          // 0-based index for positional args
        std::string                                 type = "string";        // "string", "int", "double", "bool", "path", "uuid"
        value                                       default_value;           // used when optional and not provided
        std::string                                 help_text;
    };

    // Custom error codes
    enum class arg_error {

        success = 0,
        unknown_argument,
        missing_required,
        invalid_type,
        missing_value,
        duplicate_argument,
        internal_error,
    };


    struct parsed_result {
        
        std::unordered_map<std::string, value>      values;
        std::vector<std::string>                    positional_order;  // names in order of appearance
    };

    // FUNCTION DECLARATION ============================================================================================


    // Helper to extract a typed value from parsedResult
    template<typename T>
    T get(const parsed_result& result, const std::string& name);


    std::error_code make_error_code(arg_error e);


    // Main parsing function
    parsed_result parse_arguments(const std::vector<argument_spec>& specs, int argc, char* argv[],
        std::error_code& error);


    std::vector<std::string> tokenize_string(const std::string& cmd);

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

#include "argument_parser.inl"

// Specialization must be in global namespace std
namespace std
{

    template<>
    struct is_error_code_enum<GLT::argument_parser::arg_error> : true_type { };

}

