
#pragma once



namespace GLT::terrain {

    // @brief A compiled per-sample formula: "a * smoothstep(0.3, 0.7, b) + c"
    //
    // Variables: the inputs a, b, c, d; x and z (world position of the sample); constants pi, e. Operators: + - * / ^ (power),
    // unary -, comparisons < <= > >= == != (1 or 0), && || !, and cond ? a : b. Functions: sin cos tan asin acos atan
    // atan2 sqrt abs floor ceil fract round sign exp log pow min max clamp lerp mix smoothstep step saturate.
    // Compiled once to a small stack program, evaluated per sample without allocations
    class expression {
    public:

        static constexpr u32                            INPUT_COUNT = 4;        // a, b, c, d

        // Empty error = success
        [[nodiscard]] static std::expected<expression, std::string> compile(std::string_view source);

        [[nodiscard]] f32 evaluate(const f32 inputs[INPUT_COUNT], f32 x, f32 z) const noexcept;

        // Which of a..d the formula reads (unused inputs need not be evaluated)
        [[nodiscard]] bool uses_input(u32 index) const noexcept { return index < INPUT_COUNT && (m_used_inputs & (1u << index)) != 0; }

        enum class op : u8 {
            constant, input, var_x, var_z,
            neg, logical_not,
            add, sub, mul, div, pow, less, less_equal, greater, greater_equal, equal, not_equal, logical_and, logical_or,
            select,
            fn_sin, fn_cos, fn_tan, fn_asin, fn_acos, fn_atan, fn_atan2, fn_sqrt, fn_abs, fn_floor, fn_ceil, fn_fract,
            fn_round, fn_sign, fn_exp, fn_log, fn_pow, fn_min, fn_max, fn_clamp, fn_lerp, fn_smoothstep, fn_step, fn_saturate,
        };

        struct instruction {
            op                                          code = op::constant;
            f32                                         value = 0.0f;           // constant
            u32                                         index = 0;              // input
        };

    private:

        friend class expression_parser;

        std::vector<instruction>                        m_code{};
        u32                                             m_max_stack = 0;
        u32                                             m_used_inputs = 0;
    };

}
