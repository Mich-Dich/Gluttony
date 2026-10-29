#include "util/pch.h"
#include "plugin_manager.h"

#include <imgui.h>

#include <plugin_system/plugin_manager.h>
#include <config/imgui_config.h>
#include <util/ui/pannel_collection.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    bool icontains(std::string_view haystack, std::string_view needle);

    std::vector<std::string> split_category(const std::string& path);

    std::string join_category(const std::vector<std::string>& parts);

    // Truncates `text` so it fits within `max_width` at the current font, appending "..." if needed.
    std::string fit_text(const std::string& text, f32 max_width);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    bool icontains(std::string_view haystack, std::string_view needle) {

        if (needle.empty())
            return true;
        if (needle.size() > haystack.size())
            return false;

        auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
        for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
            bool ok = true;
            for (size_t j = 0; j < needle.size(); ++j)
                if (lower(haystack[i + j]) != lower(needle[j])) {
                    ok = false;
                    break;
                }
            if (ok)
                return true;
        }
        return false;
    }


    std::vector<std::string> split_category(const std::string& path) {

        std::vector<std::string> out;
        if (path.empty())
            return out;
        size_t start = 0, end;
        while ((end = path.find('|', start)) != std::string::npos) {
            out.push_back(path.substr(start, end - start));
            start = end + 1;
        }
        out.push_back(path.substr(start));
        return out;
    }


    std::string join_category(const std::vector<std::string>& parts) {

        std::string out;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i) out += '|';
            out += parts[i];
        }
        return out;
    }


    std::string fit_text(const std::string& text, f32 max_width) {

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

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================
    
    plugin_manager_window::plugin_manager_window() {

        make_window_name("Plugin Manager");
    }


    plugin_manager_window::~plugin_manager_window() { }

    // CLASS PUBLIC ====================================================================================================

    void plugin_manager_window::focus_plugin(std::string_view plugin_name) {

        if (!find_plugin(plugin_name))
            return;

        m_selected_plugin = std::string(plugin_name);
        m_selected_category.clear();                    // "all"
        m_search_buffer[0] = 0;
        m_filter = state_filter::all;
        m_show_window = true;
    }


    void plugin_manager_window::update(const f32 /*delta_time*/) {

        const u64  gen = plugin_manager::get_generation();
        const bool dirty = plugin_manager::is_config_dirty();

        if (m_first_frame || gen != m_cached_generation || dirty != m_config_dirty) {

            m_first_frame = false;
            m_cached_generation = gen;
            m_config_dirty = dirty;
            refresh_plugin_data();
        }
    }


    bool plugin_manager_window::serialize(const std::filesystem::path& /*project_file*/, const GLT::serializer::option /*option*/) {

        return false;
    }


    void plugin_manager_window::window(const f32 /*delta_time*/) {

        if (!m_show_window)
            return;

        apply_pending_dock();
        ImGui::SetNextWindowSizeConstraints(ImVec2(920.0f, 520.0f), ImVec2(FLT_MAX, FLT_MAX));

        if (!ImGui::Begin(m_window_id.c_str(), &m_show_window))
            { ImGui::End(); return; }

        draw_toolbar();
        draw_restart_banner();

        const ImVec2 avail = ImGui::GetContentRegionAvail();

        // ---- left: tree + filters at the bottom ---- --------------------------------------------------------------
        ImGui::BeginChild("##tree_panel", ImVec2(TREE_PANEL_WIDTH, avail.y), ImGuiChildFlags_Borders);
        {
            const f32 filter_section_h = 94.0f;
            const f32 tree_h = ImGui::GetContentRegionAvail().y - filter_section_h;

            ImGui::BeginChild("##tree_scroll", ImVec2(0, tree_h), ImGuiChildFlags_None);
            draw_tree_panel();
            ImGui::EndChild();

            ImGui::Separator();
            draw_filter_group();
        }
        ImGui::EndChild();

        ImGui::SameLine(0, 4);

        // ---- right: cards + details ---- -------------------------------------------------------------------------
        ImGui::BeginChild("##right_panel", ImVec2(0, avail.y), ImGuiChildFlags_None);
        {
            const bool show_details = !m_selected_plugin.empty();
            const f32  details_h = show_details ? DETAILS_HEIGHT : 0.0f;
            const f32  cards_h = show_details ? (avail.y - details_h - 4.0f) : avail.y;

            ImGui::BeginChild("##cards_panel", ImVec2(0, cards_h), ImGuiChildFlags_Borders);
            draw_cards_panel();
            ImGui::EndChild();

            if (show_details) {
                ImGui::BeginChild("##details_panel", ImVec2(0, 0), ImGuiChildFlags_Borders);
                draw_details_panel();
                ImGui::EndChild();
            }
        }
        ImGui::EndChild();

        ImGui::End();
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    // refresh / tree --------------------------------------------------------------------------------------------------

    void plugin_manager_window::refresh_plugin_data() {

        m_plugins = plugin_manager::get_all_plugins();

        // Selected plugin may have been removed.
        if (!m_selected_plugin.empty() && !find_plugin(m_selected_plugin))
            m_selected_plugin.clear();

        rebuild_category_tree();
        rebuild_interface_tree();
    }


    void plugin_manager_window::rebuild_category_tree() {

        m_category_root = {};
        m_category_root.full_path = "";             // root sentinel

        for (const auto& p : m_plugins) {
            // Uncategorized plugins go under a dedicated leaf so they don't disappear.
            const std::string cat = p.category.empty() ? "Uncategorized" : p.category;
            const auto parts = split_category(cat);

            category_node* node = &m_category_root;
            std::string running;
            for (const auto& part : parts) {
                running = running.empty() ? part : (running + "|" + part);

                auto it = std::find_if(node->children.begin(), node->children.end(),
                    [&](const category_node& c) { return c.name == part; });
                if (it == node->children.end()) {
                    category_node child;
                    child.name = part;
                    child.full_path = running;
                    node->children.push_back(std::move(child));
                    it = node->children.end() - 1;
                }
                node = &(*it);
            }
        }

        // Sort children alphabetically at each level for stable presentation.
        std::function<void(category_node&)> sort_rec = [&](category_node& n) {
            std::sort(n.children.begin(), n.children.end(),
                [](const category_node& a, const category_node& b) { return a.name < b.name; });
            for (auto& c : n.children) sort_rec(c);
        };
        sort_rec(m_category_root);
    }


    void plugin_manager_window::rebuild_interface_tree() {

        m_interface_root = {};
        m_interface_root.full_path = "";

        for (const auto& p : m_plugins) {
            if (p.target == GLT::plugin_manager::interface::none)
                continue;

            const std::string name = std::string(GLT::util::enum_to_string(p.target));
            const std::string full = "interface:" + name;
            auto it = std::find_if(m_interface_root.children.begin(), m_interface_root.children.end(),
                [&](const category_node& c) { return c.name == name; });

            if (it == m_interface_root.children.end()) {
                category_node child;
                child.name = name;
                child.full_path = full;
                m_interface_root.children.push_back(std::move(child));
            }
        }

        std::sort(m_interface_root.children.begin(), m_interface_root.children.end(),
            [](const category_node& a, const category_node& b) {
                // "custom" is a shared bucket — always sort it after the exclusive interfaces
                const bool a_custom = (a.name == "custom");
                const bool b_custom = (b.name == "custom");
                if (a_custom != b_custom)
                    return !a_custom;
                return a.name < b.name;
            });
    }

    // toolbar / filters -----------------------------------------------------------------------------------------------

    void plugin_manager_window::draw_toolbar() {

        ImGui::SetNextItemWidth(300.0f);
        ImGui::InputTextWithHint("##search", "Search name / category / author...",
            m_search_buffer, IM_ARRAYSIZE(m_search_buffer));
        if (m_search_buffer[0] != 0) {
            ImGui::SameLine(0, 4);
            if (ImGui::SmallButton("x##clear_search"))
                m_search_buffer[0] = 0;
        }
    }


    void plugin_manager_window::draw_filter_group() {

        const ImVec4& main_col = imgui_config::get_main_color_ref();

        auto filter_btn = [&](const char* label, const char* id, state_filter target) {

            const bool active = (m_filter == target);
            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button, main_col);
            if (ImGui::Button(label, ImVec2(0.0f, 0.0f)))
                m_filter = target;
            if (active)
                ImGui::PopStyleColor();
            (void)id;
        };

        auto group_btn = [&](const char* label, group_mode target) {

            const bool active = (m_group_mode == target);
            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button, main_col);
            if (ImGui::Button(label, ImVec2(0.0f, 0.0f)))
                m_group_mode = target;
            if (active)
                ImGui::PopStyleColor();
        };

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 3.0f));

        ImGui::TextDisabled("Filter");
        filter_btn("All", "##f_all", state_filter::all);
        ImGui::SameLine(0, 2);
        filter_btn("On",  "##f_en",  state_filter::enabled);
        ImGui::SameLine(0, 2);
        filter_btn("Off", "##f_dis", state_filter::disabled);
        ImGui::SameLine(0, 2);
        filter_btn("Blk", "##f_blk", state_filter::blocked);

        ImGui::TextDisabled("Group by");
        group_btn("Category",  group_mode::category);
        ImGui::SameLine(0, 2);
        group_btn("Interface", group_mode::interface_);

        ImGui::PopStyleVar(2);
    }


    void plugin_manager_window::draw_restart_banner() {

        if (!m_config_dirty)
            return;

        ImVec4 banner_bg = imgui_config::get_action_color00_faded_ref();
        banner_bg.w = 0.65f;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, banner_bg);
        ImGui::BeginChild("##restart_banner", ImVec2(0, 26), ImGuiChildFlags_None);
        ImGui::SetCursorPos(ImVec2(8, 5));
        ImGui::TextColored(imgui_config::get_main_color_ref(),
            "!  Plugin configuration changed - restart the engine to apply");
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // tree ------------------------------------------------------------------------------------------------------------

    void plugin_manager_window::draw_tree_panel() {

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));

        const bool all_selected = m_selected_category.empty();

        if (all_selected)
            ImGui::PushStyleColor(ImGuiCol_Text, imgui_config::get_main_color_ref());

        if (ImGui::Selectable("All Plugins", all_selected))
            m_selected_category.clear();

        if (all_selected)
            ImGui::PopStyleColor();

        ImGui::TextDisabled("%zu total", m_plugins.size());
        ImGui::Separator();

        category_node& root = (m_group_mode == group_mode::category) ? m_category_root : m_interface_root;
        for (auto& child : root.children)
            draw_tree_node(child);

        ImGui::PopStyleVar();
    }


    void plugin_manager_window::draw_tree_node(category_node& node) {

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (node.children.empty())
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (node.full_path == m_selected_category)
            flags |= ImGuiTreeNodeFlags_Selected;

        // Default-open the top-level groups so a first-time user immediately
        // sees the shape of the tree.
        if (node.full_path.find('|') == std::string::npos && node.full_path.find(':') == std::string::npos)
            flags |= ImGuiTreeNodeFlags_DefaultOpen;

        const bool is_selected = (node.full_path == m_selected_category);

        if (is_selected)
            ImGui::PushStyleColor(ImGuiCol_Text, imgui_config::get_main_color_ref());

        const bool open = ImGui::TreeNodeEx(node.full_path.c_str(), flags, "%s", node.name.c_str());

        // ---- click check FIRST, before any style-stack manipulation ----
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            m_selected_category = node.full_path;

        if (is_selected)
            ImGui::PopStyleColor();

        if (open && !node.children.empty()) {
            for (auto& c : node.children)
                draw_tree_node(c);
            ImGui::TreePop();
        }
    }

    // cards -----------------------------------------------------------------------------------------------------------

    void plugin_manager_window::draw_cards_panel() {

        // ---- rebuild filtered list ----
        m_filtered_indices.clear();
        for (size_t i = 0; i < m_plugins.size(); ++i) {
            const auto& p = m_plugins[i];
            if (!matches_category(p, m_selected_category))
                continue;
            if (!matches_search(p))
                continue;
            if (!passes_filter(p))
                continue;
            m_filtered_indices.push_back(i);
        }

        ImGui::TextDisabled("%zu plugin%s", m_filtered_indices.size(), m_filtered_indices.size() == 1 ? "" : "s");
        ImGui::Separator();

        if (m_filtered_indices.empty()) {
            ImGui::Dummy(ImVec2(0, 20));
            ImGui::TextDisabled("No plugins match the current filters.");
            return;
        }

        const category_node& root = (m_group_mode == group_mode::category) ? m_category_root : m_interface_root;
        const category_node* scope = find_node_by_path(root, m_selected_category);

        if (scope && !scope->children.empty()) {

            std::vector<size_t> direct;
            for (size_t idx : m_filtered_indices)
                if (plugin_is_direct(m_plugins[idx], *scope))
                    direct.push_back(idx);
            if (!direct.empty()) {
                render_card_grid(direct);
                ImGui::Spacing();
            }

            for (const auto& child : scope->children)
                render_category_section(child);

        } else
            render_card_grid(m_filtered_indices);
    }


    void plugin_manager_window::render_category_section(const category_node& node) {

        // Gather every filtered plugin that lives somewhere in this subtree.
        std::vector<size_t> subtree;
        for (size_t idx : m_filtered_indices)
            if (plugin_in_section(m_plugins[idx], node))
                subtree.push_back(idx);

        // Skip empty sections entirely — no header, no gap.
        if (subtree.empty())
            return;

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
        if (node.children.empty())
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        const bool open = ImGui::TreeNodeEx((node.full_path + "##section").c_str(),
            flags, "%s (%zu)", node.name.c_str(), subtree.size());

        if (!open)
            return;

        // Recurse into sub-categories first (matches the tree's own ordering)
        for (const auto& child : node.children)
            render_category_section(child);

        // Then render plugins that live directly at this node
        std::vector<size_t> direct;
        for (size_t idx : subtree)
            if (plugin_is_direct(m_plugins[idx], node))
                direct.push_back(idx);
        if (!direct.empty())
            render_card_grid(direct);

        if (!node.children.empty())
            ImGui::TreePop();
    }


    void plugin_manager_window::render_card_grid(const std::vector<size_t>& indices) {

        if (indices.empty())
            return;

        const f32 avail_w = ImGui::GetContentRegionAvail().x;
        const int columns = std::max(1, static_cast<int>((avail_w + CARD_SPACING) / (CARD_MIN_WIDTH + CARD_SPACING)));
        const f32 card_w = (avail_w - (columns - 1) * CARD_SPACING) / columns;

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(CARD_SPACING, CARD_SPACING));

        int col = 0;
        for (size_t idx : indices) {
            if (col > 0) ImGui::SameLine();
            draw_plugin_card(m_plugins[idx], card_w);
            col = (col + 1) % columns;
        }

        ImGui::PopStyleVar();
    }


    void plugin_manager_window::draw_plugin_card(const GLT::plugin_manager::plugin_info& info, f32 card_width) {

        ImGui::PushID(info.name.c_str());

        const plugin_state state = classify(info);
        const bool is_selected = (m_selected_plugin == info.name);

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImVec2(card_width, CARD_HEIGHT);

        ImGui::InvisibleButton("##card", size);
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked())
            m_selected_plugin = info.name;

        ImDrawList* dl = ImGui::GetWindowDrawList();

        // ---- background ----
        ImU32 bg;
        if (is_selected)
            bg = ImGui::GetColorU32(imgui_config::get_action_color_gray_active_ref());
        else if (hovered)
            bg = ImGui::GetColorU32(imgui_config::get_action_color_gray_hover_ref());
        else
            bg = ImGui::GetColorU32(imgui_config::get_action_color_gray_default_ref());

        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg, 4.0f);

        // ---- logo strip (full-height square on the left) ----
        const f32 logo_w = size.y;
        const ImU32 logo_bg = ImGui::GetColorU32(imgui_config::get_default_gray1_ref());
        dl->AddRectFilled(pos, ImVec2(pos.x + logo_w, pos.y + size.y),
            logo_bg, 4.0f, ImDrawFlags_RoundCornersLeft);

        {
            char ch[2] = { info.display_name.empty() ? '?' : static_cast<char>(info.display_name[0]), 0 };
            const ImVec2 ch_size = ImGui::CalcTextSize(ch);
            dl->AddText(
                ImVec2(pos.x + (logo_w - ch_size.x) * 0.5f, pos.y + (size.y - ch_size.y) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_Text), ch);
        }

        // ---- text block ----
        const f32 text_x = pos.x + logo_w + 12.0f;
        const f32 text_max_w = size.x - logo_w - 24.0f;
        f32 text_y = pos.y + 10.0f;

        const ImU32 name_col = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 dim_col = ImGui::GetColorU32(ImGuiCol_TextDisabled);

        dl->AddText(ImVec2(text_x, text_y), name_col, fit_text(info.display_name, text_max_w).c_str());
        text_y += 20.0f;

        dl->AddText(ImVec2(text_x, text_y), dim_col, info.plugin_version.to_str().c_str());
        text_y += 18.0f;

        if (!info.category.empty()) {
            dl->AddText(ImVec2(text_x, text_y), dim_col, fit_text(info.category, text_max_w).c_str());
            text_y += 18.0f;
        }

        // ---- state pill ----
        const char* state_label = "";
        ImU32 state_col = 0;
        switch (state) {
            case plugin_state::loaded:
                state_label = "Loaded";
                state_col = ImGui::GetColorU32(imgui_config::get_main_color_ref());
                break;
            case plugin_state::pending:
                state_label = "Pending";
                state_col = IM_COL32(230, 190,  75, 255);
                break;
            case plugin_state::blocked:
                state_label = "Blocked";
                state_col = IM_COL32(240, 100,  75, 255);
                break;
            default:
                state_label = "Disabled";
                state_col = ImGui::GetColorU32(imgui_config::get_default_gray_ref());
                break;
        }

        dl->AddCircleFilled(ImVec2(text_x + 4.0f, text_y + 8.0f), 4.0f, state_col);
        dl->AddText(ImVec2(text_x + 12.0f, text_y), state_col, state_label);

        // ---- selection border (nothing otherwise) ----
        if (is_selected) {
            dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(imgui_config::get_main_color_ref()),
                4.0f, 0, 2.0f);
        }

        ImGui::PopID();
    }

    // details ---------------------------------------------------------------------------------------------------------

    void plugin_manager_window::draw_details_panel() {

        ImGui::Separator();
        const GLT::plugin_manager::plugin_info* info = find_plugin(m_selected_plugin);
        if (!info) {
            ImGui::TextDisabled("Select a plugin.");
            return;
        }

        if (ImGui::BeginTable("##details_3col", 3,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoBordersInBody)) {

            ImGui::TableNextRow();

            // ---- Column 1: image + header + action button ----
            ImGui::TableSetColumnIndex(0);
            ImGui::BeginGroup();
            draw_details_header(*info);
            draw_details_actions(*info);
            ImGui::EndGroup();

            // ---- Column 2: metadata ----
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginGroup();
            draw_details_metadata(*info);
            ImGui::EndGroup();

            // ---- Column 3: dependencies ----
            ImGui::TableSetColumnIndex(2);
            ImGui::BeginGroup();
            draw_details_dependencies(*info);
            ImGui::EndGroup();

            ImGui::EndTable();
        }
    }


    void plugin_manager_window::draw_details_header(const GLT::plugin_manager::plugin_info& info) {


        if (ImGui::BeginTable("##header", 2, ImGuiTableFlags_SizingFixedFit)) {

            ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            draw_logo_placeholder(info, 100.0f);

            ImGui::TableSetColumnIndex(1);
            ImGui::PushFont(imgui_config::get_font(imgui_config::font_type::bold_big));
            ImGui::TextWrapped("%s", info.display_name.c_str());
            ImGui::PopFont();

            ImGui::Spacing();
            ImGui::TextDisabled("v%s   api %s",
                info.plugin_version.to_str().c_str(),
                info.api_version.to_str().c_str());

            if (!info.author.empty())
                ImGui::TextDisabled("by %s", info.author.c_str());

            if (!info.category.empty())
                ImGui::TextDisabled("%s", info.category.c_str());

            if (!info.description.empty()) {
                ImGui::Spacing();
                ImGui::TextWrapped("%s", info.description.c_str());
            }            

            ImGui::EndTable();
        }

    }


    void plugin_manager_window::draw_details_metadata(const GLT::plugin_manager::plugin_info& info) {

        ImGui::TextDisabled("Details");
        ImGui::Spacing();

        if (ImGui::BeginTable("##meta", 2, ImGuiTableFlags_SizingFixedFit)) {

            ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);

            auto row = [](const char* label, const char* value) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%s", label);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(value);
            };

            row("Interface", std::string(GLT::util::enum_to_string(info.target)).c_str());
            row("Load phase", std::string(GLT::util::enum_to_string(info.load_phase)).c_str());
            row("Unload phase", std::string(GLT::util::enum_to_string(info.unload_phase)).c_str());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("State");
            ImGui::TableSetColumnIndex(1);
            draw_state_pill(classify(info));

            ImGui::EndTable();
        }
    }


    void plugin_manager_window::draw_details_dependencies(const GLT::plugin_manager::plugin_info& info) {

        ImGui::TextDisabled("Dependencies");
        ImGui::Spacing();

        const auto deps = plugin_manager::get_dependency_status(info);
        if (deps.empty()) {
            ImGui::TextDisabled("(none)");
            return;
        }

        for (const auto& d : deps) {

            const ImVec4 col = d.currently_loaded ? imgui_config::get_main_color_ref()
                : d.satisfied ? ImVec4(0.90f, 0.75f, 0.30f, 1.0f)
                : ImVec4(0.95f, 0.40f, 0.30f, 1.0f);
            const char* glyph = d.currently_loaded ? "[ok]"
                : d.satisfied ? "[..]"
                : "[!!]";

            ImGui::TextColored(col, "%s", glyph);
            ImGui::SameLine(0, 6);
            ImGui::TextUnformatted(d.requirement.c_str());

            if (!d.provider_display.empty()) {
                ImGui::SameLine(0, 6);
                ImGui::TextDisabled("(%s)", d.provider_display.c_str());
            }
        }
    }


    void plugin_manager_window::draw_details_actions(const GLT::plugin_manager::plugin_info& info) {

        const plugin_state state = classify(info);

        ImGui::Spacing();
        ImGui::Spacing();

        // Conflicting-alternatives note (above the button)
        if (info.target != GLT::plugin_manager::interface::custom) {
            for (const auto& p : m_plugins) {
                if (p.target == info.target && p.name != info.name && p.is_enabled) {
                    ImGui::TextDisabled("Replaces '%s'.", p.display_name.c_str());
                    break;
                }
            }
        }

        // ---- button, right-aligned to the column's available width ----
        const char* label = info.is_enabled ? "Disable" : "Enable";
        const f32 button_w = 100.0f;
        const f32 avail_w = ImGui::GetContentRegionAvail().x;
        if (avail_w > button_w + 4.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail_w - button_w);

        if (ImGui::Button(label, ImVec2(button_w, 0.0f)))
            plugin_manager::set_plugin_enabled(info.name, !info.is_enabled);

        // ---- contextual note underneath ----
        if (state == plugin_state::loaded && !info.is_enabled)
            ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.3f, 1.0f), "Will disable on restart");
        else if (state == plugin_state::pending && info.is_enabled)
            ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.3f, 1.0f), "Restart to load");
    }

    // helpers ---------------------------------------------------------------------------------------------------------

    void plugin_manager_window::draw_state_pill(plugin_state state) {

        struct entry { const char* label; ImVec4 color; };
        entry e{};
        switch (state) {
            case plugin_state::loaded:  e = { "Loaded",   imgui_config::get_main_color_ref() }; break;
            case plugin_state::pending: e = { "Pending",  ImVec4(0.90f, 0.75f, 0.30f, 1.0f) }; break;
            case plugin_state::blocked: e = { "Blocked",  ImVec4(0.95f, 0.40f, 0.30f, 1.0f) }; break;
            default:                    e = { "Disabled", imgui_config::get_default_gray_ref() }; break;
        }
        ImGui::TextColored(e.color, "%s %s",
            (state == plugin_state::loaded) ? "*" : "", e.label);
    }


    void plugin_manager_window::draw_logo_placeholder(const GLT::plugin_manager::plugin_info& info, f32 size) {

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();

        dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size),
            ImGui::GetColorU32(imgui_config::get_default_gray1_ref()), 4.0f);

        // First letter of the display name, centred.
        char ch[2] = { info.display_name.empty() ? '?' : static_cast<char>(info.display_name[0]), 0 };
        const ImVec2 tsize = ImGui::CalcTextSize(ch);
        dl->AddText(ImVec2(pos.x + (size - tsize.x) * 0.5f, pos.y + (size - tsize.y) * 0.5f),
            ImGui::GetColorU32(ImGuiCol_Text), ch);

        ImGui::Dummy(ImVec2(size, size));
    }


    plugin_manager_window::plugin_state plugin_manager_window::classify(const GLT::plugin_manager::plugin_info& info) const {

        if (info.is_loaded)
            return plugin_state::loaded;
        if (!info.is_enabled)
            return plugin_state::disabled;

        const auto deps = plugin_manager::get_dependency_status(info);
        for (const auto& d : deps)
            if (!d.satisfied)
                return plugin_state::blocked;

        return plugin_state::pending;
    }


    bool plugin_manager_window::matches_category(const GLT::plugin_manager::plugin_info& info, const std::string& sel) const {

        if (sel.empty())
            return true;

        // interface grouping selector
        if (sel.rfind("interface:", 0) == 0)
            return std::string(GLT::util::enum_to_string(info.target)) == sel.substr(10);

        const std::string cat = info.category.empty() ? "Uncategorized" : info.category;
        if (cat == sel)
            return true;
        return cat.size() > sel.size()
            && cat.compare(0, sel.size(), sel) == 0
            && cat[sel.size()] == '|';
    }


    bool plugin_manager_window::matches_search(const GLT::plugin_manager::plugin_info& info) const {

        const std::string_view q = m_search_buffer;
        if (q.empty())
            return true;

        return icontains(info.name, q)
            || icontains(info.display_name, q)
            || icontains(info.description, q)
            || icontains(info.category, q)
            || icontains(info.author, q);
    }


    bool plugin_manager_window::passes_filter(const GLT::plugin_manager::plugin_info& info) const {

        switch (m_filter) {
            case state_filter::all:      return true;
            case state_filter::enabled:  return info.is_enabled;
            case state_filter::disabled: return !info.is_enabled;
            case state_filter::blocked: {
                if (!info.is_enabled)
                    return false;
                for (const auto& d : plugin_manager::get_dependency_status(info))
                    if (!d.satisfied)
                        return true;
                return false;
            }
        }
        return true;
    }


    const GLT::plugin_manager::plugin_info* plugin_manager_window::find_plugin(std::string_view name) const {

        for (const auto& p : m_plugins)
            if (p.name == name)
                return &p;
        return nullptr;
    }


    const plugin_manager_window::category_node* plugin_manager_window::find_node_by_path(const category_node& root,
        const std::string& path) const {

        if (path.empty() || root.full_path == path)
            return &root;

        for (const auto& child : root.children)
            if (const auto* found = find_node_by_path(child, path))
                return found;

        return nullptr;
    }


    bool plugin_manager_window::plugin_in_section(const GLT::plugin_manager::plugin_info& info, const category_node& node) const {

        if (node.full_path.empty())
            return true;

        // Interface grouping nodes
        if (node.full_path.rfind("interface:", 0) == 0)
            return std::string(GLT::util::enum_to_string(info.target)) == node.full_path.substr(10);

        const std::string cat = info.category.empty() ? "Uncategorized" : info.category;
        if (cat == node.full_path)
            return true;
        return cat.size() > node.full_path.size()
            && cat.compare(0, node.full_path.size(), node.full_path) == 0
            && cat[node.full_path.size()] == '|';
    }


    bool plugin_manager_window::plugin_is_direct(const GLT::plugin_manager::plugin_info& info, const category_node& node) const {

        if (node.full_path.rfind("interface:", 0) == 0)
            return std::string(GLT::util::enum_to_string(info.target)) == node.full_path.substr(10);

        const std::string cat = info.category.empty() ? "Uncategorized" : info.category;
        return cat == node.full_path;
    }

}
