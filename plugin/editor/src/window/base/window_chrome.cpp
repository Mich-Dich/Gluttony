#include "util/pch.h"
#include "window_chrome.h"

#include <imgui_internal.h>

#include "config/imgui_config.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor::chrome {

    // CONSTANTS =======================================================================================================

    constexpr f32                               HEADER_PADDING_Y = 7.f;         // above / below the header's frame height

    constexpr f32                               HEADER_PADDING_X = 10.f;

    constexpr f32                               FOOTER_PADDING_X = 14.f;

    constexpr f32                               FOOTER_PADDING_Y = 9.f;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // "/very/long/path/file.ext" -> ".../path/file.ext" so it fits into [max_width]
    static std::string truncate_left(const char* text, f32 max_width);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static std::string truncate_left(const char* text, const f32 max_width) {

        std::string out = text;
        if (ImGui::CalcTextSize(out.c_str()).x <= max_width)
            return out;

        while (out.size() > 4 && ImGui::CalcTextSize(("..." + out).c_str()).x > max_width)
            out.erase(0, 1);
        return "..." + out;
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    header_bar begin_header_bar(const icon_manager::icon_type icon, const char* title, const header_options& options) {

        header_bar bar{};
        const f32 frame_h = ImGui::GetFrameHeight();
        bar.min = ImGui::GetCursorScreenPos();
        bar.width = ImGui::GetContentRegionAvail().x;
        bar.height = frame_h + HEADER_PADDING_Y * 2.f;

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(bar.min, ImVec2(bar.min.x + bar.width, bar.min.y + bar.height), ImGui::GetColorU32(ImGuiCol_TitleBg));

        const f32 buttons_x = bar.min.x + bar.width - options.buttons_width - HEADER_PADDING_X;

        // icon
        f32 x = bar.min.x + HEADER_PADDING_X;
        const f32 icon_size = frame_h;
        const ImVec2 icon_min(x, bar.min.y + (bar.height - icon_size) * 0.5f);
        const auto icon_data = icon_manager::get(icon);
        draw_list->AddImage(icon_data.tex_ref, icon_min, ImVec2(icon_min.x + icon_size, icon_min.y + icon_size), icon_data.uv0, icon_data.uv1);
        x += icon_size + 8.f;

        // title
        const f32 text_y = bar.min.y + (bar.height - ImGui::GetTextLineHeight()) * 0.5f;
        ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::bold));
        draw_list->AddText(ImVec2(x, text_y), ImGui::GetColorU32(ImGuiCol_Text), title);
        x += ImGui::CalcTextSize(title).x + 10.f;
        ImGui::PopFont();

        // pill
        if (options.pill && *options.pill)
            x += status_pill(draw_list, ImVec2(x, text_y), options.pill, options.pill_color) + 10.f;

        // subtitle (left-truncated so the end of a path stays readable)
        if (options.subtitle && *options.subtitle && x < buttons_x - 40.f) {
            const std::string subtitle = truncate_left(options.subtitle, buttons_x - 10.f - x);
            draw_list->AddText(ImVec2(x, text_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), subtitle.c_str());
        }

        ImGui::SetCursorScreenPos(ImVec2(buttons_x, bar.min.y + (bar.height - frame_h) * 0.5f));
        return bar;
    }


    void end_header_bar(const header_bar& bar) {

        ImGui::SetCursorScreenPos(ImVec2(bar.min.x, bar.min.y + bar.height));
        ImGui::Dummy(ImVec2(0.f, 0.f));
    }


    f32 status_pill(ImDrawList* draw_list, const ImVec2& pos, const char* text, const ImU32 color) {

        const ImVec2 size = ImGui::CalcTextSize(text);
        const ImVec2 pill_min(pos.x, pos.y - 2.f);
        const ImVec2 pill_max(pos.x + size.x + 22.f, pos.y + size.y + 2.f);
        const ImU32 fill = (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(40) << IM_COL32_A_SHIFT);

        draw_list->AddRectFilled(pill_min, pill_max, fill, (pill_max.y - pill_min.y) * 0.5f);
        draw_list->AddCircleFilled(ImVec2(pos.x + 9.f, pos.y + size.y * 0.5f), 3.f, color);
        draw_list->AddText(ImVec2(pos.x + 16.f, pos.y), color, text);
        return pill_max.x - pill_min.x;
    }


    void status_pill_item(const char* text, const ImU32 color) {

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const f32 y = pos.y + (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f;
        const f32 width = status_pill(ImGui::GetWindowDrawList(), ImVec2(pos.x, y), text, color);
        ImGui::Dummy(ImVec2(width, ImGui::GetFrameHeight()));
    }


    void section_label(const char* text, const char* note) {

        ImGui::Dummy(ImVec2(0.f, 4.f));
        ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::small));
        ImGui::TextDisabled("%s", text);
        if (note && *note) {
            ImGui::SameLine(0.f, 6.f);
            ImGui::TextDisabled("%s", note);
        }
        ImGui::PopFont();
    }


    f32 footer_height() { return ImGui::GetFrameHeight() + FOOTER_PADDING_Y * 2.f; }


    void begin_footer() {

        const f32 height = footer_height();
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const f32 width = ImGui::GetContentRegionAvail().x;
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        draw_list->AddRectFilled(ImVec2(p0.x, p0.y), ImVec2(p0.x + width, p0.y + height), ImGui::GetColorU32(ImVec4(0.f, 0.f, 0.f, 0.15f)));
        draw_list->AddLine(ImVec2(p0.x, p0.y + 0.5f), ImVec2(p0.x + width, p0.y + 0.5f), ImGui::GetColorU32(ImGuiCol_Separator, 0.6f));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(FOOTER_PADDING_X, FOOTER_PADDING_Y));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::BeginChild("##dialog_footer", ImVec2(0.f, height), ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }


    void end_footer() { ImGui::EndChild(); }


    f32 button_width(const char* label) {

        return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.f + 14.f;
    }


    void align_right(const f32 width) {

        const f32 avail = ImGui::GetContentRegionAvail().x;
        if (avail > width)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - width);
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
