#pragma once

#include <imgui.h>

#include "resource_manager/icon_manager.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor::chrome {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Geometry of a header bar between begin_header_bar() and end_header_bar()
    struct header_bar {

        ImVec2                                  min{};
        f32                                     width = 0.f;
        f32                                     height = 0.f;
    };


    struct header_options {

        const char*                             subtitle = nullptr;         // dim text after the title (paths are left-truncated)
        const char*                             pill = nullptr;             // status pill after the title ("Unsaved changes")
        ImU32                                   pill_color = IM_COL32(230, 180, 60, 255);
        f32                                     buttons_width = 0.f;        // room reserved on the right for the caller's buttons
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // @brief Header strip in the title-bar color, the same one the mesh / material editors use: icon, bold title, optional
    //        status pill and a dim subtitle. Leaves the cursor at the reserved button area (vertically centered), so the
    //        caller draws its buttons with SameLine(0, 6) and then calls end_header_bar()
    header_bar begin_header_bar(icon_manager::icon_type icon, const char* title, const header_options& options = {});

    // @brief Moves the cursor below the header bar
    void end_header_bar(const header_bar& bar);

    // @brief Rounded pill with a colored dot. [color] is the dot / text color, the fill is a faint version of it
    // @return width of the pill
    f32 status_pill(ImDrawList* draw_list, const ImVec2& pos, const char* text, ImU32 color);

    // @brief The same pill as an ImGui item at the cursor (for use inside layouts)
    void status_pill_item(const char* text, ImU32 color);

    // @brief Small caps section title in the dim text color ("FACTORY OPTIONS"), with optional dim count / note on the right
    void section_label(const char* text, const char* note = nullptr);

    // @brief Height of a dialog footer (one row of buttons plus padding)
    [[nodiscard]] f32 footer_height();

    // @brief Footer pinned to the bottom of a dialog: thin top line, slightly darker background, padded child. Call when
    //        exactly footer_height() is left; always pair with end_footer()
    void begin_footer();
    void end_footer();

    // @brief Width of a button with this label (frame padding + extra room so the labels breathe)
    [[nodiscard]] f32 button_width(const char* label);

    // @brief Moves the cursor so [width] pixels of buttons end at the right edge of the current line
    void align_right(f32 width);

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
