
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::input::input_manager_default {

    // CONSTANTS =======================================================================================================

    // Sentinel used by modifiers that operate on the "current axis" cursor rather than on a fixed index.
    inline constexpr u8                                 cursor_axis = 0xFF;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    enum class value_type : u8 {

        boolean = 0,
        axis1d,
        axis2d,
        axis3d
    };

    // SOURCES ---------------------------------------------------------------------------------------------------------

    struct source_key { GLT::key_code code{}; };

    struct source_mouse_btn { GLT::key_code code = GLT::key_code::mouse_bu_left; };

    struct source_mouse_move {};                        // contributes x, y

    struct source_mouse_wheel {};                       // contributes x, y

    struct source_gamepad_btn { u8 button = 0; };

    struct source_gamepad_axis { u8 axis = 0; };

    using source_payload = std::variant<source_key, source_mouse_btn, source_mouse_move, source_mouse_wheel, 
        source_gamepad_btn, source_gamepad_axis>;


    struct source {

        source_payload                                  payload = source_key{};
        f32                                             scale   = 1.f;
        bool                                            negate  = false;

        static source key(GLT::key_code code, f32 scale = 1.f, bool negate = false) { return { source_key{code}, scale, negate }; }
        static source mouse_button(GLT::key_code code)                              { return { source_mouse_btn{code}, 1.f, false }; }
        static source mouse_move(f32 scale = 1.f)                                   { return { source_mouse_move{}, scale, false }; }
        static source mouse_wheel(f32 scale = 1.f)                                  { return { source_mouse_wheel{}, scale, false }; }
    };

    // MODIFIERS -------------------------------------------------------------------------------------------------------

    enum class modifier_type : u8 {

        scalar = 0,                                     // multiply every component
        axis,                                           // route the scalar source value to axis N, set cursor = N
        invert,                                         // negate cursor (or explicit) axis
        axis_scale,                                     // multiply cursor (or explicit) axis
        swap,                                           // swap two explicit axes
        deadzone,                                       // radial deadzone on xy
        require_key,                                    // zero the value unless key held (chord)
        block_key,                                      // zero the value if key held (chord guard)
    };


    struct modifier {

        modifier_type                                   type = modifier_type::scalar;
        f32                                             scalar = 1.f;
        u8                                              axis_idx = cursor_axis;      // cursor_axis -> use current cursor
        u8                                              axis_idx_b = 1;
        f32                                             deadzone_val = .2f;
        GLT::key_code                                   key = GLT::key_code::key_unknown;

        // Cursor-aware factories
        static modifier axis(u8 idx)                    { return { modifier_type::axis,       1.f, idx }; }
        static modifier invert()                        { return { modifier_type::invert }; }
        static modifier invert(u8 idx)                  { return { modifier_type::invert,     1.f, idx }; }
        static modifier scale(f32 s)                    { return { modifier_type::axis_scale, s }; }
        static modifier scale(u8 idx, f32 s)            { return { modifier_type::axis_scale, s, idx }; }

        // Whole-vector / explicit factories
        static modifier all(f32 s)                      { return { modifier_type::scalar,      s }; }
        static modifier swap(u8 a, u8 b)                { return { modifier_type::swap,        1.f, a, b }; }
        static modifier deadzone(f32 dz)                { return { modifier_type::deadzone,    1.f, cursor_axis, 1, dz }; }
        static modifier require(GLT::key_code k)        { return { modifier_type::require_key, 1.f, cursor_axis, 1, .0f, k }; }
        static modifier block(GLT::key_code k)          { return { modifier_type::block_key,   1.f, cursor_axis, 1, .0f, k }; }
    };

    // TRIGGERS --------------------------------------------------------------------------------------------------------

    enum class trigger_type : u8 {

        down = 0,
        pressed,
        released,
        hold,
        tap,
        pulse,
    };


    struct trigger {

        trigger_type                                    type = trigger_type::down;
        f32                                             threshold = .5f;
        f32                                             duration = .2f;
        f32                                             interval = .1f;

        static trigger down(f32 t = .5f)                { return { trigger_type::down, t }; }
        static trigger key_down(f32 t = .5f)            { return { trigger_type::down, t }; }   // alias
        static trigger pressed()                        { return { trigger_type::pressed }; }
        static trigger released()                       { return { trigger_type::released }; }
        static trigger hold(f32 d)                      { return { trigger_type::hold,  .5f, d }; }
        static trigger tap(f32 d)                       { return { trigger_type::tap,   .5f, d }; }
        static trigger pulse(f32 i)                     { return { trigger_type::pulse, .5f, .2f, i }; }
    };

    // ACTION + BINDING ------------------------------------------------------------------------------------------------

    struct binding {

        source                                          src{};
        std::vector<modifier>                           modifiers{};
        std::vector<trigger>                            triggers{};   // empty -> passthrough
    };


    struct action {

        std::string                                     name{};       // debug / serialization only, no lookup by name
        value_type                                      type = value_type::boolean;
        std::vector<binding>                            bindings{};
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
