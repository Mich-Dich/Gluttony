
#include "util/pch.h"
#include "settings_window.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <config/imgui_config.h>
#include <util/ui/panel_collection.h>


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    namespace {

        constexpr f32                           SIDEBAR_WIDTH = 230.f;
        constexpr f32                           MAX_PAGE_WIDTH = 760.f;             // long rows are hard to read
    }

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    namespace {

        ImVec4 shade(ImVec4 color, const f32 amount) {

            color.x = std::clamp(color.x + amount, 0.f, 1.f);
            color.y = std::clamp(color.y + amount, 0.f, 1.f);
            color.z = std::clamp(color.z + amount, 0.f, 1.f);
            return color;
        }

    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    settings_window::settings_window(const char* title, const settings_registry& registry)
        : m_registry(registry) {

        make_window_name(title);
    }


    settings_window::~settings_window() {

        if (m_dirty)
            m_registry.save_all();
    }


    void settings_window::window(const f32 /*delta_time*/) {

        if (!m_show_window)
            return;

        const auto pages = m_registry.pages();
        if (m_hits.size() != pages.size())
            m_hits.assign(pages.size(), 0);
        if (m_selected_page >= pages.size())
            m_selected_page = 0;

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(980.f, 660.f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(640.f, 360.f), ImVec2(FLT_MAX, FLT_MAX));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        const bool open = ImGui::Begin(m_window_id.c_str(), &m_show_window, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();

        if (open) {

            // Ctrl+F jumps to the search field
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
                m_focus_search = true;

            // ---- sidebar ----
            ImGui::PushStyleColor(ImGuiCol_ChildBg, shade(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg), -0.03f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 12.f));
            ImGui::BeginChild("##sidebar", ImVec2(SIDEBAR_WIDTH, 0.f), ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            render_sidebar();
            ImGui::EndChild();

            // ---- content ----
            ImGui::SameLine(0.f, 0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.f, 20.f));
            ImGui::BeginChild("##content", ImVec2(0.f, 0.f), ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PopStyleVar();
            render_content();
            ImGui::EndChild();
        }
        ImGui::End();

        // Save once the user lets go (not on every frame of a slider drag)
        if (m_dirty && !ImGui::IsAnyItemActive()) {
            m_registry.save_all();
            m_dirty = false;
        }
    }


    void settings_window::update(const f32 /*delta_time*/) {}


    void settings_window::select_page(std::string_view page_name) {

        const auto pages = m_registry.pages();
        for (size_t x = 0; x < pages.size(); ++x) {
            if (pages[x].name == page_name) {
                m_selected_page = x;
                m_search[0] = '\0';
                m_scroll_to_top = true;
                break;
            }
        }
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void settings_window::render_sidebar() {

        const auto pages = m_registry.pages();
        const bool searching = m_search[0] != '\0';

        // search
        if (m_focus_search) {
            ImGui::SetKeyboardFocusHere();
            m_focus_search = false;
        }
        if (UI::search_field("##search", m_search, sizeof(m_search), "Search   (Ctrl+F)"))
            m_scroll_to_top = true;

        ImGui::Dummy(ImVec2(0.f, 4.f));

        // page list
        const f32 row_h = ImGui::GetFrameHeight() + 6.f;
        const f32 width = ImGui::GetContentRegionAvail().x;
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        std::string_view last_group{};

        for (size_t x = 0; x < pages.size(); ++x) {

            const auto& page = pages[x];

            if (page.group != last_group) {

                ImGui::Dummy(ImVec2(0.f, 6.f));
                ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::small));
                std::string upper = page.group;
                std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.f);
                ImGui::TextDisabled("%s", upper.c_str());
                ImGui::PopFont();
                last_group = page.group;
            }

            ImGui::PushID(static_cast<int>(x));
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::InvisibleButton("##page", ImVec2(width, row_h));
            const bool hovered = ImGui::IsItemHovered();
            const bool selected = !searching && x == m_selected_page;
            const ImVec2 max = ImVec2(pos.x + width, pos.y + row_h);

            if (selected) {
                draw_list->AddRectFilled(pos, max, ImGui::GetColorU32(GLT::imgui_config::get_action_color00_faded_ref()), 4.f);
                draw_list->AddRectFilled(ImVec2(pos.x, pos.y + 5.f), ImVec2(pos.x + 3.f, max.y - 5.f),
                    ImGui::GetColorU32(GLT::imgui_config::get_action_color00_hover_ref()), 1.5f);
            } else if (hovered)
                draw_list->AddRectFilled(pos, max, ImGui::GetColorU32(GLT::imgui_config::get_action_color_gray_hover_ref()), 4.f);

            const bool dimmed = searching && m_hits[x] == 0;
            const f32 text_y = pos.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f;
            draw_list->AddText(ImVec2(pos.x + 12.f, text_y), ImGui::GetColorU32(dimmed ? ImGuiCol_TextDisabled : ImGuiCol_Text), page.name.c_str());

            // match count while searching
            if (searching && m_hits[x] > 0) {
                const std::string count = std::to_string(m_hits[x]);
                const f32 count_w = ImGui::CalcTextSize(count.c_str()).x;
                draw_list->AddText(ImVec2(max.x - count_w - 10.f, text_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), count.c_str());
            }

            if (clicked)
                select_page(page.name);

            ImGui::PopID();
        }
    }


    void settings_window::render_content() {

        const auto pages = m_registry.pages();
        if (pages.empty()) {
            ImGui::TextDisabled("No settings registered");
            return;
        }

        if (m_scroll_to_top) {
            ImGui::SetScrollY(0.f);
            m_scroll_to_top = false;
        }

        const f32 width = std::min(ImGui::GetContentRegionAvail().x, MAX_PAGE_WIDTH);
        ImGui::BeginChild("##page_body", ImVec2(width, 0.f), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);

        if (m_search[0] == '\0') {

            // ---- one page ----
            const auto& page = pages[m_selected_page];
            UI::settings_begin_page(page.name.c_str(), {}, page.description.c_str());
            if (m_reset_page) {
                UI::settings_reset_page();
                m_reset_page = false;
            }
            if (page.draw)
                m_dirty |= page.draw();
            UI::settings_end_page();

            render_footer();

        } else {

            // ---- search results from every page ----
            u32 total = 0;
            for (size_t x = 0; x < pages.size(); ++x) {

                UI::settings_begin_page(pages[x].name.c_str(), m_search, pages[x].description.c_str());
                if (pages[x].draw)
                    m_dirty |= pages[x].draw();
                m_hits[x] = UI::settings_end_page();
                total += m_hits[x];

                if (m_hits[x] > 0)
                    ImGui::Dummy(ImVec2(0.f, 12.f));
            }

            if (total == 0) {
                ImGui::Dummy(ImVec2(0.f, 20.f));
                ImGui::TextDisabled("No settings match \"%s\"", m_search);
            }
        }

        ImGui::EndChild();
    }


    void settings_window::render_footer() {

        ImGui::Dummy(ImVec2(0.f, 8.f));

        if (UI::subtle_button("Reset page to defaults"))
            m_reset_page = true;

        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Changes are saved automatically");
    }

}
