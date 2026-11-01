
#include "util/pch.h"
#include "UI.h"

#include <imgui.h>

#include "../../../core/src/util/ui/imgui_markdown.h"
#include "../../core/src/config/imgui_config.h"

#include "util/description.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::launcher {

    // CONSTANTS =======================================================================================================

    constexpr float                             CORNER_SIZE_MAX = 25.f;

    constexpr float                             LEADING_BLOCK_WIDTH = 150.f;

    constexpr float                             TOP_BAR_HEIGHT = 56.f;

    constexpr float                             SIDE_BAR_WIDTH = 240.f;

    constexpr ImGuiWindowFlags                  PANEL_FLAGS = ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    std::filesystem::path find_asset_root(const std::filesystem::path& hint);


    void try_load(image& img, const std::filesystem::path& p);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // Resolve a usable "asset" root from a hint path. Walks up a few levels so
    // the launcher also works when run from build/bin/Release/... in an IDE.
    std::filesystem::path find_asset_root(const std::filesystem::path& hint) {

        std::error_code error{};
        if (std::filesystem::exists(hint / "image", error) && !error)
            return hint;

        auto cur = hint;
        for (int i = 0; i < 5; ++i) {
            if (cur.has_parent_path() == false)
                break;

            cur = cur.parent_path();
            if (std::filesystem::exists(cur / "asset" / "image", error) && !error)
                return cur / "asset";

            if (std::filesystem::exists(cur / "image", error) && !error)
                return cur;
        }
        return hint; // caller will simply not load anything
    }


    void try_load(image& img, const std::filesystem::path& p) {

        std::error_code error{};
        if (std::filesystem::exists(p, error) && !error)
            img.load_from_file(p);
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    void UI::init(const std::filesystem::path& asset_dir) {

        const auto root = find_asset_root(asset_dir);
        const auto img_dir = root / "image";
        GLT::imgui_config::update_ui_colors(ImVec4(0, 1, 0, 1));
        try_load(m_logo, img_dir / "logo.png");
        try_load(m_hero, img_dir / "logo_panel_0.png");
        try_load(m_about_art, img_dir / "logo_panel_1.png");
    }


    void UI::draw(bool& running) {

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const ImVec2 origin = vp->WorkPos;
        const ImVec2 size = vp->WorkSize;

        const ImVec2 top_pos = origin;
        const ImVec2 top_size = ImVec2(size.x, TOP_BAR_HEIGHT);
        const ImVec2 side_pos = ImVec2(origin.x, origin.y + TOP_BAR_HEIGHT);
        const ImVec2 side_size = ImVec2(SIDE_BAR_WIDTH, size.y - TOP_BAR_HEIGHT);
        const ImVec2 content_pos = ImVec2(origin.x + SIDE_BAR_WIDTH, origin.y + TOP_BAR_HEIGHT);
        const ImVec2 content_size = ImVec2(size.x - SIDE_BAR_WIDTH, size.y - TOP_BAR_HEIGHT);

        ImGui::SetNextWindowPos(top_pos);
        ImGui::SetNextWindowSize(top_size);
        ImGui::Begin("##top_bar", nullptr, PANEL_FLAGS);
        draw_top_bar(running);
        ImGui::End();

        ImGui::SetNextWindowPos(side_pos);
        ImGui::SetNextWindowSize(side_size);
        ImGui::Begin("##side_bar", nullptr, PANEL_FLAGS);
        draw_side_bar();
        ImGui::End();

        ImGui::SetNextWindowPos(content_pos);
        ImGui::SetNextWindowSize(content_size);
        ImGui::Begin("##content", nullptr, PANEL_FLAGS);
        draw_content();
        ImGui::End();
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void UI::draw_top_bar(bool& running) {

        const ImGuiStyle& style = ImGui::GetStyle();
        const float pad_y = style.WindowPadding.y;
        const float row_h = ImGui::GetWindowHeight() - 2.f * pad_y;
        const float text_h = ImGui::GetTextLineHeight();
        const float center_y = pad_y + (row_h - text_h) * 0.5f;

        // Left side: logo + title
        if (m_logo.is_valid()) {
            const float aspect = static_cast<float>(m_logo.width()) / static_cast<float>(m_logo.height());
            const float logo_h = row_h - 8.f;
            const float logo_w = logo_h * aspect;

            ImGui::SetCursorPosY(pad_y + (row_h - logo_h) * 0.5f);
            ImGui::Image(m_logo.tex_id(), ImVec2(logo_w, logo_h));
            ImGui::SameLine();
        }

        ImGui::SetCursorPosY(center_y);
        ImGui::TextUnformatted("Gluttony Launcher");
    }


    void UI::draw_side_bar() {

        struct entry { const char* label; page target; };
        static const entry entries[] = {
            { "Home",     page::home },
            { "Projects", page::projects },
            { "Settings", page::settings },
            { "About",    page::about },
        };

        ImGui::Dummy(ImVec2(0.f, 4.f));
        for (const entry& e : entries) {
            const bool selected = (m_page == e.target);
            if (ImGui::Selectable(e.label, selected, ImGuiSelectableFlags_None, ImVec2(0.f, 34.f)))
                m_page = e.target;
        }

        // Version footer pinned to the bottom of the sidebar.
        const ImGuiStyle& style = ImGui::GetStyle();
        const float footer_h = ImGui::GetTextLineHeightWithSpacing() * 2.f + 16.f;
        const float y = ImGui::GetWindowHeight() - footer_h - style.WindowPadding.y;
        if (y > ImGui::GetCursorPosY()) {
            ImGui::SetCursorPosY(y);
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextDisabled("Gluttony Engine");
            ImGui::TextDisabled("Launcher v0.0 (dev)");
        }
    }


    void UI::draw_content() {

        switch (m_page) {
            case page::home:        draw_home_page(); break;
            case page::projects:    draw_projects_page(); break;
            case page::settings:    draw_settings_page(); break;
            case page::about:       draw_about_page(); break;
        }
    }


    void UI::draw_home_page() {

        if (m_about_art.is_valid()) {

            const f32 avail_w = ImGui::GetContentRegionAvail().x;
            const f32 aspect = static_cast<float>(m_hero.width()) / static_cast<float>(m_hero.height());
            f32 weight = avail_w * 0.5f;
            f32 height = weight / aspect;
            if (height > 170.f) {
                height = 170.f;
                weight = height * aspect;
            }

            const auto screen_pos = ImGui::GetCursorScreenPos();
            auto draw = ImGui::GetWindowDrawList();
            const f32 corner_size = GLT::math::clamp<f32>(CORNER_SIZE_MAX, 0, height / 4);
            draw->AddTriangleFilled(screen_pos + ImVec2(corner_size, 0), screen_pos + ImVec2(corner_size, corner_size),
                screen_pos + ImVec2(0, corner_size), IM_COL32(0, 0, 0, 255));
            draw->AddTriangleFilled(screen_pos + ImVec2(corner_size, height - corner_size), screen_pos + ImVec2(corner_size, height),
                screen_pos + ImVec2(0, height - corner_size), IM_COL32(0, 0, 0, 255));
            draw->AddRectFilled(screen_pos + ImVec2(corner_size, 0), screen_pos + ImVec2(corner_size + height, height), IM_COL32(0, 0, 0, 255));
            draw->AddRectFilled(screen_pos + ImVec2(0, corner_size), screen_pos + ImVec2(corner_size, height - corner_size),
                IM_COL32(0, 0, 0, 255));

            ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(height, 0));
            ImGui::Image(m_hero.tex_id(), ImVec2(weight, height));
            ImGui::SameLine();
            const auto screen_pos_after_image = ImGui::GetCursorScreenPos();
            draw->AddRectFilled(screen_pos_after_image - ImVec2(10, 0),
                screen_pos_after_image + ImVec2(ImGui::GetContentRegionAvail().x, height), IM_COL32(0, 0, 0, 255));
            ImGui::Dummy(ImVec2(0, 0));
        }

        ImGui::BeginChild("##launcher::general_gluttony_description");
        GLT::UI::markdown(GLT::launcher::general_gluttony_description);
        ImGui::EndChild();
    }


    void UI::draw_projects_page() {

        ImGui::TextWrapped("No projects loaded. Create one to get started.");
        ImGui::Spacing();
        ImGui::Spacing();

        const ImGuiStyle& style = ImGui::GetStyle();
        constexpr int cols = 3;
        const float avail_w = ImGui::GetContentRegionAvail().x;
        const float card_w = (avail_w - style.ItemSpacing.x * (cols - 1)) / cols;
        const float card_h = 120.f;

        for (int i = 0; i < cols; ++i) {
            if (i > 0) ImGui::SameLine();
            ImGui::PushID(i);
            ImGui::BeginChild("##card", ImVec2(card_w, card_h), true);
            ImGui::TextDisabled("Empty slot");
            ImGui::Spacing();
            ImGui::Text("Project %d", i + 1);
            ImGui::EndChild();
            ImGui::PopID();
        }
    }


    void UI::draw_settings_page() {

        ImGui::TextWrapped("Launcher settings will appear here.");
        ImGui::Spacing();
        ImGui::TextDisabled("\xE2\x80\xA2 Theme");
        ImGui::TextDisabled("\xE2\x80\xA2 Language");
        ImGui::TextDisabled("\xE2\x80\xA2 Asset paths");
    }


    void UI::draw_about_page() {

        if (m_about_art.is_valid()) {
            const float avail_w = ImGui::GetContentRegionAvail().x;
            const float aspect = static_cast<float>(m_about_art.width()) / static_cast<float>(m_about_art.height());
            float w = avail_w * 0.5f;
            float h = w / aspect;
            if (h > 200.f) {
                h = 200.f;
                w = h * aspect;
            }

            ImGui::Image(m_about_art.tex_id(), ImVec2(w, h));
            ImGui::Spacing();
        }

        ImGui::TextUnformatted("Gluttony Engine");
        ImGui::TextDisabled("Launcher v0.0 (dev)");
        ImGui::Spacing();
        ImGui::TextWrapped("The Gluttony Engine is an experimental game engine focused on data-driven workflows.");
    }

}
