#pragma once

#include "action_types.h"

#include "action_types.h"

#include <event/input_event.h>


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::input_action_mapper {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================
    
    static std::unordered_map<UUID, ref<action>>                g_actions{};
        
    static u64                                                  g_next_uuid = 1;

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // Helper: bitwise operators for enum classes (using underlying type)
    template<typename Enum>
    constexpr Enum operator|(Enum lhs, Enum rhs) {
    
        return static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(lhs) | static_cast<std::underlying_type_t<Enum>>(rhs));
    }


    template<typename Enum>
    constexpr Enum operator&(Enum lhs, Enum rhs) {
    
        return static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(lhs) & static_cast<std::underlying_type_t<Enum>>(rhs));
    }


    constexpr trigger operator|(trigger a, trigger b)           { return static_cast<trigger>(static_cast<u16>(a) | static_cast<u16>(b)); }


    constexpr bool has_flag(trigger value, trigger flag)        { return (static_cast<u16>(value) & static_cast<u16>(flag)) != 0; }
    
    
    constexpr bool has_flag(modefire value, modefire flag)      { return (static_cast<u16>(value) & static_cast<u16>(flag)) != 0; }


    bool is_key(const key_code key) {

        return key != key_code::mouse_move
            && key != key_code::mouse_move_x
            && key != key_code::mouse_move_y
            && key != key_code::mouse_scroll_x
            && key != key_code::mouse_scroll_y;
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // NAMESPACE FUNCTIONS =============================================================================================

    UUID register_action(action&& def) {

        UUID id = g_next_uuid++;
        auto act = create_ref<action>(std::move(def));
        g_actions[id] = act;
        return id;
    }


    weak_ref<action> get_action(const UUID id) {

        auto it = g_actions.find(id);
        if (it != g_actions.end())
            return weak_ref<action>(it->second);
        return weak_ref<action>();
    }

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    void plugin::on_load() {

        m_key_event_sub = GLT::event_bus::subscribe<GLT::key_event>( std::bind_front(&plugin::on_key_event, this) );
        m_mouse_event_sub = GLT::event_bus::subscribe<GLT::mouse_event>( std::bind_front(&plugin::on_mouse_event, this) );
        LOG_LOADED
    }

    
    void plugin::on_unload() {

        GLT::event_bus::unsubscribe(m_key_event_sub);
        GLT::event_bus::unsubscribe(m_mouse_event_sub);
        LOG_UNLOADED
    }


    void plugin::update() {

        auto now = std::chrono::steady_clock::now();

        for (auto& [id, action] : g_actions) {                                              // Process each registered action
            if (!action) continue;

			if (has_flag(action->flags, modefire::auto_reset)) {

                switch (action->data_type) {
                    
                    default:                [[fallthrough]];
                    case type::boolean:     action->m_current_value = false; break;
                    case type::vec_1D:      action->m_current_value = 0.0f; break;
                    case type::vec_2D:      action->m_current_value = glm::vec2(0.0f); break;
                    case type::vec_3D:      action->m_current_value = glm::vec3(0.0f); break;
                }
                action->status = status_flag::inactive;
            }
            // Use old [action->status]
            // // Reset status to "inactive" – will be updated during binding processing

            action_value new_value;                                                         // Start with a zero value according to the action's data type
            switch (action->data_type) {
                case type::boolean: new_value = false; break;
                case type::vec_1D:  new_value = 0.0f; break;
                case type::vec_2D:  new_value = glm::vec2(0.0f); break;
                case type::vec_3D:  new_value = glm::vec3(0.0f); break;
            }

            for (const auto& binding : action->bindings) {                                  // Evaluate every binding of this action

                float contribution = 0.0f;          // raw contribution (scalar)

                // ----- Evaluate the trigger condition -----
                // Keyboard bindings
                if (is_key(binding.key)) {

                    if (!m_key_states.contains(binding.key)) continue;             // key not pressed this frame

                    const key_info& current_key_state = m_key_states.at(binding.key);
                    // TODO: use [current_key_state] and [action->status_flags] to determine edge-triggered statuses like "started" and "completed"

                    const bool was_down = current_key_state.previous == key_state::press || current_key_state.previous == key_state::repeat;
                    const bool is_down  = current_key_state.current == key_state::press || current_key_state.current == key_state::repeat;
                    const bool just_pressed  = is_down && !was_down;
                    const bool just_released = !is_down && was_down;

                    if (has_flag(binding.trigger_flags, trigger::key_down) && is_down)                          contribution = 1.0f;
                    else if (has_flag(binding.trigger_flags, trigger::key_up) && !is_down)                      contribution = 1.0f;
                    else if (has_flag(binding.trigger_flags, trigger::key_move_down) && just_pressed)           contribution = 1.0f;
                    else if (has_flag(binding.trigger_flags, trigger::key_move_up) && just_released)            contribution = 1.0f;

                    // key_hold : held longer than action.duration_in_sec
                    else if (has_flag(binding.trigger_flags, trigger::key_hold) && is_down) {
                        auto held_duration = std::chrono::duration<float>(now - current_key_state.press_time).count();
                        if (held_duration >= action->duration_in_sec)
                            contribution = 1.0f;
                    }

                    // key_tap : pressed and released within action.duration_in_sec
                    else if (has_flag(binding.trigger_flags, trigger::key_tap) && just_released) {
                        auto held_duration = std::chrono::duration<float>(current_key_state.release_time - current_key_state.press_time).count();
                        if (held_duration <= action->duration_in_sec)
                            contribution = 1.0f;
                    }

                } else {                                // Mouse bindings (movement / scroll)

                    float raw_delta = 0.0f;
                    switch (binding.key) {              // Extract the appropriate axis value from the accumulated mouse state
                        case key_code::mouse_move_x:    raw_delta = m_mouse_state.delta.x; break;
                        case key_code::mouse_move_y:    raw_delta = m_mouse_state.delta.y; break;
                        case key_code::mouse_scroll_x:  raw_delta = m_mouse_state.scroll.x; break;
                        case key_code::mouse_scroll_y:  raw_delta = m_mouse_state.scroll.y; break;
                        case key_code::mouse_move:      raw_delta = glm::length(m_mouse_state.delta); break;
                        default:                        continue;
                    }

                    // Mouse triggers based on sign / non‑zero
                    if (has_flag(binding.trigger_flags, trigger::mouse_pos_and_neg) && raw_delta != 0.0f)       contribution = raw_delta;
                    else if (has_flag(binding.trigger_flags, trigger::mouse_positive) && raw_delta > 0.0f)      contribution = raw_delta;
                    else if (has_flag(binding.trigger_flags, trigger::mouse_negative) && raw_delta < 0.0f)      contribution = raw_delta;
                }

                if (contribution == 0.f)
                    continue;

                // ----- Apply binding‑level modifiers -----
                if (has_flag(binding.modifier_flags, modefire::negate))
                    contribution = -contribution;

                // ----- Combine contribution into the action's value according to its data type -----
                switch (action->data_type) {
                    case type::boolean: {
                        // Any active binding makes the boolean true
                        std::get<bool>(new_value) = true;
                    } break;

                    case type::vec_1D: {
                        std::get<float>(new_value) += contribution;
                    } break;
                    
                    case type::vec_2D: {
                        glm::vec2& vec = std::get<glm::vec2>(new_value);
                        if (has_flag(binding.modifier_flags, modefire::axis_0_negative))        vec.x -= contribution;
                        else if (has_flag(binding.modifier_flags, modefire::axis_1))            vec.y += contribution;
                        else if (has_flag(binding.modifier_flags, modefire::axis_1_negative))   vec.y -= contribution;
                        else                                                                    vec.x += contribution;   // default: X axis
                    } break;
                    
                    case type::vec_3D: {
                        glm::vec3& vec = std::get<glm::vec3>(new_value);
                        if (has_flag(binding.modifier_flags, modefire::axis_1))                 vec.y += contribution;
                        else if (has_flag(binding.modifier_flags, modefire::axis_1_negative))   vec.y -= contribution;
                        else if (has_flag(binding.modifier_flags, modefire::axis_2))            vec.z += contribution;
                        else if (has_flag(binding.modifier_flags, modefire::axis_2_negative))   vec.z -= contribution;
                        else                                                                    vec.x += contribution;   // default: X axis
                    } break;
                }
            }

            // ----- Apply action‑level modifiers after all bindings are accumulated -----
            if (has_flag(action->flags, modefire::use_vec_normal)) {

                switch (action->data_type) {
                    case type::vec_1D: {
                        float& v = std::get<float>(new_value);
                        v = std::clamp(v, -1.0f, 1.0f);
                    } break;

                    case type::vec_2D: {
                        glm::vec2& v = std::get<glm::vec2>(new_value);
                        float len = glm::length(v);
                        if (len > std::numeric_limits<float>::epsilon())
                            v /= len;
                    } break;

                    case type::vec_3D: {
                        glm::vec3& v = std::get<glm::vec3>(new_value);
                        float len = glm::length(v);
                        if (len > std::numeric_limits<float>::epsilon())
                            v /= len;
                    } break;

                    default: break;
                }
            }

            // ----- Determine status flags by comparing old and new values -----
            auto& old_value = action->m_current_value;
            u32 new_status = 0;

            const float eps = 1e-6f;

            // Helper to test zero-ness for each type
            auto is_zero = [&](const action_value& val) -> bool {
                switch (action->data_type) {
                    case type::boolean: return !std::get<bool>(val);
                    case type::vec_1D:  return std::abs(std::get<float>(val)) < eps;
                    case type::vec_2D:  return glm::length(std::get<glm::vec2>(val)) < eps;
                    case type::vec_3D:  return glm::length(std::get<glm::vec3>(val)) < eps;
                    default:            return true;
                }
            };

            // Active / inactive
            bool was_active = !is_zero(old_value);
            bool is_active  = !is_zero(new_value);

            if (is_active)                          new_status |= static_cast<u32>(status_flag::active);
            else                                    new_status |= static_cast<u32>(status_flag::inactive);

            // Edge triggers: started / completed
            if (!was_active && is_active)           new_status |= static_cast<u32>(status_flag::started);
            else if (was_active && !is_active)      new_status |= static_cast<u32>(status_flag::completed);
            // canceled cannot be derived from value alone – keep previous canceled flag if any,
            // but typically cleared each frame unless external logic sets it.
            // We preserve the old `canceled` bit only if it was already set and the action is still active?
            // For simplicity we do not propagate canceled here.

            // Value change detection
            bool changed = false, increased = false, decreased = false;
            switch (action->data_type) {
                case type::boolean: {
                    bool old_b = std::get<bool>(old_value);
                    bool new_b = std::get<bool>(new_value);
                    changed = (old_b != new_b);
                    increased = (new_b && !old_b);
                    decreased = (!new_b && old_b);
                } break;

                case type::vec_1D: {
                    float old_f = std::get<float>(old_value);
                    float new_f = std::get<float>(new_value);
                    changed = (std::abs(old_f - new_f) > eps);
                    increased = (new_f > old_f + eps);
                    decreased = (new_f < old_f - eps);
                } break;

                case type::vec_2D: {
                    glm::vec2 old_v = std::get<glm::vec2>(old_value);
                    glm::vec2 new_v = std::get<glm::vec2>(new_value);
                    changed = (glm::length(old_v - new_v) > eps);
                    float old_len = glm::length(old_v);
                    float new_len = glm::length(new_v);
                    increased = (new_len > old_len + eps);
                    decreased = (new_len < old_len - eps);
                } break;

                case type::vec_3D: {
                    glm::vec3 old_v = std::get<glm::vec3>(old_value);
                    glm::vec3 new_v = std::get<glm::vec3>(new_value);
                    changed = (glm::length(old_v - new_v) > eps);
                    float old_len = glm::length(old_v);
                    float new_len = glm::length(new_v);
                    increased = (new_len > old_len + eps);
                    decreased = (new_len < old_len - eps);
                } break;
            }
            if (changed)        new_status |= static_cast<u32>(status_flag::value_changed);
            if (increased)      new_status |= static_cast<u32>(status_flag::value_increased);
            if (decreased)      new_status |= static_cast<u32>(status_flag::value_decreased);

            // Reached max / min (assuming normalised range where max magnitude = 1)
            switch (action->data_type) {
                case type::boolean: {
                    if (std::get<bool>(new_value))
                        new_status |= static_cast<u32>(status_flag::value_reached_max);
                    else
                        new_status |= static_cast<u32>(status_flag::value_reached_min);
                } break;

                case type::vec_1D: {
                    float v = std::get<float>(new_value);
                    if (v >= 1.0f - eps)
                        new_status |= static_cast<u32>(status_flag::value_reached_max);
                    if (v <= -1.0f + eps)
                        new_status |= static_cast<u32>(status_flag::value_reached_min);
                } break;

                case type::vec_2D: {
                    float len = glm::length(std::get<glm::vec2>(new_value));
                    if (len >= 1.0f - eps)
                        new_status |= static_cast<u32>(status_flag::value_reached_max);
                    if (len <= eps)
                        new_status |= static_cast<u32>(status_flag::value_reached_min);
                } break;

                case type::vec_3D: {
                    float len = glm::length(std::get<glm::vec3>(new_value));
                    if (len >= 1.0f - eps)
                        new_status |= static_cast<u32>(status_flag::value_reached_max);
                    if (len <= eps)
                        new_status |= static_cast<u32>(status_flag::value_reached_min);
                } break;
            }

            // Direction change (2D/3D only)
            if (action->data_type == type::vec_2D || action->data_type == type::vec_3D) {
                
                bool was_dir_active = (action->data_type == type::vec_2D) 
                    ? glm::length(std::get<glm::vec2>(old_value)) > eps
                    : glm::length(std::get<glm::vec3>(old_value)) > eps;

                bool now_dir_active = is_active;
                if (was_dir_active && now_dir_active) {

                    float angle = 0.0f;
                    if (action->data_type == type::vec_2D) {
                        glm::vec2 old_n = glm::normalize(std::get<glm::vec2>(old_value));
                        glm::vec2 new_n = glm::normalize(std::get<glm::vec2>(new_value));
                        angle = glm::acos(glm::clamp(glm::dot(old_n, new_n), -1.0f, 1.0f));

                    } else {

                        glm::vec3 old_n = glm::normalize(std::get<glm::vec3>(old_value));
                        glm::vec3 new_n = glm::normalize(std::get<glm::vec3>(new_value));
                        angle = glm::acos(glm::clamp(glm::dot(old_n, new_n), -1.0f, 1.0f));
                    }

                    if (angle > 0.01f)  // threshold ~0.57 degrees
                        new_status |= static_cast<u32>(status_flag::direction_changed);
                }
            }

            // Preserve any flags that are not overwritten but should persist (e.g., canceled)?
            // Usually status is refreshed every frame, so we assign directly.
            
            // (Optional) you can add checks for value_reached_max/min and time‑based flags later
            
            action->status = static_cast<status_flag>(new_status);
            action->m_current_value = std::move(new_value);                                 // Store the final value
        }

        m_key_states.clear();
        m_mouse_state = {};
    }
    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    void plugin::on_key_event(const GLT::key_event& event) {

        auto& info = m_key_states[event.get_key_code()];
        info.previous = info.current;
        info.current = event.get_key_state();
        switch (info.current) {
            case GLT::key_state::press:         info.press_time = std::chrono::steady_clock::now(); break;
            case GLT::key_state::repeat:        break;
            case GLT::key_state::release:       info.release_time = std::chrono::steady_clock::now(); break;
            default:                            break;
        }
    }


    void plugin::on_mouse_event(const GLT::mouse_event& event) {

        switch (event.get_action_type()) {
            case GLT::mouse_event::action_type::move:       m_mouse_state.delta += event.get_delta(); break;
            case GLT::mouse_event::action_type::scroll:     m_mouse_state.scroll += event.get_delta(); break;
            case GLT::mouse_event::action_type::enter:      [[fallthrough]];    // ignore
            default:        break;
        }

    }

}
