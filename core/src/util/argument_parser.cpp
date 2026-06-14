/*******************************************************************************************************
(c) NewTec GmbH 2026   -   www.newtec.de
*******************************************************************************************************/
/**
 @ingroup ingroup_group
 @file ArgumentParser.cpp
 @see @ref ArgumentParser.h for detailed description.
*******************************************************************************************************/

/* INCLUDES *******************************************************************************************/

#include "util/pch.h"

#include "argument_parser.h"

/* FORWARD DECLARATION ********************************************************************************/


namespace GLT::argument_parser {

    /* CONSTANTS **************************************************************************************/

    /* MACROS *****************************************************************************************/

    /* TYPES ******************************************************************************************/

    class loc_error_category : public std::error_category {
    public:

        const char* name() const noexcept override { return "ArgParser"; }


        std::string message(int ev) const override {

            switch (static_cast<arg_error>(ev)) {
                case arg_error::success:                return "success";
                case arg_error::unknown_argument:       return "Unknown argument";
                case arg_error::missing_required:       return "Missing required argument";
                case arg_error::invalid_type:           return "Invalid type conversion";
                case arg_error::missing_value:          return "Missing value for argument";
                case arg_error::duplicate_argument:     return "Duplicate argument";
                default:                                return "Unknown error";
            }
        }
    };

    /* STATIC VARIABLES *******************************************************************************/

    /* INTERNAL FUNCTION DECLARATION ******************************************************************/

    /* INTERNAL FUNCTION IMPLEMENTATION ***************************************************************/

    const std::error_category& arg_error_category() {

        static loc_error_category instance;
        return instance;
    }


    // Helper: convert string to value based on type
    static std::error_code convert_value(const std::string& raw, const std::string& typeName, value& out) {

        if (typeName == "string")
            out = raw;

        else if (typeName == "int") {

            int val;
            auto [ptr, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), val);
            if (ec != std::errc())
                return make_error_code(arg_error::invalid_type);

            out = val;

        } else if (typeName == "double") {

            double val;
            auto [ptr, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), val);
            if (ec != std::errc())
                return make_error_code(arg_error::invalid_type);

            out = val;
        
        } else if (typeName == "bool") {

            if (raw == "true" || raw == "1" || raw == "yes")
                out = true;
            else if (raw == "false" || raw == "0" || raw == "no")
                out = false;
            else
                return make_error_code(arg_error::invalid_type);

        } else if (typeName == "path") {

            out = std::filesystem::path(raw);

        } else if (typeName == "uuid") {

            GLT::UUID uuid = GLT::util::from_string<GLT::UUID>(raw);
            if (uuid == 0U)
                return make_error_code(arg_error::invalid_type);

            out = uuid;

        } else
            return make_error_code(arg_error::internal_error);

        return make_error_code(arg_error::success);
    }

    /* FUNCTION IMPLEMENTATION ************************************************************************/

    std::error_code make_error_code(arg_error e) {

        return std::error_code(static_cast<int>(e), arg_error_category());
    }


    parsed_result parseArguments(const std::vector<argument_spec>& specs, int argc, char* argv[], std::error_code& error) {

        parsed_result out{};

        // Build lookup maps
        std::unordered_map<std::string, const argument_spec*> nameToSpec;
        std::unordered_map<std::string, const argument_spec*> shortToSpec;
        std::unordered_map<int, const argument_spec*> positionalSpecs;

        for (const auto& spec : specs) {

            nameToSpec[spec.name] = &spec;
            if (!spec.short_name.empty())
                shortToSpec[spec.short_name] = &spec;

            if (spec.positional && spec.position >= 0)
                positionalSpecs[spec.position] = &spec;

            if (!spec.required && spec.default_value.index() != std::variant_npos)       // Initialize with default values
                out.values[spec.name] = spec.default_value;
        }

        // Parse argv[1..]
        int pos = 0;
        for (int index = 1; index < argc; index++) {

            std::string arg = argv[index];
            const argument_spec* currentSpec = nullptr;

            // Check if it's a named argument (starts with - or --)
            if (arg.size() > 1 && arg[0] == '-') {

                std::string key;
                bool longForm = (arg.size() > 2 && arg[1] == '-');
                if (longForm)
                    key = arg.substr(2);
                else
                    key = arg.substr(1); // single dash, e.g. -f

                // Find spec
                std::unordered_map<std::string, const argument_spec*>::const_iterator it;
                if (longForm) {

                    it = nameToSpec.find(key);
                    if (it == nameToSpec.end()) {

                        error = make_error_code(arg_error::unknown_argument);
                        return out;
                    }

                } else {

                    // short form: try short name first
                    it = shortToSpec.find(key);
                    if (it == shortToSpec.end()) {

                        // not found as short name, try as full name
                        it = nameToSpec.find(key);
                        if (it == nameToSpec.end()) {

                            error = make_error_code(arg_error::unknown_argument);
                            return out;
                        }
                    }
                }
                currentSpec = it->second;

                // Check if this flag expects a value
                bool takesValue = (currentSpec->type != "bool");
                if (takesValue) {

                    // Next token should be value
                    if (index + 1 >= argc) {

                        error = make_error_code(arg_error::missing_value);
                        return out;
                    }

                    std::string rawValue = argv[++index];
                    value val;
                    auto ec = convert_value(rawValue, currentSpec->type, val);
                    if (ec) {

                        error = ec;
                        return out;
                    }
                    out.values[currentSpec->name] = val;

                } else
                    out.values[currentSpec->name] = true;       // bool flag: presence means true

            } else {

                // positional argument
                auto it = positionalSpecs.find(pos);
                if (it == positionalSpecs.end()) {

                    error = make_error_code(arg_error::unknown_argument);
                    return out;
                }
                currentSpec = it->second;
                value val;
                auto ec = convert_value(arg, currentSpec->type, val);
                if (ec) {

                    error = ec;
                    return out;
                }
                out.values[currentSpec->name] = val;
                out.positional_order.push_back(currentSpec->name);
                ++pos;
            }
        }

        // Check required arguments
        for (const auto& spec : specs) {

            if (spec.required && out.values.find(spec.name) == out.values.end()) {

                error = make_error_code(arg_error::missing_required);
                return out;
            }
        }

        error = make_error_code(arg_error::success);
        return out;
    }

    
    std::vector<std::string> tokenizeString(const std::string& cmd) {

        std::vector<std::string> tokens;
        std::stringstream ss(cmd);
        std::string token;
        while (ss >> token)
            tokens.push_back(token);

        return tokens;
    }

    /* CLASS IMPLEMENTATION ***************************************************************************/

    /* CLASS PUBLIC ***********************************************************************************/

    /* CLASS PROTECTED ********************************************************************************/

    /* CLASS PRIVATE **********************************************************************************/

}
