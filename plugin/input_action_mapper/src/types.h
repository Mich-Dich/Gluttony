
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::input_action_mapper {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    namespace action {

        enum class trigger : u16 {

            none					= BIT(0),		// NEVER activate input
            // key trigger flags
            key_down				= BIT(1),		// activate input when key is pressed down (can repeat)
            key_up					= BIT(2),		// activate input when key NOT pressed (can repeat)
            key_hold				= BIT(3),		// activate input when key down LONGER than [duration_in_sec] in input_action struct (can repeat)
            key_tap					= BIT(4),		// activate input when key down SHORTER than [duration_in_sec] in input_action struct (can repeat)
            key_move_down			= BIT(5),		// activate input when key transitions from up to down (can NOT repeat)
            key_move_up				= BIT(6),		// activate input when key transitions from down to up (can NOT repeat)
            // mouse trigger flags
            mouse_positive			= BIT(10),
            mouse_negative			= BIT(11),
            mouse_pos_and_neg		= BIT(12),
        };


        enum class modefire : u16 {

            none					= BIT(0),		// NEVER activate input
            negate					= BIT(1),
            use_vec_normal			= BIT(2),
            axis_0_negative			= BIT(3),
            axis_1					= BIT(4),
            axis_1_negative			= BIT(5),
            axis_2					= BIT(6),
            axis_2_negative			= BIT(7),
            auto_reset				= BIT(8),
            auto_reset_all			= BIT(9),
            smooth_interp			= BIT(10),		// !! CAUTION !! not implemented yet
        };


        // Defines the data type stored by an input action.
        enum class type : u8 {
            boolean,	// Boolean true/false value
            vec_1D,		// Single floating-point value
            vec_2D,		// 2D vector value
            vec_3D,		// 3D vector value
        };

    }

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
