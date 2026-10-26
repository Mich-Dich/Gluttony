
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::platform {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // @brief Which platform library backs this window implementation
    enum class backend_api : u8 {

        glfw = 0,
        sdl,
    };


    // @brief The window's current size category
    enum class window_size_state : u8 {

        windowed,
        minimized,
        fullscreen,
        fullscreen_windowed,
    };


    // @brief How the OS cursor behaves while the window is focused
    enum class cursor_mode : u8 {

        cursor_normal = 0,
        cursor_hidden,
        cursor_disabled,
        cursor_captured,
    };


    // @brief Everything needed to create (and later persist) a window
    //
    // Fields are plain POD so this can be round-tripped through the config serializer without a custom codec
    struct window_attributes {

        std::string                         title = "Gluttony";
        u32                                 width = 1600;
        u32                                 height = 900;
        u32                                 pos_x = 100;
        u32                                 pos_y = 100;
        bool                                vsync = false;
        window_size_state                   size_state = window_size_state::windowed;
    };


    // @brief Opaque handle identifying one specific window created by [i_window_plugin]
    //
    // Packing [index] with a [generation] counter gives:
    //   - O(1) lookup: [index] is a direct slot into the plugin's vector
    //   - stale-handle detection: if the slot is recycled after [destroy()], the new window gets a
    //     bumped generation; any handle to the old window fails the generation check
    //
    // Handles are trivially copyable (8 bytes). A default-constructed handle is invalid
    struct window_handle {

        u32                                 index = std::numeric_limits<u32>::max();
        u32                                 generation = 0;

        [[nodiscard]] constexpr bool is_valid() const noexcept { return index != std::numeric_limits<u32>::max(); }

        constexpr bool operator==(const window_handle&) const noexcept = default;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}


namespace std {

    template<>
    struct hash<GLT::platform::window_handle> {

        [[nodiscard]] std::size_t operator()(const GLT::platform::window_handle& h) const noexcept {

            const u64 combined = (static_cast<u64>(h.index) << 32) | static_cast<u64>(h.generation);
            return hash<u64>{}(combined);
        }
    };

}
