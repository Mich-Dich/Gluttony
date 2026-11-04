
#include "util/pch.h"
#include "UI.h"

#include <imgui.h>

#include "../../../core/src/util/ui/imgui_markdown.h"
#include "../../../core/src/util/io/serializer_yaml.h"
#include "../../../core/src/config/project.h"
#include "../../core/src/config/imgui_config.h"

#include "util/description.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::launcher {

    // CONSTANTS =======================================================================================================
    constexpr f32                               CORNER_SIZE_MAX = 45.f;

    constexpr f32                               LEADING_BLOCK_WIDTH = 150.f;

    constexpr f32                               TOP_BAR_HEIGHT = 180.f;

    constexpr f32                               SIDE_BAR_WIDTH = 240.f;

    constexpr f32                               PROJECT_CARD_MIN_WIDTH = 360.f;

    constexpr f32                               PROJECT_CARD_SPACING = 12.f;

    constexpr f32                               PROJECT_CARD_CORNER = 6.f;

    constexpr f32                               PROJECT_CARD_TEXT_PADDING = 10.f;

    constexpr f32                               PROJECT_CARD_TEXT_GAP = 4.f;

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


    // Truncate `text` with an ellipsis so it fits within `max_width` at the current font
    std::string elide_text(const std::string& text, f32 max_width);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // Best-effort textual representation of an arbitrary value, so we don't have to enumerate every
    // concrete type used by GLT::project (version, UUID, system_time, ...)
    template<typename T>
    std::string fmt_val(const T& v);

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // Resolve a usable "asset" root from a hint path. Walks up a few levels so
    // the launcher also works when run from build/bin/Release/... in an IDE
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


    std::string elide_text(const std::string& text, f32 max_width) {

        if (text.empty() || max_width <= 0.0f)
            return text;

        if (ImGui::CalcTextSize(text.c_str()).x <= max_width)
            return text;

        std::string result = text;
        while (!result.empty()) {
            result.pop_back();
            if (ImGui::CalcTextSize((result + "...").c_str()).x <= max_width)
                return result + "...";
        }
        return "...";
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // Best-effort textual representation of an arbitrary value, so we don't have to enumerate every
    // concrete type used by GLT::project (version, UUID, system_time, ...).
    template<typename T>
    std::string fmt_val(const T& v) {

        using U = std::decay_t<T>;

        if constexpr (std::is_same_v<U, std::filesystem::path>)
            return v.generic_string();
        else if constexpr (std::is_same_v<U, std::string>)
            return v;
        else if constexpr (std::is_arithmetic_v<U>)
            return std::to_string(v);
        else if constexpr (std::is_enum_v<U>)
            return std::to_string(static_cast<std::underlying_type_t<U>>(v));
        else if constexpr (requires { v.to_string(); })
            return v.to_string();
        else if constexpr (requires(std::ostream& os, const U& x) { os << x; }) {
            std::ostringstream oss;
            oss << v;
            return oss.str();
        }
        else
            return "<unformatted>";
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    UI::UI() = default;


    UI::~UI() = default;

    // CLASS PUBLIC ====================================================================================================

    void UI::init(const std::filesystem::path& asset_dir) {

        const auto root = find_asset_root(asset_dir);
        const auto img_dir = root / "image";
        GLT::imgui_config::update_ui_colors(ImVec4(0, 1, 0, 1));
        try_load(m_logo, img_dir / "logo.png");
        try_load(m_hero, img_dir / "logo_panel_0.png");
        try_load(m_about_art, img_dir / "logo_panel_1.png");

        m_projects = {
            { "Voxel Sandbox", "Endless procedurally generated voxel worlds", "0.1.0", "Sandbox" },
            { "Ray Tracer Demo", "Real-time hardware ray traced scene viewer", "0.3.2", "Graphics" },
            { "Platformer Test", "A small 2D platformer prototype", "0.0.1", "Prototype" },
            { "Space Explorer", "Procedurally generated space exploration sandbox", "0.0.5", "Exploration" },
            { "Roguelike Prototype", "Turn-based dungeon crawler with procedural maps",  "0.2.0", "Roguelike" },
            { "Test Project", "A Simple test project, manually created", "0.0.0", "Test", "", "/home/mich/workspace/gluttony_test_project" },
        };

        // Load any per-project thumbnails that have been specified
        for (const auto& p : m_projects) {
            if (p.thumbnail_path.empty())
                continue;
            auto img = std::make_unique<image>();
            try_load(*img, img_dir / p.thumbnail_path);
            if (img->is_valid())
                m_project_images[p.name] = std::move(img);
        }
    }


    void UI::draw(bool& running) {

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const ImVec2 origin = vp->WorkPos;
        const ImVec2 size = vp->WorkSize;
        const ImGuiStyle& style = ImGui::GetStyle();

        // Compute banner dimensions from the viewport, NOT from an active window
        // We need these before Begin() so the top bar can size itself correctly
        f32 banner_w = 0.f;
        f32 banner_h = 0.f;
        if (m_hero.is_valid()) {
            const f32 content_w = size.x - style.WindowPadding.x * 2.f;
            const f32 aspect = static_cast<f32  >(m_hero.width()) / static_cast<f32   >(m_hero.height());
            banner_w = content_w * 0.5f;
            banner_h = banner_w / aspect;
            if (banner_h > 150.f) {
                banner_h = 150.f;
                banner_w = banner_h * aspect;
            }
        }

        // The top bar hugs the banner. Padding above and below is added back
        const f32 top_bar_height = banner_h + style.WindowPadding.y * 2.f;

        const ImVec2 top_pos = origin;
        const ImVec2 top_size = ImVec2(size.x, top_bar_height);
        const ImVec2 side_pos = ImVec2(origin.x, origin.y + top_bar_height);
        const ImVec2 side_size = ImVec2(SIDE_BAR_WIDTH, size.y - top_bar_height);
        const ImVec2 content_pos = ImVec2(origin.x + SIDE_BAR_WIDTH, origin.y + top_bar_height);
        const ImVec2 content_size = ImVec2(size.x - SIDE_BAR_WIDTH, size.y - top_bar_height);

        ImGui::SetNextWindowPos(top_pos);
        ImGui::SetNextWindowSize(top_size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2());
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        ImGui::Begin("##top_bar", nullptr, PANEL_FLAGS);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        draw_top_bar(running, banner_h, banner_w);
        ImGui::End();

        ImGui::SetNextWindowPos(side_pos);
        ImGui::SetNextWindowSize(side_size);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        ImGui::Begin("##side_bar", nullptr, PANEL_FLAGS);
        ImGui::PopStyleColor();
        draw_side_bar();
        ImGui::End();

        ImGui::SetNextWindowPos(content_pos);
        ImGui::SetNextWindowSize(content_size);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        ImGui::Begin("##content", nullptr, PANEL_FLAGS);
        ImGui::PopStyleColor();
        draw_content();
        ImGui::End();
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void UI::draw_top_bar(bool& running, const f32 height, const f32 weight) {

        if (height <= 0.f || weight <= 0.f || !m_hero.is_valid())
            return;

        const auto screen_pos = ImGui::GetCursorScreenPos();
        auto draw = ImGui::GetWindowDrawList();
        const f32 corner_size = GLT::math::clamp<f32>(CORNER_SIZE_MAX, 0, height / 4);
        draw->AddTriangleFilled(screen_pos + ImVec2(corner_size, height - corner_size), screen_pos + ImVec2(corner_size, height),
            screen_pos + ImVec2(0, height - corner_size), IM_COL32(0, 0, 0, 255));
        draw->AddRectFilled(screen_pos + ImVec2(corner_size, 0), screen_pos + ImVec2(corner_size + height, height), IM_COL32(0, 0, 0, 255));
        draw->AddRectFilled(screen_pos, screen_pos + ImVec2(corner_size, height - corner_size), IM_COL32(0, 0, 0, 255));

        ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(height, 0));
        ImGui::Image(m_hero.tex_id(), ImVec2(weight, height));
        ImGui::SameLine();
        const auto screen_pos_after_image = ImGui::GetCursorScreenPos();
        draw->AddRectFilled(screen_pos_after_image - ImVec2(10, 0),
            screen_pos_after_image + ImVec2(ImGui::GetContentRegionAvail().x, height), IM_COL32(0, 0, 0, 255));
        ImGui::Dummy(ImVec2(0, 0));
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
            const bool selected = (m_page == e.target) || (e.target == page::projects && m_page == page::project_details);
            if (ImGui::Selectable(e.label, selected, ImGuiSelectableFlags_None, ImVec2(0.f, 34.f)))
                m_page = e.target;
        }

        // Version footer pinned to the bottom of the sidebar
        const ImGuiStyle& style = ImGui::GetStyle();
        const f32   footer_h = ImGui::GetTextLineHeightWithSpacing() * 2.f + 16.f;
        const f32   y = ImGui::GetWindowHeight() - footer_h - style.WindowPadding.y;
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
            case page::home:            draw_home_page(); break;
            case page::projects:        draw_projects_page(); break;
            case page::project_details: draw_project_details_page(); break;
            case page::settings:        draw_settings_page(); break;
            case page::about:           draw_about_page(); break;
        }
    }


    void UI::draw_home_page() {

        ImGui::BeginChild("##launcher::general_gluttony_description");
        GLT::UI::markdown(GLT::launcher::general_gluttony_description);
        ImGui::EndChild();
    }


    void UI::draw_projects_page() {

        if (m_projects.empty()) {
            ImGui::TextWrapped("No projects loaded. Create one to get started.");
            return;
        }

        // The content panel has no scrollbar of its own — give the grid a scrolling child
        ImGui::BeginChild("##projects_scroll");
        render_project_grid();
        ImGui::EndChild();
    }


    void UI::render_project_grid() {

        if (m_projects.empty())
            return;

        // Fit as many cards across as possible, but never below the minimum width
        const f32 avail_w = ImGui::GetContentRegionAvail().x;
        const int columns = std::max(1, static_cast<int>(
            (avail_w + PROJECT_CARD_SPACING) / (PROJECT_CARD_MIN_WIDTH + PROJECT_CARD_SPACING)));
        const f32 card_w = (avail_w - (columns - 1) * PROJECT_CARD_SPACING) / columns;

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(PROJECT_CARD_SPACING, PROJECT_CARD_SPACING));

        int col = 0;
        for (const auto& p : m_projects) {
            if (col > 0)
                ImGui::SameLine();
            draw_project_card(p, card_w);
            col = (col + 1) % columns;
        }

        ImGui::PopStyleVar();
    }


    void UI::draw_project_card(const project& p, f32 card_width) {

        ImGui::PushID(p.name.c_str());

        // resolve fonts from the project font registry
        ImFont* title_font = imgui_config::get_font(imgui_config::font_type::bold);
        ImFont* small_font = imgui_config::get_font(imgui_config::font_type::small);
        ImFont* giant_font = imgui_config::get_font(imgui_config::font_type::giant);

        // measure per-font line heights up front
        auto line_height = [](ImFont* f) -> f32 {
            if (!f)
                return ImGui::GetTextLineHeight();
            ImGui::PushFont(f);
            const f32 h = ImGui::GetTextLineHeight();
            ImGui::PopFont();
            return h;
        };
        const f32 title_line_h = line_height(title_font);
        const f32 small_line_h = line_height(small_font);
        const bool is_selected = (m_selected_project == p.name);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const f32 image_h = card_width * 9.f / 16.f;
        const f32 text_h = PROJECT_CARD_TEXT_PADDING * 2.f + title_line_h + small_line_h + PROJECT_CARD_TEXT_GAP;
        const ImVec2 size = ImVec2(card_width, image_h + text_h);

        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##card", size);
        const bool hovered = ImGui::IsItemHovered();
        const bool card_clicked = ImGui::IsItemClicked();

        ImDrawList* dl = ImGui::GetWindowDrawList();

        // background
        ImU32 bg;
        if (is_selected)
            bg = ImGui::GetColorU32(imgui_config::get_action_color_gray_active_ref());
        else if (hovered)
            bg = ImGui::GetColorU32(imgui_config::get_action_color_gray_hover_ref());
        else
            bg = ImGui::GetColorU32(imgui_config::get_action_color_gray_default_ref());
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg, PROJECT_CARD_CORNER);

        const ImVec2 img_min = pos;
        const ImVec2 img_max = ImVec2(pos.x + card_width, pos.y + image_h);
        bool drew_image = false;
        if (const auto it = m_project_images.find(p.name);
            it != m_project_images.end() && it->second && it->second->is_valid()) {
            dl->AddImageRounded(it->second->tex_id(), img_min, img_max, ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), IM_COL32_WHITE,
                PROJECT_CARD_CORNER, ImDrawFlags_RoundCornersTop);
            drew_image = true;
        }

        if (!drew_image) {
            dl->AddRectFilled(img_min, img_max, ImGui::GetColorU32(imgui_config::get_default_gray1_ref()),
                PROJECT_CARD_CORNER, ImDrawFlags_RoundCornersTop);

            if (!p.name.empty() && giant_font) {
                char ch[2] = { p.name[0], 0 };
                ImGui::PushFont(giant_font);
                const ImVec2 ch_size = ImGui::CalcTextSize(ch);
                const ImVec2 ch_pos = ImVec2(img_min.x + (card_width - ch_size.x) * 0.5f, img_min.y + (image_h - ch_size.y) * 0.5f);
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ch_pos, ImGui::GetColorU32(ImGuiCol_Text), ch);
                ImGui::PopFont();
            }
        }

        const ImU32 name_col = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 dim_col = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const f32 text_left = pos.x + PROJECT_CARD_TEXT_PADDING;
        const f32 text_right = pos.x + card_width - PROJECT_CARD_TEXT_PADDING;
        const f32 title_y = pos.y + image_h + PROJECT_CARD_TEXT_PADDING;
        const std::string version_str = p.version.empty() ? std::string{} : ("v" + p.version);
        const f32 version_w = version_str.empty() ? 0.f : ImGui::CalcTextSize(version_str.c_str()).x;
        const f32 title_max_w = text_right - text_left - version_w - 12.f;

        if (title_font) ImGui::PushFont(title_font);
        {
            const std::string title_text = elide_text(p.name, title_max_w);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(text_left, title_y), name_col, title_text.c_str());
        }
        if (title_font) ImGui::PopFont();

        if (!version_str.empty())
            dl->AddText(ImVec2(text_right - version_w, title_y), dim_col, version_str.c_str());

        if (!p.description.empty()) {
            const f32 desc_y = title_y + title_line_h + PROJECT_CARD_TEXT_GAP;
            const f32 desc_max_w = text_right - text_left;

            if (small_font)
                ImGui::PushFont(small_font);
            {
                const std::string desc_text = elide_text(p.description, desc_max_w);
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(text_left, desc_y), dim_col, desc_text.c_str());
            }
            if (small_font) ImGui::PopFont();
        }

        // selection border
        if (is_selected) {

            dl->AddRectFilled(img_min, img_max, IM_COL32(0, 0, 0, 150), PROJECT_CARD_CORNER, ImDrawFlags_RoundCornersTop);

            const f32 btn_w = 140.f;
            const f32 btn_h = 32.f;
            const f32 btn_gap = 10.f;
            const f32 total_w = btn_w * 2.f + btn_gap;
            const f32 btn_x = img_min.x + (card_width - total_w) * 0.5f;
            const f32 btn_y = img_min.y + (image_h  - btn_h)     * 0.5f;

            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.28f, 0.28f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.38f, 0.38f, 0.38f, 1.00f));

            bool button_clicked = false;

            ImGui::SetCursorScreenPos(ImVec2(btn_x, btn_y));
            if (ImGui::Button("Open in Engine", ImVec2(btn_w, btn_h))) {
                button_clicked = true;
                LOG(info, "launcher: Open in Engine requested for [{}] at [{}]", p.name, p.path.generic_string());
            }

            ImGui::SetCursorScreenPos(ImVec2(btn_x + btn_w + btn_gap, btn_y));
            if (ImGui::Button("View Details", ImVec2(btn_w, btn_h))) {
                button_clicked = true;
                load_project_details(p.path);
                m_page = page::project_details;
            }

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();

            dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(imgui_config::get_main_color_ref()),
                PROJECT_CARD_CORNER, 0, 2.0f);

            if (card_clicked && !button_clicked)
                m_selected_project.clear();
        }
        else if (card_clicked) {
            m_selected_project = p.name;
        }

        ImGui::SetCursorScreenPos(pos);
        ImGui::Dummy(size);
        ImGui::PopID();
    }


    void UI::load_project_details(const std::filesystem::path& project_dir) {

        m_details_project.reset();
        m_details_loaded = false;

        VALIDATE(!project_dir.empty(), return, "", "launcher: cannot open details — project has no path set")

        std::error_code error{};
        VALIDATE(std::filesystem::is_directory(project_dir, error) && !error, return,
            "", "launcher: project directory missing [{}]: [{}]", (project_dir.string(), error.message()))

        auto proj = std::make_unique<GLT::project>();
        VALIDATE(proj->is_valid_project_path(project_dir), return, "", "launcher: [{}] is not a valid Gluttony project", (project_dir.string()))

        proj->serialize_projects_data(project_dir, GLT::serializer::option::load);
        VALIDATE(!proj->display_name.empty() && !proj->name.empty(), return,
            "", "launcher: failed to read project data from [{}]", (project_dir.string()))

        m_details_project = std::move(proj);
        m_details_loaded = true;
    }


    void UI::draw_project_details_page() {

        if (ImGui::Button("< Back to Projects"))
            m_page = page::projects;

        ImGui::Separator();
        ImGui::Spacing();

        if (!m_details_loaded || !m_details_project) {
            ImGui::TextDisabled("Failed to load project details. Check the log for the reason.");
            return;
        }

        const auto& p = *m_details_project;

        // ---- header ----
        if (ImFont* h1 = imgui_config::get_font(imgui_config::font_type::header1))
            ImGui::PushFont(h1);
        ImGui::TextUnformatted(p.display_name.empty() ? p.name.c_str() : p.display_name.c_str());
        if (imgui_config::get_font(imgui_config::font_type::header1))
            ImGui::PopFont();

        ImGui::TextDisabled("Directory:  %s", p.project_path.generic_string().c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ---- helper for "label            value" rows ----
        constexpr f32 LABEL_COL = 200.f;
        auto row = [](const char* label, const std::string& value) {
            ImGui::TextDisabled("%s", label);
            ImGui::SameLine(LABEL_COL);
            ImGui::TextWrapped("%s", value.empty() ? "-" : value.c_str());
        };

        // ---- metadata ----
        row("Display Name:", p.display_name);
        row("Name:", p.name);
        row("ID:", fmt_val(p.ID));
        row("Engine Version:", fmt_val(p.engine_version));
        row("Project Version:", fmt_val(p.project_version));

        ImGui::Spacing();

        // ---- paths ----
        row("Build Path:", fmt_val(p.build_path));
        row("Start World:", fmt_val(p.start_world));
        row("Editor Start World:", fmt_val(p.editor_start_world));

        ImGui::Spacing();

        // ---- user data ----
        row("Last Modified:", fmt_val(p.last_modified));
        row("Description:", p.description);

        // ---- tags ----
        ImGui::TextDisabled("Tags:");
        ImGui::SameLine(LABEL_COL);
        if (p.tags.empty()) {
            ImGui::TextDisabled("(none)");
        } else {
            for (size_t i = 0; i < p.tags.size(); ++i) {
                if (i > 0) ImGui::SameLine();
                ImGui::TextUnformatted(p.tags[i].c_str());
                if (i + 1 < p.tags.size()) { ImGui::SameLine(); ImGui::TextDisabled(","); }
            }
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
            const f32   avail_w = ImGui::GetContentRegionAvail().x;
            const f32   aspect = static_cast<f32  >(m_about_art.width()) / static_cast<f32  >(m_about_art.height());
            f32     w = avail_w * 0.5f;
            f32     h = w / aspect;
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
