
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

    // @brief Single event type carrying every mouse action
    //
    // Uses a tag (action_type) to distinguish between movement, scroll, and cursor enter/leave. [delta] is meaningful for [move]
    // and [scroll]; only [entered] is meaningful for [enter]. [state] is populated by the input layer for button-driven events
    class mouse_event : public event {
    public:

        // @brief Which kind of mouse action this event represents
        enum class action_type : u8 {
            move,       // Mouse movement
            scroll,     // Mouse wheel scroll
            enter       // Mouse entered/left window
        };

        // @brief Constructs a movement or scroll event
        // @param type   [action_type::move] or [action_type::scroll]
        // @param delta  Movement (pixels) or scroll amount for the event
        mouse_event(const action_type type, const glm::vec2 delta)
            : m_action_type(type), m_delta(delta) {}

        // @brief Constructs a cursor enter/leave event
        // @param entered  true when the cursor entered the window, false when it left
        mouse_event(bool entered)
            : m_action_type(action_type::enter), m_entered(entered) {}

        // Getters
		DEFAULT_GETTER_C(action_type, 		action_type)
		DEFAULT_GETTER_C(key_state,			state)
		DEFAULT_GETTER_C(glm::vec2, 		delta)
		DEFAULT_GETTER_C(bool, 				entered)

        // @brief Human-readable description of this mouse event
        [[nodiscard]] FORCE_INLINE std::string to_string() const override {
            switch (m_action_type) {
                case action_type::move:     return std::format("mouse moved to [{:.1f}, {:.1f}]", m_delta.x, m_delta.y);
                case action_type::scroll:   return std::format("mouse scrolled [{:.1f}, {:.1f}]", m_delta.x, m_delta.y);
				case action_type::enter:    return std::format("mouse cursor [{}]", m_entered ? "entered" : "left");
                default:                    return "mouse event [unknown]";
            }
        }

    private:

        const action_type 					m_action_type;
        key_state 							m_state{};
        glm::vec2 							m_delta{};
        bool 								m_entered{false};
    };


    // @brief Single event type carrying every physical-keyboard action
    //
    // Carries the physical key (key_code), its new state (pressed / released / repeated), and the modifier bits active at the time
    // Not to be confused with [char_event], which carries text input rather than keys
    class key_event : public event {
    public:
        // @param code   Physical key that changed state
        // @param state  New state of the key
        // @param mods   Modifier-key bitmask active at the time
        key_event(key_code code, key_state state, i32 mods = 0)
            : m_key_code(code), m_key_state(state), m_modifiers(mods) {}

        DEFAULT_GETTER_C(key_code,          key_code)
        DEFAULT_GETTER_C(key_state,         key_state)
        DEFAULT_GETTER_C(i32,               modifiers)

        // @brief Human-readable description of this key event
        [[nodiscard]] FORCE_INLINE std::string to_string() const override {
            return std::format("key [{}] state [{}] mods [{}]", static_cast<u16>(m_key_code), static_cast<u16>(m_key_state), m_modifiers);
        }

        // @brief Convenience check for a specific key code
        // @param code  Key code to compare against
        // @return true if this event is for that key, regardless of state
        FORCE_INLINE_R bool is_key_code(const key_code code) const { return m_key_code == code; }

        // @brief Convenience check for a specific state
        // @param code  Key state to compare against
        // @return true if this event has that state, regardless of which key
        FORCE_INLINE_R bool is_key_state(const key_state code) const { return m_key_state == code; }
        
        // @brief Convenience check for a (key, state) pair
        // @param code   Key code to match
        // @param state  Key state to match
        // @return true if both the key and state match
        FORCE_INLINE_R bool is(const key_code code, const key_state state) const { return m_key_code == code && m_key_state == state; }

    private:

        key_code                            m_key_code;
        key_state                           m_key_state;
        i32                                 m_modifiers;
    };


    // @brief Text-input event, distinct from physical key events
    //
    // Fires with a Unicode codepoint produced by the platform's text input path (which handles IME, dead keys, and layout remapping)
    // Use this for text fields; use [key_event] for gameplay bindings
    class char_event : public event {
    public:
        // @param codepoint  Unicode codepoint produced
        // @param mods       Modifier-key bitmask active at the time
        char_event(u32 codepoint, i32 mods = 0)
            : m_codepoint(codepoint), m_modifiers(mods) {}

        DEFAULT_GETTER_C(u32, codepoint)
        DEFAULT_GETTER_C(i32, modifiers)

        // @brief Human-readable description of this character-input event
        [[nodiscard]] FORCE_INLINE std::string to_string() const override {
            return std::format("char input [{}] mods [{}]", m_codepoint, m_modifiers);
        }

    private:
        u32 m_codepoint;
        i32 m_modifiers;
    };

}
