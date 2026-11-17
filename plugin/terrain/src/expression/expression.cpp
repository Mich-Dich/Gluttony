
#include "util/pch.h"
#include "expression.h"

#include <cctype>
#include <charconv>



namespace GLT::terrain {

    // CONSTANTS =======================================================================================================

    static constexpr u32                                MAX_STACK = 64;

    // TYPES ===========================================================================================================

    namespace {

        struct function_info {
            std::string_view                            name;
            expression::op                              code;
            u32                                         arity;
        };

        constexpr function_info FUNCTIONS[] = {
            { "sin", expression::op::fn_sin, 1 },           { "cos", expression::op::fn_cos, 1 },
            { "tan", expression::op::fn_tan, 1 },           { "asin", expression::op::fn_asin, 1 },
            { "acos", expression::op::fn_acos, 1 },         { "atan", expression::op::fn_atan, 1 },
            { "atan2", expression::op::fn_atan2, 2 },       { "sqrt", expression::op::fn_sqrt, 1 },
            { "abs", expression::op::fn_abs, 1 },           { "floor", expression::op::fn_floor, 1 },
            { "ceil", expression::op::fn_ceil, 1 },         { "fract", expression::op::fn_fract, 1 },
            { "round", expression::op::fn_round, 1 },       { "sign", expression::op::fn_sign, 1 },
            { "exp", expression::op::fn_exp, 1 },           { "log", expression::op::fn_log, 1 },
            { "pow", expression::op::fn_pow, 2 },           { "min", expression::op::fn_min, 2 },
            { "max", expression::op::fn_max, 2 },           { "clamp", expression::op::fn_clamp, 3 },
            { "lerp", expression::op::fn_lerp, 3 },         { "mix", expression::op::fn_lerp, 3 },
            { "smoothstep", expression::op::fn_smoothstep, 3 }, { "step", expression::op::fn_step, 2 },
            { "saturate", expression::op::fn_saturate, 1 },
        };


        // Stack effect: values popped / pushed
        [[nodiscard]] std::pair<u32, u32> stack_effect(expression::op code) {
            using op = expression::op;
            switch (code) {
                case op::constant: case op::input: case op::var_x: case op::var_z:  return { 0, 1 };
                case op::neg: case op::logical_not:                                  return { 1, 1 };
                case op::select: case op::fn_clamp: case op::fn_lerp: case op::fn_smoothstep: return { 3, 1 };
                case op::add: case op::sub: case op::mul: case op::div: case op::pow: case op::less: case op::less_equal:
                case op::greater: case op::greater_equal: case op::equal: case op::not_equal: case op::logical_and:
                case op::logical_or: case op::fn_atan2: case op::fn_pow: case op::fn_min: case op::fn_max: case op::fn_step:
                                                                                     return { 2, 1 };
                default:                                                             return { 1, 1 };     // one-argument functions
            }
        }

    }

    // CLASS DECLARATION ===============================================================================================

    class expression_parser {
    public:

        explicit expression_parser(std::string_view source) : m_src(source) {}

        std::expected<expression, std::string> run() {

            // Accept an optional "out =" prefix (the design doc's notation)
            skip_space();
            if (m_src.substr(m_pos, 3) == "out") {
                size_t p = m_pos + 3;
                while (p < m_src.size() && std::isspace(static_cast<unsigned char>(m_src[p])))
                    ++p;
                if (p < m_src.size() && m_src[p] == '=' && (p + 1 >= m_src.size() || m_src[p + 1] != '='))
                    m_pos = p + 1;
            }

            if (!parse_ternary())
                return std::unexpected(m_error);
            skip_space();
            if (m_pos < m_src.size())
                return std::unexpected(std::format("unexpected '{}' at column {}", m_src[m_pos], m_pos + 1));
            if (m_out.m_code.empty())
                return std::unexpected(std::string("empty expression"));

            // Stack depth check (also proves the program is well formed)
            i32 depth = 0;
            u32 max_depth = 0;
            for (const auto& ins : m_out.m_code) {
                const auto [pop, push] = stack_effect(ins.code);
                depth -= static_cast<i32>(pop);
                if (depth < 0)
                    return std::unexpected(std::string("malformed expression"));
                depth += static_cast<i32>(push);
                max_depth = std::max(max_depth, static_cast<u32>(depth));
            }
            if (depth != 1)
                return std::unexpected(std::string("malformed expression"));
            if (max_depth > MAX_STACK)
                return std::unexpected(std::string("expression too deeply nested"));
            m_out.m_max_stack = max_depth;
            return std::move(m_out);
        }

    private:

        using op = expression::op;

        void emit(op code, f32 value = 0.0f, u32 index = 0) { m_out.m_code.push_back({ code, value, index }); }

        bool fail(std::string message) {
            if (m_error.empty())
                m_error = std::format("{} (column {})", message, m_pos + 1);
            return false;
        }

        void skip_space() {
            while (m_pos < m_src.size() && std::isspace(static_cast<unsigned char>(m_src[m_pos])))
                ++m_pos;
        }

        bool accept(std::string_view token) {
            skip_space();
            if (m_src.substr(m_pos, token.size()) == token) {
                m_pos += token.size();
                return true;
            }
            return false;
        }

        [[nodiscard]] bool peek(std::string_view token) {
            skip_space();
            return m_src.substr(m_pos, token.size()) == token;
        }

        bool parse_ternary() {
            if (!parse_or())
                return false;
            if (accept("?")) {
                if (!parse_ternary())
                    return false;
                if (!accept(":"))
                    return fail("expected ':'");
                if (!parse_ternary())
                    return false;
                emit(op::select);
            }
            return true;
        }

        bool parse_or() {
            if (!parse_and())
                return false;
            while (accept("||")) {
                if (!parse_and())
                    return false;
                emit(op::logical_or);
            }
            return true;
        }

        bool parse_and() {
            if (!parse_equality())
                return false;
            while (accept("&&")) {
                if (!parse_equality())
                    return false;
                emit(op::logical_and);
            }
            return true;
        }

        bool parse_equality() {
            if (!parse_comparison())
                return false;
            for (;;) {
                op code;
                if (accept("=="))      code = op::equal;
                else if (accept("!=")) code = op::not_equal;
                else                   return true;
                if (!parse_comparison())
                    return false;
                emit(code);
            }
        }

        bool parse_comparison() {
            if (!parse_additive())
                return false;
            for (;;) {
                op code;
                if (accept("<="))      code = op::less_equal;
                else if (accept(">=")) code = op::greater_equal;
                else if (accept("<"))  code = op::less;
                else if (accept(">"))  code = op::greater;
                else                   return true;
                if (!parse_additive())
                    return false;
                emit(code);
            }
        }

        bool parse_additive() {
            if (!parse_term())
                return false;
            for (;;) {
                op code;
                if (accept("+"))      code = op::add;
                else if (accept("-")) code = op::sub;
                else                  return true;
                if (!parse_term())
                    return false;
                emit(code);
            }
        }

        bool parse_term() {
            if (!parse_unary())
                return false;
            for (;;) {
                op code;
                if (accept("*"))      code = op::mul;
                else if (accept("/")) code = op::div;
                else                  return true;
                if (!parse_unary())
                    return false;
                emit(code);
            }
        }

        bool parse_unary() {
            if (accept("-")) {
                if (!parse_unary())
                    return false;
                emit(op::neg);
                return true;
            }
            if (peek("!") && !peek("!=")) {
                accept("!");
                if (!parse_unary())
                    return false;
                emit(op::logical_not);
                return true;
            }
            if (accept("+"))
                return parse_unary();
            return parse_power();
        }

        bool parse_power() {
            if (!parse_primary())
                return false;
            if (accept("^")) {
                if (!parse_unary())             // right associative: 2^3^2 = 2^9
                    return false;
                emit(op::pow);
            }
            return true;
        }

        bool parse_primary() {

            skip_space();
            if (m_pos >= m_src.size())
                return fail("unexpected end of expression");

            const char c = m_src[m_pos];

            if (accept("(")) {
                if (!parse_ternary())
                    return false;
                if (!accept(")"))
                    return fail("expected ')'");
                return true;
            }

            if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
                const char* begin = m_src.data() + m_pos;
                const char* end = m_src.data() + m_src.size();
                f32 value = 0.0f;
                const auto [ptr, ec] = std::from_chars(begin, end, value);
                if (ec != std::errc())
                    return fail("bad number");
                m_pos += static_cast<size_t>(ptr - begin);
                emit(op::constant, value);
                return true;
            }

            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                const size_t start = m_pos;
                while (m_pos < m_src.size() && (std::isalnum(static_cast<unsigned char>(m_src[m_pos])) || m_src[m_pos] == '_'))
                    ++m_pos;
                const std::string_view name = m_src.substr(start, m_pos - start);

                if (accept("(")) {
                    const function_info* fn = nullptr;
                    for (const auto& f : FUNCTIONS)
                        if (f.name == name)
                            fn = &f;
                    if (!fn)
                        return fail(std::format("unknown function '{}'", name));
                    u32 args = 0;
                    if (!accept(")")) {
                        do {
                            if (!parse_ternary())
                                return false;
                            ++args;
                        } while (accept(","));
                        if (!accept(")"))
                            return fail("expected ')' after arguments");
                    }
                    if (args != fn->arity)
                        return fail(std::format("'{}' takes {} argument(s), got {}", name, fn->arity, args));
                    emit(fn->code);
                    return true;
                }

                if (name.size() == 1 && name[0] >= 'a' && name[0] <= 'd') {
                    const u32 index = static_cast<u32>(name[0] - 'a');
                    emit(op::input, 0.0f, index);
                    m_out.m_used_inputs |= 1u << index;
                    return true;
                }
                if (name == "x")  { emit(op::var_x); return true; }
                if (name == "z")  { emit(op::var_z); return true; }
                if (name == "pi") { emit(op::constant, 3.14159265358979f); return true; }
                if (name == "e")  { emit(op::constant, 2.71828182845905f); return true; }
                return fail(std::format("unknown name '{}' (inputs are a, b, c, d; position is x, z)", name));
            }

            return fail(std::format("unexpected '{}'", c));
        }

        std::string_view                                m_src;
        size_t                                          m_pos = 0;
        std::string                                     m_error{};
        expression                                      m_out{};
    };

    // CLASS IMPLEMENTATION ============================================================================================

    std::expected<expression, std::string> expression::compile(std::string_view source) {

        expression_parser parser(source);
        return parser.run();
    }


    f32 expression::evaluate(const f32 inputs[INPUT_COUNT], f32 x, f32 z) const noexcept {

        f32 stack[MAX_STACK];
        u32 top = 0;            // number of values on the stack

        for (const instruction& ins : m_code) {
            switch (ins.code) {
                case op::constant:      stack[top++] = ins.value; break;
                case op::input:         stack[top++] = inputs[ins.index]; break;
                case op::var_x:         stack[top++] = x; break;
                case op::var_z:         stack[top++] = z; break;
                case op::neg:           stack[top - 1] = -stack[top - 1]; break;
                case op::logical_not:   stack[top - 1] = stack[top - 1] == 0.0f ? 1.0f : 0.0f; break;
                case op::fn_sin:        stack[top - 1] = std::sin(stack[top - 1]); break;
                case op::fn_cos:        stack[top - 1] = std::cos(stack[top - 1]); break;
                case op::fn_tan:        stack[top - 1] = std::tan(stack[top - 1]); break;
                case op::fn_asin:       stack[top - 1] = std::asin(std::clamp(stack[top - 1], -1.0f, 1.0f)); break;
                case op::fn_acos:       stack[top - 1] = std::acos(std::clamp(stack[top - 1], -1.0f, 1.0f)); break;
                case op::fn_atan:       stack[top - 1] = std::atan(stack[top - 1]); break;
                case op::fn_sqrt:       stack[top - 1] = std::sqrt(std::max(stack[top - 1], 0.0f)); break;
                case op::fn_abs:        stack[top - 1] = std::abs(stack[top - 1]); break;
                case op::fn_floor:      stack[top - 1] = std::floor(stack[top - 1]); break;
                case op::fn_ceil:       stack[top - 1] = std::ceil(stack[top - 1]); break;
                case op::fn_fract:      stack[top - 1] = stack[top - 1] - std::floor(stack[top - 1]); break;
                case op::fn_round:      stack[top - 1] = std::round(stack[top - 1]); break;
                case op::fn_sign:       stack[top - 1] = stack[top - 1] > 0.0f ? 1.0f : (stack[top - 1] < 0.0f ? -1.0f : 0.0f); break;
                case op::fn_exp:        stack[top - 1] = std::exp(stack[top - 1]); break;
                case op::fn_log:        stack[top - 1] = std::log(std::max(stack[top - 1], 1e-30f)); break;
                case op::fn_saturate:   stack[top - 1] = std::clamp(stack[top - 1], 0.0f, 1.0f); break;
                default: {
                    // Two and three operand operations
                    const auto [pop, push] = stack_effect(ins.code);
                    (void)push;
                    if (pop == 2) {
                        const f32 b = stack[--top];
                        const f32 a = stack[top - 1];
                        f32 r = 0.0f;
                        switch (ins.code) {
                            case op::add:           r = a + b; break;
                            case op::sub:           r = a - b; break;
                            case op::mul:           r = a * b; break;
                            case op::div:           r = b != 0.0f ? a / b : 0.0f; break;
                            case op::pow:
                            case op::fn_pow:        r = std::pow(a, b); break;
                            case op::less:          r = a < b ? 1.0f : 0.0f; break;
                            case op::less_equal:    r = a <= b ? 1.0f : 0.0f; break;
                            case op::greater:       r = a > b ? 1.0f : 0.0f; break;
                            case op::greater_equal: r = a >= b ? 1.0f : 0.0f; break;
                            case op::equal:         r = a == b ? 1.0f : 0.0f; break;
                            case op::not_equal:     r = a != b ? 1.0f : 0.0f; break;
                            case op::logical_and:   r = (a != 0.0f && b != 0.0f) ? 1.0f : 0.0f; break;
                            case op::logical_or:    r = (a != 0.0f || b != 0.0f) ? 1.0f : 0.0f; break;
                            case op::fn_atan2:      r = std::atan2(a, b); break;
                            case op::fn_min:        r = std::min(a, b); break;
                            case op::fn_max:        r = std::max(a, b); break;
                            case op::fn_step:       r = b < a ? 0.0f : 1.0f; break;        // step(edge, x)
                            default: break;
                        }
                        stack[top - 1] = r;
                    } else if (pop == 3) {
                        const f32 c = stack[--top];
                        const f32 b = stack[--top];
                        const f32 a = stack[top - 1];
                        f32 r = 0.0f;
                        switch (ins.code) {
                            case op::select:        r = a != 0.0f ? b : c; break;
                            case op::fn_clamp:      r = std::clamp(a, std::min(b, c), std::max(b, c)); break;
                            case op::fn_lerp:       r = a + (b - a) * c; break;
                            case op::fn_smoothstep: {
                                const f32 t = b != a ? std::clamp((c - a) / (b - a), 0.0f, 1.0f) : (c < a ? 0.0f : 1.0f);
                                r = t * t * (3.0f - 2.0f * t);
                                break;
                            }
                            default: break;
                        }
                        stack[top - 1] = r;
                    }
                    break;
                }
            }
        }
        return top > 0 ? stack[top - 1] : 0.0f;
    }

}
