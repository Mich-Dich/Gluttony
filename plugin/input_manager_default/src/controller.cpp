
#include "util/pch.h"
#include "controller.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::input::input_manager_default {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    controller::controller()
        : m_key_sub(event_bus::subscribe_scoped<GLT::key_event>([this](GLT::key_event& e){ on_key_event(e); }))
        , m_mouse_sub(event_bus::subscribe_scoped<GLT::mouse_event>([this](GLT::mouse_event& e){ on_mouse_event(e); })) { }


    controller::~controller() = default;

    // CLASS PUBLIC ====================================================================================================

    handle controller::add_action(action action) {

        u32 index;
        if (!m_free_indices.empty()) {

            index = m_free_indices.back();
            m_free_indices.pop_back();

        } else {

            index = static_cast<u32>(m_slots.size());
            m_slots.emplace_back();
        }

        auto& slot = m_slots[index];
        slot.act = std::move(action);
        slot.runtime = action_runtime{};
        slot.runtime.bindings.resize(slot.act.bindings.size());
        for (size_t index = 0; index < slot.act.bindings.size(); ++index)
            slot.runtime.bindings[index].triggers.resize(slot.act.bindings[index].triggers.size());

        slot.alive = true;
            
        return pack(index, slot.generation);
    }


    void controller::remove_action(::handle h) {

        const auto index = handle_index(h);
        const auto generation = handle_generation(h);
        if (index >= m_slots.size())
            return;

        auto& slot = m_slots[index];
        if (!slot.alive || slot.generation != generation)
            return;

        slot.alive = false;
        slot.act = {};
        slot.runtime = {};

        ++slot.generation;
        if (slot.generation == 0)
            slot.generation = 1;   // keep 0 reserved for INVALID_HANDLE

        m_free_indices.push_back(index);
    }


    void controller::clear_actions() {

        for (auto& slot : m_slots) {

            if (!slot.alive)
                continue;

            slot.alive = false;
            slot.act = {};
            slot.runtime = {};

            ++slot.generation;
            if (slot.generation == 0)
                slot.generation = 1;

            m_free_indices.push_back(static_cast<u32>(&slot - m_slots.data()));
        }
    }


    // per-frame step --------------------------------------------------------------------------------------------------

    void controller::update(f32 delta_time) {

        for (auto& action_slot : m_slots) {

            if (!action_slot.alive)
                continue;

            auto& action_runtime = action_slot.runtime;
            action_runtime.prev_value  = action_runtime.value;
            action_runtime.prev_active = action_runtime.active;

            glm::vec3 accumulated_value{0.0f};
            bool any_binding_active = false;

            for (size_t index = 0; index < action_slot.act.bindings.size(); ++index) {

                const auto& binding = action_slot.act.bindings[index];
                auto& binding_runtime = action_runtime.bindings[index];

                glm::vec3 evaluated_value = apply_mods(eval_source(binding.src), binding.modifiers);

                const f32 magnitude = std::max({
                    std::abs(evaluated_value.x),
                    std::abs(evaluated_value.y),
                    std::abs(evaluated_value.z)
                });

                if (binding.triggers.empty()) {

                    if (magnitude > 0.0f) {
                        any_binding_active = true;
                        accumulated_value += evaluated_value;
                    }

                } else {

                    for (size_t trigger_index = 0; trigger_index < binding.triggers.size(); ++trigger_index) {

                        if (eval_trigger(
                                binding.triggers[trigger_index],
                                binding_runtime.triggers[trigger_index],
                                magnitude,
                                delta_time)) {

                            any_binding_active = true;
                            accumulated_value += evaluated_value;
                        }
                    }
                }
            }

            action_runtime.value = accumulated_value;
            action_runtime.active = any_binding_active;
            action_runtime.held_time = any_binding_active ? action_runtime.held_time + delta_time : 0.0f;
        }

        m_raw.mouse_delta  = glm::vec2{0.0f};
        m_raw.mouse_scroll = glm::vec2{0.0f};
    }

    // query surface ---------------------------------------------------------------------------------------------------

    bool controller::is_active(::handle hand) const {

        const auto* slot = find(hand);
        return slot && slot->runtime.active;
    }


    bool controller::was_pressed(::handle hand) const {

        const auto* slot = find(hand);
        if (!slot)
            return false;
        return slot->runtime.active && !slot->runtime.prev_active;
    }


    bool controller::was_released(::handle hand) const {

        const auto* slot = find(hand);
        if (!slot)
            return false;
        return !slot->runtime.active && slot->runtime.prev_active;
    }


    f32 controller::get_axis(::handle hand) const {

        const auto* slot = find(hand);
        return slot ? slot->runtime.value.x : 0.0f;
    }


    glm::vec2 controller::get_axis2d(::handle hand) const {

        const auto* slot = find(hand);
        return slot ? glm::vec2{ slot->runtime.value } : glm::vec2{0.0f};
    }


    glm::vec3 controller::get_axis3d(::handle hand) const {

        const auto* slot = find(hand);
        return slot ? slot->runtime.value : glm::vec3{0.0f};
    }


    f32 controller::get_held_time(::handle hand) const {

        const auto* slot = find(hand);
        return slot ? slot->runtime.held_time : 0.0f;
    }

    // raw access ------------------------------------------------------------------------------------------------------

    bool controller::is_key_down(GLT::key_code key_code) const {

        if (!is_valid_button_code(key_code))
            return false;

        const auto button_state = m_raw.button_states[static_cast<size_t>(key_code)];
        return button_state == GLT::key_state::press || button_state == GLT::key_state::repeat;
    }


    bool controller::is_mouse_down(GLT::key_code mouse_button_code) const { return is_key_down(mouse_button_code); }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    // lookup ----------------------------------------------------------------------------------------------------------

    const controller::action_slot* controller::find(::handle handle) const {

        const auto idx = handle_index(handle);
        const auto gen = handle_generation(handle);
        if (idx >= m_slots.size())
            return nullptr;
            
        const auto& slot = m_slots[idx];
        if (!slot.alive || slot.generation != gen)
            return nullptr;

        return &slot;
    }


    controller::action_slot* controller::find(::handle handle) { return const_cast<action_slot*>(std::as_const(*this).find(handle)); }

    // event handlers --------------------------------------------------------------------------------------------------

    void controller::on_key_event(GLT::key_event& key_event) {

        const auto key_code = key_event.get_key_code();

        if (!is_valid_button_code(key_code))
            return;

        m_raw.button_states[static_cast<size_t>(key_code)] = key_event.get_key_state();
    }


    void controller::on_mouse_event(GLT::mouse_event& mouse_event) {

        switch (mouse_event.get_action_type()) {

            case GLT::mouse_event::action_type::move:       m_raw.mouse_delta += mouse_event.get_delta(); break;
            case GLT::mouse_event::action_type::scroll:     m_raw.mouse_scroll += mouse_event.get_delta(); break;
            case GLT::mouse_event::action_type::enter:      m_raw.mouse_entered = mouse_event.get_entered(); break;
            default:                                        break;
        }
    }

    // source / modifier / trigger evaluation --------------------------------------------------------------------------

    glm::vec3 controller::eval_source(const source& input_source) const {

        glm::vec3 value{0.0f};

        std::visit([&](const auto& payload) {

            using payload_type = std::decay_t<decltype(payload)>;

            if constexpr (std::is_same_v<payload_type, source_key>) {

                value.x = is_key_down(payload.code) ? 1.0f : 0.0f;

            } else if constexpr (std::is_same_v<payload_type, source_mouse_btn>) {

                value.x = is_key_down(payload.code) ? 1.0f : 0.0f;

            } else if constexpr (std::is_same_v<payload_type, source_mouse_move>) {

                value.x = m_raw.mouse_delta.x;
                value.y = m_raw.mouse_delta.y;

            } else if constexpr (std::is_same_v<payload_type, source_mouse_wheel>) {

                value.x = m_raw.mouse_scroll.x;
                value.y = m_raw.mouse_scroll.y;
            }

            // gamepad arms: add when you add the source types

        }, input_source.payload);

        value *= input_source.scale;

        if (input_source.negate)
            value = -value;

        return value;
    }


    glm::vec3 controller::apply_mods(glm::vec3 value, const std::vector<modifier>& modifiers) const {

        u8 cursor_axis_index = 0;

        for (const auto& modifier : modifiers) {

            const u8 target_axis =
                (modifier.axis_idx == cursor_axis) ? cursor_axis_index : modifier.axis_idx;

            switch (modifier.type) {

                case modifier_type::scalar:
                    value *= modifier.scalar;
                    break;

                case modifier_type::invert:
                    value[target_axis] = -value[target_axis];
                    break;

                case modifier_type::axis_scale:
                    value[target_axis] *= modifier.scalar;
                    break;

                case modifier_type::swap:
                    std::swap(value[modifier.axis_idx], value[modifier.axis_idx_b]);
                    break;

                case modifier_type::axis:
                    value[modifier.axis_idx] = value.x;
                    if (modifier.axis_idx != 0)
                        value.x = 0.0f;

                    cursor_axis_index = modifier.axis_idx;
                    break;

                case modifier_type::deadzone: {
                    const f32 magnitude = glm::length(glm::vec2{value.x, value.y});

                    if (magnitude <= modifier.deadzone_val) {
                        value.x = 0.0f;
                        value.y = 0.0f;
                    } else {
                        const f32 scaled_magnitude =
                            (magnitude - modifier.deadzone_val) / (1.0f - modifier.deadzone_val);

                        value.x *= scaled_magnitude / magnitude;
                        value.y *= scaled_magnitude / magnitude;
                    }
                    break;
                }

                case modifier_type::require_key:
                    if (!is_key_down(modifier.key))
                        value = glm::vec3{0.0f};
                    break;

                case modifier_type::block_key:
                    if (is_key_down(modifier.key))
                        value = glm::vec3{0.0f};
                    break;

                default:
                    break;
            }
        }

        return value;
    }


    bool controller::eval_trigger(const trigger& trigger, trigger_runtime& runtime, f32 magnitude, f32 delta_time) {

        const bool is_active = magnitude >= trigger.threshold;
        bool fired = false;

        switch (trigger.type) {

            case trigger_type::down:        fired = is_active; break;
            case trigger_type::pressed:     fired = is_active && !runtime.was_active; break;
            case trigger_type::released:    fired = !is_active && runtime.was_active; break;

            case trigger_type::hold:
                if (is_active) {
                    runtime.timer += delta_time;
                    if (runtime.timer >= trigger.duration && !runtime.fired) {
                        fired = true;
                        runtime.fired = true;
                    }
                } else {
                    runtime.timer = 0.0f;
                    runtime.fired = false;
                }
                break;

            case trigger_type::tap:
                if (is_active) {
                    runtime.timer += delta_time;
                } else if (runtime.was_active) {
                    fired = runtime.timer <= trigger.duration;
                    runtime.timer = 0.0f;
                }
                break;

            case trigger_type::pulse:
                if (is_active) {
                    runtime.timer += delta_time;
                    if (runtime.timer >= trigger.interval) {
                        runtime.timer -= trigger.interval;
                        fired = true;
                    }
                } else {
                    runtime.timer = 0.0f;
                }
                break;

            default:
                break;
        }

        runtime.was_active = is_active;
        return fired;
    }

}
