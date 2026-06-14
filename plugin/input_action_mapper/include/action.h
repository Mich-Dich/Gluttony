
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::input_action_mapper {

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    enum class trigger : u16 {

        // key trigger flags
        key_down				= BIT(0),		// activate input when key is pressed down (can repeat)
        key_up					= BIT(1),		// activate input when key NOT pressed (can repeat)
        key_hold				= BIT(2),		// activate input when key down LONGER than [duration_in_sec] in input_action struct (can repeat)
        key_tap					= BIT(3),		// activate input when key down SHORTER than [duration_in_sec] in input_action struct (can repeat)
        key_move_down			= BIT(4),		// activate input when key transitions from up to down (can NOT repeat)
        key_move_up				= BIT(5),		// activate input when key transitions from down to up (can NOT repeat)
        // mouse trigger flags
        mouse_positive          = BIT(10),      // active when mouse delta > 0
        mouse_negative          = BIT(11),      // active when mouse delta < 0
        mouse_pos_and_neg       = BIT(12),      // active for both signs (delta used as is)
    };


    enum class modefire : u16 {

        negate                  = BIT(0),       // invert the contribution
        use_vec_normal          = BIT(1),       // normalise the resulting vector
        axis_0_negative         = BIT(2),       // contribute to X but negative
        axis_1                  = BIT(3),       // contribute to Y
        axis_1_negative         = BIT(4),       // contribute to Y negative
        axis_2                  = BIT(5),       // contribute to Z
        axis_2_negative         = BIT(6),       // contribute to Z negative
        auto_reset              = BIT(7),       // reset to zero when no binding active
        smooth_interp           = BIT(9),       // interpolate value over duration_in_sec
    };


    enum class status_flag : u32 {
        none                    = 0,

        // --- Activation edges ---
        started                 = BIT(0),   // Action just became active (value > 0)
        completed               = BIT(1),   // Action just became inactive (value == 0) after having been active
        canceled                = BIT(2),   // Action interrupted before completion (e.g., chord broken, gesture rejected)

        // --- Continuous state ---
        active                  = BIT(3),   // Action is currently non‑zero (held, being performed)
        inactive                = BIT(4),   // Action is currently zero (idle)

        // --- Value changes ---
        value_changed           = BIT(5),   // Raw value changed by any amount
        value_increased         = BIT(6),   // Value increased this frame
        value_decreased         = BIT(7),   // Value decreased this frame
        value_reached_max       = BIT(8),   // Value hit its maximum (1.0 or custom cap)
        value_reached_min       = BIT(9),   // Value hit its minimum (0.0 or custom floor)

        // --- Time‑based triggers ---
        held_for_threshold      = BIT(10),  // Action been active for a minimum time (tap vs hold)
        held_long_press         = BIT(11),  // Held past a “long press” duration
        held_repeat             = BIT(12),  // Fires every repeat interval while held (like key repeat)

        // --- Multi‑tap / sequence ---
        tapped_once             = BIT(13),  // Quick press and release (completed within tap window)
        double_tapped           = BIT(14),  // Two quick taps in succession
        triple_tapped           = BIT(15),  // Three quick taps
        multi_tap               = BIT(16),  // Generic N‑tap (count provided separately)

        // --- Chord / combination ---
        chord_started           = BIT(17),  // A required modifier key/button became active
        chord_broken            = BIT(18),  // Modifier was lost before action completed

        // --- Directional (analogue sticks / gestures) ---
        direction_changed       = BIT(19),  // 2D direction (e.g., stick angle) changed
        crossed_deadzone        = BIT(20),  // Value entered or left deadzone
        entered_deadzone        = BIT(21),  // Value entered deadzone (from outside)
        exited_deadzone         = BIT(22),  // Value left deadzone (from inside)

    };


    // Defines the data type stored by an input action.
    enum class type : u8 {
        boolean,	                // Boolean true/false value
        vec_1D,		                // Single floating-point value
        vec_2D,		                // 2D vector value
        vec_3D,		                // 3D vector value
    };


    constexpr u32 operator|(status_flag a, status_flag b) { return static_cast<u32>(a) | static_cast<u32>(b); }
    

    constexpr u32 operator|(u32 a, status_flag b) { return a | static_cast<u32>(b); }


    constexpr u32 operator&(status_flag a, status_flag b) { return static_cast<u32>(a) & static_cast<u32>(b); }


    constexpr u32 operator~(status_flag a) { return ~static_cast<u32>(a); }


    using action_value = std::variant<bool, f32, glm::vec2, glm::vec3>;


    struct binding {

        key_code                    key;
        trigger                     trigger_flags;
        modefire                    modifier_flags;
    };


    struct action {
    public:

        std::string                 description{};
        bool                        trigger_when_paused = true;
        modefire                    flags{};
        type                        data_type{};
        float                       duration_in_sec = 0.0f;
        std::vector<binding>        bindings{};
        status_flag                 status{};


        // Get current value (read‑only)
        FORCE_INLINE_R const action_value& get() const { return m_current_value; }


        // Get current value (read‑only)
        template<typename T>
        const T get() const { 

            if (std::holds_alternative<T>(m_current_value))
                return std::get<T>(m_current_value);
            return {};
        }

        action_value                m_current_value;

    };


    // CONSTANTS =======================================================================================================
    
    constexpr u32 AnyEdge = status_flag::started | status_flag::completed | status_flag::canceled;
    
    constexpr u32 AnyValueChange = status_flag::value_changed | status_flag::value_increased | status_flag::value_decreased | status_flag::value_reached_max | status_flag::value_reached_min;
    
    constexpr u32 AnyTap = status_flag::tapped_once | status_flag::double_tapped | status_flag::triple_tapped | status_flag::multi_tap;
    
    constexpr u32 AllEvents = 0xFFFFFFFF;

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================
    
    // CLASS DECLARATION ===============================================================================================

}
