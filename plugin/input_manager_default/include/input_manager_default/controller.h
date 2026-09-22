
#pragma once

#include <event/event_bus.h>
#include <event/input_event.h>
#include <world/controller.h>

#include "type.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::input::input_manager_default {

    // CONSTANTS =======================================================================================================

    static constexpr size_t                         BUTTON_STATE_COUNT = 512;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct trigger_runtime {

        bool                                            was_active = false;
        bool                                            fired = false;
        f32                                             timer = .0f;
    };


    struct binding_runtime {

        std::vector<trigger_runtime>                    triggers{};
    };


    struct action_runtime {

        glm::vec3                                       value{ .0f };
        glm::vec3                                       prev_value{ .0f };
        bool                                            active = false;
        bool                                            prev_active = false;
        f32                                             held_time = 0.0f;
        std::vector<binding_runtime>                    bindings{};
    };


    struct raw_state {

        std::array<GLT::key_state, BUTTON_STATE_COUNT>  button_states{};
        glm::vec2                                       mouse_delta{0.0f};
        glm::vec2                                       mouse_scroll{0.0f};
        bool                                            mouse_entered = false;
    };


    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class controller : public GLT::world::controller {
    public:

        controller();
        virtual ~controller();

        DELETE_COPY_CONSTRUCTOR(controller)

        // -- configuration ------------------------------------------------------

        // Register a new action. Returns a stable handle for querying / removal. `action::name` is retained for debug UI and serialization only.
        [[nodiscard]] handle add_action(action action);

        // Remove an action. All outstanding handles for that action become invalid.
        void remove_action(handle handle);

        void clear_actions();

        // -- per-frame step -----------------------------------------------------

        // Evaluate all actions from the raw input snapshot. Derived controllers that override this MUST call controller::update(dt) 
        // somewhere in their own implementation to keep action state in sync.
        virtual void update(f32 dt);

        // -- read-only query surface for scripts --------------------------------

        [[nodiscard]] bool is_active(::handle handle) const;

        [[nodiscard]] bool was_pressed(::handle handle) const;

        [[nodiscard]] bool was_released(::handle handle) const;

        [[nodiscard]] f32 get_axis(::handle handle) const;

        [[nodiscard]] glm::vec2 get_axis2d(::handle handle) const;

        [[nodiscard]] glm::vec3 get_axis3d(::handle handle) const;

        [[nodiscard]] f32 get_held_time(::handle handle) const;

        // -- raw access (rare) --------------------------------------------------

        [[nodiscard]] bool is_key_down(GLT::key_code code) const;

        [[nodiscard]] bool is_mouse_down(GLT::key_code code) const;

    private:

        struct action_slot {

            action                                      act;
            action_runtime                              runtime;
            u32                                         generation = 1;      // 0 is reserved for INVALID_HANDLE
            bool                                        alive = false;
        };

        // Handle packing: [ 32-bit generation | 32-bit index ]
        static constexpr u32                            HANDLE_INDEX_BITS = 32;

        static constexpr u64                            HANDLE_INDEX_MASK = 0xFFFFFFFFull;

        [[nodiscard]] static constexpr ::handle pack(u32 index, u32 generation) noexcept {

            return (static_cast<handle>(generation) << HANDLE_INDEX_BITS) | static_cast<handle>(index);
        }

        [[nodiscard]] static constexpr u32 handle_index(::handle handle) noexcept { return static_cast<u32>(handle & HANDLE_INDEX_MASK); }

        [[nodiscard]] static constexpr u32 handle_generation(::handle handle) noexcept { return static_cast<u32>(handle >> HANDLE_INDEX_BITS); }

        [[nodiscard]] const action_slot* find(::handle handle) const;

        [[nodiscard]] action_slot* find(::handle handle);

        // Event handlers — never mark events as handled; other subscribers
        // (debug overlays, replay recorders, …) may still want them.
        void on_key_event(GLT::key_event& event);

        void on_mouse_event(GLT::mouse_event& event);

        [[nodiscard]] glm::vec3 eval_source(const source& src) const;

        [[nodiscard]] glm::vec3 apply_mods(glm::vec3 value, const std::vector<modifier>& mods) const;

        [[nodiscard]] bool eval_trigger(const trigger& trig, trigger_runtime& runtime_trig, f32 magnitude, f32 delta_timet);

        [[nodiscard]] static constexpr bool is_valid_button_code(GLT::key_code code) noexcept {

            const auto raw_code = static_cast<int>(code);
            return raw_code >= 0 && static_cast<size_t>(raw_code) < BUTTON_STATE_COUNT;
        }

        std::vector<action_slot>                        m_slots{};
        std::vector<u32>                                m_free_indices{};
        raw_state                                       m_raw{};
        GLT::event_bus::subscription_guard              m_key_sub{};
        GLT::event_bus::subscription_guard              m_mouse_sub{};

    };

}
