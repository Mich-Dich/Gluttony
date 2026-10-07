
#pragma once

// INCLUDES ============================================================================================================

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::argument_parser {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // @brief The set of value types an argument can hold
    //
    // A single variant covers every type the parser knows how to convert to, so [parsed_result::values] can store 
    // heterogeneous arguments in one container. The type string in [argument_spec::type] selects which
    // alternative [convert_value()] will populate
    using value = std::variant<std::string, int, double, bool, std::filesystem::path, GLT::UUID>;


    // @brief Declares one argument the parser should accept
    //
    // A spec is either positional ([positional == true], identified by [position]) or named. Named specs are addressed by [name]
    // (long form, matched after [--]) and optionally by [short_name] (single-dash form, e.g. [-f]). A named spec whose [type] is "bool"
    // is treated as a presence flag: it consumes no value and is stored as [true] when seen
    struct argument_spec {

        std::string                                 name;                   // e.g., "file", "uuid", "help"
        std::string                                 short_name;             // optional, e.g. "f"
        bool                                        required = false;
        bool                                        positional = false;     // if true, position matters; otherwise named flag/option
        int                                         position = -1;          // 0-based index for positional args
        std::string                                 type = "string";        // "string", "int", "double", "bool", "path", "uuid"
        value                                       default_value;          // used when optional and not provided
        std::string                                 help_text;
    };

    // @brief Failure codes produced by [parse_arguments()]
    //
    // Wired into [std::error_code] via the [is_error_code_enum] specialization at the bottom of this header, so it can be returned
    // alongside the parse result without a separate out-parameter per failure mode
    enum class arg_error : u8 {

        success = 0,
        unknown_argument,           // neither a known name nor a valid positional
        missing_required,           // a spec marked required was never populated
        invalid_type,               // value failed to convert to the spec's declared type
        missing_value,              // a value-taking flag was the last token on the command line
        duplicate_argument,         // reserved; currently unused by the parser
        internal_error,             // unknown type string in a spec (developer error)
    };


    // @brief The result of a successful parse
    //
    // [values] maps each populated argument to its parsed value - populated for every spec, whether the value came from the
    // command line or from [argument_spec::default_value]. [positional_order] records the names of positional args in the order
    // they appeared, which is useful when the caller wants to preserve "first file wins" semantics without relying on
    // the (unordered) [values] map
    struct parsed_result {
        
        std::unordered_map<std::string, value>      values;
        std::vector<std::string>                    positional_order;  // names in order of appearance
    };

    // FUNCTION DECLARATION ============================================================================================


    // @brief Extracts a typed value from a parsed result
    //
    // Throws [std::runtime_error] if [name] isn't present in [result.values], and propagates [std::bad_variant_access] if the
    // stored alternative doesn't match [T]. Callers should check the error code returned by [parse_arguments()] before calling this
    //
    // @tparam T     The alternative to extract (must match the variant's stored type)
    // @param result  The parsed result to read from
    // @param name    The argument name to look up
    // @return The stored value, converted to [T]
    template<typename T>
    T get(const parsed_result& result, const std::string& name);


    // @brief Builds a [std::error_code] for a [arg_error] value
    //
    // Required so [arg_error] participates in the standard error-code machinery; the [is_error_code_enum] specialization
    // below wires up implicit construction from [arg_error]
    //
    // @param e  The parser error to wrap
    // @return An [error_code] bound to the parser's error category
    std::error_code make_error_code(arg_error e);


    // @brief Parses a command line against a spec list
    //
    // Iterates [argv[1..argc)] once, dispatching each token to either a named flag / option or a positional slot (in the
    // order positional specs were declared). On any failure, [error] is set and the returned [parsed_result] should be treated
    // as incomplete; on success [error] holds arg_error::success
    //
    // @param specs  Specs describing every accepted argument
    // @param argc   Argument count, including [argv[0]]
    // @param argv   Argument vector; [argv[0]] is skipped
    // @param error  Out-parameter set to the parse outcome
    // @return The parsed values, populated as far as parsing succeeded
    parsed_result parse_arguments(const std::vector<argument_spec>& specs, int argc, char* argv[], std::error_code& error);


    // @brief Splits a command line string into whitespace-separated tokens
    //
    // Naive tokenizer: does not honor quoting or escaping, so it should only be used for simple dev-console inputs and
    // simple string splitting
    //
    // @param cmd  The raw command line to split
    // @return The tokens, in the order they appeared
    std::vector<std::string> tokenize_string(const std::string& cmd);

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

#include "argument_parser.inl"

// Specialization must be in global namespace std
namespace std {

    template<>
    struct is_error_code_enum<GLT::argument_parser::arg_error> : true_type { };

}
