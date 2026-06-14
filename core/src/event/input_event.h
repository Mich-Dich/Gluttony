#pragma once

#include "event.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT {

	// CONSTANTS =======================================================================================================

	// MACROS ==========================================================================================================

	// TYPES ===========================================================================================================

	// STATIC VARIABLES ================================================================================================

	// FUNCTION DECLARATION ============================================================================================

	// TEMPLATE DECLARATION ============================================================================================

	// CLASS DECLARATION ===============================================================================================

    // Single mouse event class for all mouse actions
    class mouse_event : public event {
    public:

        enum class action_type : u8 {
            move,       // Mouse movement
            scroll,     // Mouse wheel scroll
            enter       // Mouse entered/left window
        };

        // Constructor for movement/scroll events
        mouse_event(const action_type type, const glm::vec2 delta)
            : m_action_type(type), m_delta(delta) {}

        // Constructor for cursor enter/leave events
        mouse_event(bool entered)
            : m_action_type(action_type::enter), m_entered(entered) {}

        // Getters
		DEFAULT_GETTER_C(action_type, 		action_type)
		DEFAULT_GETTER_C(key_state,			state)
		DEFAULT_GETTER_C(glm::vec2, 		delta)
		DEFAULT_GETTER_C(bool, 				entered)

        [[nodiscard]] FORCE_INLINE std::string to_string() const override {
            switch (m_action_type) {
                case action_type::move:
                    return std::format("mouse moved to [{:.1f}, {:.1f}]", m_delta.x, m_delta.y);

                case action_type::scroll:
                    return std::format("mouse scrolled [{:.1f}, {:.1f}]", m_delta.x, m_delta.y);

				case action_type::enter:
                    return std::format("mouse cursor [{}]", m_entered ? "entered" : "left");

                default:
                    return "mouse event [unknown]";
            }
        }

    private:

        const action_type 					m_action_type;
        key_state 							m_state{};
        glm::vec2 							m_delta{};
        bool 								m_entered{false};
    };


    // Single keyboard event class for all keyboard actions
    class key_event : public event {
    public:
        key_event(key_code code, key_state state, i32 mods = 0)
            : m_key_code(code), m_key_state(state), m_modifiers(mods) {}

        DEFAULT_GETTER_C(key_code, key_code)
        DEFAULT_GETTER_C(key_state, key_state)
        DEFAULT_GETTER_C(i32, modifiers)

        [[nodiscard]] FORCE_INLINE std::string to_string() const override {
            return std::format("key [{}] state [{}] mods [{}]",
                static_cast<u16>(m_key_code), static_cast<u16>(m_key_state), m_modifiers);
        }

    private:
        key_code m_key_code;
        key_state m_key_state;
        i32 m_modifiers;
    };


    // Character input event (text input, not physical keys)
    class char_event : public event {
    public:
        char_event(u32 codepoint, i32 mods = 0)
            : m_codepoint(codepoint), m_modifiers(mods) {}

        DEFAULT_GETTER_C(u32, codepoint)
        DEFAULT_GETTER_C(i32, modifiers)

        [[nodiscard]] FORCE_INLINE std::string to_string() const override {
            return std::format("char input [{}] mods [{}]", m_codepoint, m_modifiers);
        }

    private:
        u32 m_codepoint;
        i32 m_modifiers;
    };

}
