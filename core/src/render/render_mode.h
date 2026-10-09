

#pragma once

#include "reflection/annotations.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::render {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // @brief A render output mode the renderer supports
    //
    // Renderer plugins declare one instance per mode inside a namespace; reflection
    // enumerates them at compile time and derives display_name / tooltip / category
    // from annotations. Only `id` and `is_default` need to be spelled out
    struct render_mode {

        u32                         id{};                       // stable; passed to the shader
        bool                        is_default{ false };
    };


    // @brief Runtime description of one render mode, produced from a reflected declaration
    //
    // This is the type the renderer exposes to the editor. It carries no annotation
    // machinery - the editor only reads these fields
    struct render_mode_info {

        u32                         id{};
        const char*                 key{};                      // variable name, e.g. "lit"
        const char*                 display_name{};             // annotation override, else key
        const char*                 tooltip{};                  // may be nullptr / empty
        const char*                 category{};                 // may be nullptr / empty
        bool                        is_default{ false };

        // Convenience for consumers
        [[nodiscard]] bool has_tooltip() const noexcept { return tooltip && tooltip[0]; }
        [[nodiscard]] bool has_category() const noexcept { return category && category[0]; }
    };


    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // @brief Builds a compile-time table of [render_mode_info] from a namespace of [render_mode] variables
    //
    // Skips anything that isn't a non-static [render_mode] variable. Reads display_name, tooltip and category from the 
    // variable's annotations; falls back to the variable name if display_name is absent
    //
    // @tparam Namespace  C++26 reflection value, e.g. ^^GLT::renderer::vk_ray::modes
    // @return A static array of render_mode_info
    template <auto Namespace>
    consteval auto make_render_mode_entries();

    // CLASS DECLARATION ===============================================================================================

}

#include "render_mode.inl"
