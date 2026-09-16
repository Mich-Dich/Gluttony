
#include "util/pch.h"
#include "content_browser.h"

#include <imgui.h>

#include <application.h>
#include <config/imgui_config.h>

#include "resource_manager/icon_manager.h"
#include "util/ui/pannel_collection.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    constexpr f32                               left_panel_min_width = 160.0f;

    constexpr f32                               left_panel_max_width = 600.0f;

    constexpr f32                               cell_width = 96.0f;

    constexpr f32                               cell_height = 96.0f;

    constexpr f32                               icon_render_size = 48.0f;

    constexpr f32                               icon_top_margin = 8.0f;

    constexpr f32                               label_bottom_margin = 20.0f;

    constexpr const char*                       drag_payload_id = "CONTENT_BROWSER_ITEM";

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    bool is_hidden_entry(const std::filesystem::path& p);

    bool paths_equal(const std::filesystem::path& a, const std::filesystem::path& b);

    // Maps a file extension to the icon that should represent it in the browser.
    icon_manager::icon extension_to_icon(const std::string& ext);

    std::vector<std::filesystem::path> list_subdirectories(const std::filesystem::path& dir);

    bool is_image_extension(const std::string& ext);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    bool is_hidden_entry(const std::filesystem::path& p) {

        const std::string name = p.filename().string();
        return !name.empty() && name.front() == '.';
    }


    bool paths_equal(const std::filesystem::path& a, const std::filesystem::path& b) {

        return a.lexically_normal() == b.lexically_normal();
    }


    icon_manager::icon extension_to_icon(const std::string& ext) {

        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
            ext == ".bmp" || ext == ".tga" || ext == ".hdr")
            return icon_manager::icon::texture_big;

        if (ext == ".gltf" || ext == ".glb" || ext == ".obj" || ext == ".fbx")
            return icon_manager::icon::mesh_asset_big;

        if (ext == ".glsl" || ext == ".vert" || ext == ".frag" || ext == ".comp" ||
            ext == ".cpp"  || ext == ".h"    || ext == ".hpp"  || ext == ".c" || ext == ".inl")
            return icon_manager::icon::script_big;

        if (ext == ".mat")
            return icon_manager::icon::material_big;

        if (ext == ".json" || ext == ".yaml" || ext == ".yml" || ext == ".toml")
            return icon_manager::icon::settings;

        if (ext == ".wav" || ext == ".ogg" || ext == ".mp3")
            return icon_manager::icon::file;               // TODO: add dedicated icon

        return icon_manager::icon::file;
    }


    std::vector<std::filesystem::path> list_subdirectories(const std::filesystem::path& dir) {

        std::vector<std::filesystem::path> out;
        std::error_code error{};
        auto iterator = GLT::vfs::directory_iterator(dir, error);
        VALIDATE(!error, return {}, "", "Failed to create directory iterator for [{}]", dir.generic_string())
        for (auto& entry : iterator) {

            if (entry.is_directory(error) && !is_hidden_entry(entry.path()))
                out.push_back(entry.path());
        }

        std::sort(out.begin(), out.end());
        return out;
    }


    bool is_image_extension(const std::string& ext) {

        return ext == ".png"  || ext == ".jpg" || ext == ".jpeg" ||
               ext == ".bmp"  || ext == ".tga" || ext == ".hdr"  ||
               ext == ".psd"  || ext == ".gif" || ext == ".pic"  || ext == ".pnm";
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    content_browser_window::content_browser_window() {

        make_window_name("Content Browser");

        m_content_dir = GLT::application::get().get_project_path() / GLT::config::CONTENT_DIR;

        // Make sure the content root actually exists before we try to browse it.
        std::error_code error{};
        if (!m_content_dir.empty() && !GLT::vfs::exists(m_content_dir, error) && !error) {

            GLT::vfs::create_directories(m_content_dir, error);
        }

        // Route through navigate_to() so the history stack starts populated
        // (otherwise back/forward buttons would be permanently dead on launch).
        if (!m_content_dir.empty())
            navigate_to(m_content_dir);
    }


    content_browser_window::~content_browser_window() { }

    // CLASS PUBLIC ====================================================================================================

    std::filesystem::path content_browser_window::selected_path() const {

        return m_selected_paths.empty() ? std::filesystem::path{} : m_selected_paths.front();
    }


    void content_browser_window::window(const f32 /*delta_time*/) {

        if (!m_show_window)
            return;

        // Process any thumbnails that finished loading since the last frame.
        icon_manager::flush_thumbnail_uploads();

        if (ImGui::Begin(m_window_title.c_str(), &m_show_window)) {

            // Any state change marks the entry cache as stale; we rebuild it here
            // rather than inside each draw call so a single frame never scans the
            // filesystem more than once.
            if (m_entries_dirty)
                refresh_directory_entries();
    
            ImGui::SetNextWindowSizeConstraints(ImVec2(left_panel_min_width, 0), ImVec2(left_panel_max_width, std::numeric_limits<f32>::max()));
            UI::custom_frame(200, true, ImGui::GetColorU32(GLT::imgui_config::get_default_gray1_ref()),
                [this]() { 
                    draw_directory_tree();
                },
                [this]() {
                    draw_toolbar();
                    draw_file_view();
                });
    
            draw_popups();
        }

        ImGui::End();
    }


    void content_browser_window::update(const f32 /*delta_time*/) {

        // All filesystem work happens on the frame the state becomes dirty;
        // nothing to do per-frame here yet. If you later add async scanning,
        // this is where the completion callback would land.
    }


    bool content_browser_window::serialize(const std::filesystem::path& /*project_file*/, const GLT::serializer::option /*option*/) { return false; }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    // selection -------------------------------------------------------------------------------------------------------

    bool content_browser_window::is_selected(const std::filesystem::path& p) const {

        return std::find_if(m_selected_paths.begin(), m_selected_paths.end(),
            [&](const auto& sp) { return paths_equal(sp, p); }) != m_selected_paths.end();
    }


    void content_browser_window::select_single(const std::filesystem::path& p) {

        m_selected_paths.clear();
        m_selected_paths.push_back(p);
        m_selection_anchor = p;
    }


    void content_browser_window::toggle_selection(const std::filesystem::path& p) {

        const auto it = std::find_if(m_selected_paths.begin(), m_selected_paths.end(),
            [&](const auto& sp) { return paths_equal(sp, p); });

        if (it != m_selected_paths.end())
            m_selected_paths.erase(it);
        else
            m_selected_paths.push_back(p);

        m_selection_anchor = p;
    }


    void content_browser_window::extend_selection_to(const std::filesystem::path& p) {

        // Resolve both the anchor and the clicked item to their indices in
        // m_entries. If either cannot be found (e.g., anchor was deleted),
        // fall back to a single-item selection.
        i32 anchor_idx = -1;
        i32 target_idx = -1;
        for (i32 i = 0; i < static_cast<i32>(m_entries.size()); ++i) {

            if (anchor_idx < 0 && paths_equal(m_entries[i].path, m_selection_anchor))
                anchor_idx = i;

            if (target_idx < 0 && paths_equal(m_entries[i].path, p))
                target_idx = i;

            if (anchor_idx >= 0 && target_idx >= 0)
                break;
        }

        if (anchor_idx < 0 || target_idx < 0) {

            select_single(p);
            return;
        }

        m_selected_paths.clear();

        const i32 lo = std::min(anchor_idx, target_idx);
        const i32 hi = std::max(anchor_idx, target_idx);
        for (i32 i = lo; i <= hi; ++i)
            m_selected_paths.push_back(m_entries[i].path);

        // Deliberately do NOT update m_selection_anchor — repeated Shift+click
        // should keep extending from the original anchor.
    }


    void content_browser_window::clear_selection() {

        m_selected_paths.clear();
        m_selection_anchor.clear();
    }

    // toolbar & breadcrumbs -------------------------------------------------------------------------------------------

    void content_browser_window::draw_toolbar() {

        const bool can_back = m_history_index > 0;
        const bool can_fwd  = m_history_index >= 0 && (m_history_index + 1) < m_history_size;
        const bool can_up   = !m_current_dir.empty() && !paths_equal(m_current_dir, m_content_dir);

        ImGui::BeginDisabled(!can_back);

        if (UI::gray_button("<##cb_back"))      navigate_back();
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!can_fwd);
        if (UI::gray_button(">##cb_fwd"))       navigate_forward();
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!can_up);
        if (UI::gray_button("^##cb_up"))        navigate_up();
        ImGui::EndDisabled();

        ImGui::SameLine();
        draw_breadcrumbs();

        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputTextWithHint("##cb_search", "Search...", m_search_buffer, sizeof(m_search_buffer));

        ImGui::SameLine();
        if (ImGui::Button("Refresh")) m_entries_dirty = true;
    }


    void content_browser_window::draw_breadcrumbs() {

        if (m_content_dir.empty())
            return;

        // Build the ancestor list from content_dir down to current_dir.
        std::vector<std::filesystem::path> crumbs;
        for (std::filesystem::path p = m_current_dir; !p.empty(); p = p.parent_path()) {
            crumbs.push_back(p);
            if (paths_equal(p, m_content_dir)) break;
        }
        std::reverse(crumbs.begin(), crumbs.end());

        for (size_t i = 0; i < crumbs.size(); ++i) {
            if (i > 0) {
                ImGui::SameLine();
                ImGui::TextUnformatted("/");
                ImGui::SameLine();
            }

            ImGui::PushID(static_cast<int>(i));
            const std::string label = crumbs[i].filename().string();
            if (ImGui::SmallButton(label.empty() ? "<root>" : label.c_str())) {
                navigate_to(crumbs[i]);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }

    // directory tree --------------------------------------------------------------------------------------------------

    void content_browser_window::draw_directory_tree() {

        if (m_content_dir.empty())
            return;

        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 10.0f);
        draw_directory_tree_recursive(m_content_dir);
        ImGui::PopStyleVar();
    }


    void content_browser_window::draw_directory_tree_recursive(const std::filesystem::path& dir) {

        // TODO: cache per-node subdirectory lists — right now we re-scan the
        //       filesystem on every frame for every visible tree node.
        const auto subdirs = list_subdirectories(dir);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
            | ImGuiTreeNodeFlags_SpanAvailWidth
            | ImGuiTreeNodeFlags_DefaultOpen;

        if (paths_equal(dir, m_current_dir)) flags |= ImGuiTreeNodeFlags_Selected;
        if (subdirs.empty())                 flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        ImGui::PushID(dir.string().c_str());

        const std::string label = dir.filename().empty() ? dir.string() : dir.filename().string();
        const bool open = ImGui::TreeNodeEx(label.c_str(), flags);

        // Only respond to clicks on the label area, not on the expand arrow.
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            navigate_to(dir);

        if (open && !subdirs.empty()) {
            for (const auto& sub : subdirs)
                draw_directory_tree_recursive(sub);

            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    // file grid -------------------------------------------------------------------------------------------------------

    void content_browser_window::draw_file_view() {

        const std::string filter = m_search_buffer;
        const f32 avail = ImGui::GetContentRegionAvail().x;
        const i32 columns = std::max(1, static_cast<i32>(avail / cell_width));
        i32  col = 0;
        bool any = false;

        for (const auto& entry : m_entries) {

            if (!filter.empty() && entry.name.find(filter) == std::string::npos)
                continue;

            draw_file_item(entry);
            any = true;

            if (++col < columns)
                ImGui::SameLine();
            else
                col = 0;
        }

        if (!any)
            ImGui::TextDisabled(filter.empty() ? "(empty)" : "(no matches)");

        // Click on empty space (no item hovered) clears the selection.
        // Modifier keys suppress this so Ctrl/Shift+click on empty space
        // does nothing unexpected.
        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsWindowHovered() 
            && !ImGui::IsAnyItemHovered() 
            && ImGui::IsMouseClicked(ImGuiMouseButton_Left) 
            && !io.KeyCtrl 
            && !io.KeyShift) {

            clear_selection();
        }

        // Right-click on empty space.
        if (ImGui::BeginPopupContextWindow("##cb_bg", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {

            draw_background_context_menu();
            ImGui::EndPopup();
        }
    }


    void content_browser_window::draw_file_item(const dir_entry& entry) {

        ImGui::PushID(entry.path.string().c_str());

        const ImVec2 cell_size(cell_width, cell_height);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const bool selected = is_selected(entry.path);
        const bool button = ImGui::InvisibleButton("##cell", cell_size);
        const auto interaction = UI::get_mouse_interation_on_item();

        if (interaction == UI::mouse_interation::left_double_clicked) {

            // Double-click: reduce to a single selection, then act on it.
            // We deliberately ignore Ctrl/Shift here — the second click of
            // a double should not toggle the item back off or extend a range.
            select_single(entry.path);

            if (entry.is_directory) {
                navigate_to(entry.path);
            } else {
                // TODO: broadcast an "open asset" event to the editor.
            }

        } else if(button) {

            // Single click: apply the modifier-aware selection policy.
            const ImGuiIO& io = ImGui::GetIO();
            const bool ctrl  = io.KeyCtrl;
            const bool shift = io.KeyShift;

            if (shift && !m_selection_anchor.empty())
                extend_selection_to(entry.path);

            else if (ctrl)
                toggle_selection(entry.path);

            else
                select_single(entry.path);
        }

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 cell_max(origin.x + cell_size.x, origin.y + cell_size.y);

        if (selected)                                                                   // Background highlight.
            draw->AddRectFilled(origin, cell_max, ImGui::GetColorU32(GLT::imgui_config::get_main_color_ref()), 4.0f);

        else if (interaction == UI::mouse_interation::hovered)
            draw->AddRectFilled(origin, cell_max, IM_COL32(90, 90, 90, 120), 4.0f);

        // icon / thumbnail --------------------------------------------------------------------------------------------
        bool drew_thumbnail = false;
        if (!entry.is_directory && is_image_extension(entry.extension)) {

            const auto thumb = icon_manager::get_thumbnail(entry.path);
            if (thumb.state == icon_manager::thumbnail_state::ready && thumb.image_size.x > 0.0f) {

                // Fit the thumbnail into the icon render box, preserving aspect ratio.
                const f32 scale = std::min(icon_render_size / thumb.image_size.x, icon_render_size / thumb.image_size.y);
                const f32 draw_w = thumb.image_size.x * scale;
                const f32 draw_h = thumb.image_size.y * scale;
                const ImVec2 t_min(
                    origin.x + (cell_size.x - draw_w) * 0.5f,
                    origin.y + icon_top_margin + (icon_render_size - draw_h) * 0.5f);
                const ImVec2 t_max(t_min.x + draw_w, t_min.y + draw_h);

                draw->AddImage(thumb.tex_ref, t_min, t_max, thumb.uv0, thumb.uv1);
                drew_thumbnail = true;
            }
        }

        if (!drew_thumbnail) {

            const icon_manager::icon_data icon = icon_manager::get(
                entry.is_directory ? icon_manager::icon::folder_big : extension_to_icon(entry.extension));
            if (icon.image_size.x > 0.0f) {

                const ImVec2 icon_min(
                    origin.x + (cell_size.x - icon_render_size) * 0.5f,
                    origin.y + icon_top_margin);
                const ImVec2 icon_max(
                    icon_min.x + icon_render_size,
                    icon_min.y + icon_render_size);

                draw->AddImage(icon.tex_ref, icon_min, icon_max, icon.uv0, icon.uv1);
            }
        }

        // filename, clipped to the cell -------------------------------------------------------------------------------
        draw->PushClipRect(origin, cell_max, true);
        const ImVec2 name_size = ImGui::CalcTextSize(entry.name.c_str());
        const f32    name_x    = origin.x + std::max(2.0f, (cell_size.x - name_size.x) * 0.5f);
        const f32    name_y    = origin.y + cell_size.y - label_bottom_margin;
        draw->AddText(ImVec2(name_x, name_y), IM_COL32_WHITE, entry.name.c_str());
        draw->PopClipRect();

        // drag source -------------------------------------------------------------------------------------------------
        // If the dragged item is part of the current multi-selection, drag
        // only that item for now — see notes at the bottom of the message.
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {

            const std::string path_str = entry.path.string();
            ImGui::SetDragDropPayload(drag_payload_id, path_str.c_str(), path_str.size() + 1); // include null terminator
            ImGui::TextUnformatted(entry.name.c_str());
            ImGui::EndDragDropSource();
        }

        // per-item context menu ---------------------------------------------------------------------------------------
        if (ImGui::BeginPopupContextItem("##item_ctx")) {

            draw_item_context_menu(entry);
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    // context menus ---------------------------------------------------------------------------------------------------

    void content_browser_window::draw_background_context_menu() {

        if (ImGui::MenuItem("New Folder")) {

            std::error_code error{};
            std::filesystem::path candidate = m_current_dir / "New Folder";
            int suffix = 1;
            while (GLT::vfs::exists(candidate, error) && !error)
                candidate = m_current_dir / ("New Folder " + std::to_string(suffix++));

            GLT::vfs::create_directory(candidate, error);
            m_entries_dirty = true;
        }

        if (ImGui::MenuItem("Refresh"))
            m_entries_dirty = true;

        ImGui::Separator();

        if (ImGui::MenuItem("Open in Explorer")) {
            // TODO: platform-specific (ShellExecuteW / xdg-open / open).
        }
    }


    void content_browser_window::draw_item_context_menu(const dir_entry& entry) {

        if (ImGui::MenuItem("Open")) {
            if (entry.is_directory)
                navigate_to(entry.path);
            else {
                // TODO: open asset.
            }
        }

        if (ImGui::MenuItem("Rename")) {

            m_pending_rename_path = entry.path;
            std::snprintf(m_rename_buffer, sizeof(m_rename_buffer), "%s", entry.name.c_str());
            m_open_rename_popup = true;
        }

        if (ImGui::MenuItem("Delete")) {

            m_pending_delete_path = entry.path;
            m_open_delete_popup = true;
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Reveal in Explorer")) {
            // TODO: platform-specific.
        }

        if (ImGui::MenuItem("Copy Path")) {
            const std::string s = entry.path.string();
            ImGui::SetClipboardText(s.c_str());
        }
    }

    // modal popups ----------------------------------------------------------------------------------------------------

    void content_browser_window::draw_popups() {

        // OpenPopup must be issued at the same ID-stack level as BeginPopupModal,
        // so we defer it via flags set from the (deeper) context menus.
        if (m_open_rename_popup) {
            ImGui::OpenPopup("Rename##cb");
            m_open_rename_popup = false;
        }

        if (m_open_delete_popup) {
            ImGui::OpenPopup("Delete##cb");
            m_open_delete_popup = false;
        }

        // rename modal ------------------------------------------------------------------------------------------------
        if (ImGui::BeginPopupModal("Rename##cb", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {

            ImGui::TextUnformatted("New name:");
            ImGui::SetNextItemWidth(260.0f);
            ImGui::InputText("##cb_rename", m_rename_buffer, sizeof(m_rename_buffer));

            const bool confirm = ImGui::Button("OK", ImVec2(80, 0));
            ImGui::SameLine();
            const bool cancel  = ImGui::Button("Cancel", ImVec2(80, 0));

            if (confirm && m_rename_buffer[0] != '\0') {

                std::error_code error{};
                const auto target = m_pending_rename_path.parent_path() / m_rename_buffer;
                if (!GLT::vfs::exists(target, error) && !error) {
                    GLT::vfs::rename(m_pending_rename_path, target, error);
                }

                // If the renamed path was in the selection, drop it — the old
                // path no longer refers to anything.
                clear_selection();

                m_pending_rename_path.clear();
                m_entries_dirty = true;
                ImGui::CloseCurrentPopup();
            }
            if (cancel) {
                m_pending_rename_path.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // delete modal ------------------------------------------------------------------------------------------------
        if (ImGui::BeginPopupModal("Delete##cb", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {

            const std::string name = m_pending_delete_path.filename().string();
            ImGui::Text("Delete '%s'?", name.c_str());
            ImGui::TextDisabled("This cannot be undone.");

            if (ImGui::Button("Delete", ImVec2(80, 0))) {

                for (auto& path : m_selected_paths) {
                    
                    std::error_code error{};
                    GLT::vfs::remove(path, error);
                    VALIDATE(!error, continue, "", "Failed to delete [{}]", path.generic_string())
                }

                clear_selection();

                m_pending_delete_path.clear();
                m_entries_dirty = true;
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80, 0))) {

                m_pending_delete_path.clear();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    // navigation ------------------------------------------------------------------------------------------------------

    void content_browser_window::history_push(const std::filesystem::path& dir) {

        // Anything in front of m_history_index is now unreachable.
        m_history_size = m_history_index + 1;

        // Buffer full? Drop the oldest entry so the current one stays reachable.
        if (m_history_size >= k_history_capacity) {

            std::move(m_history.begin() + 1,
                      m_history.begin() + m_history_size,
                      m_history.begin());

            --m_history_index;
            --m_history_size;
        }

        m_history[++m_history_index] = dir;
        m_history_size = m_history_index + 1;
    }


    void content_browser_window::navigate_to(const std::filesystem::path& dir) {

        std::error_code error;
        if (!GLT::vfs::is_directory(dir, error))
            return;

        if (!m_current_dir.empty() && paths_equal(dir, m_current_dir))
            return;

        history_push(dir);

        m_current_dir = dir;
        clear_selection();
        m_entries_dirty = true;
    }


    void content_browser_window::navigate_back() {

        if (m_history_index <= 0)
            return;

        --m_history_index;
        m_current_dir = m_history[m_history_index];
        clear_selection();
        m_entries_dirty = true;
    }


    void content_browser_window::navigate_forward() {

        if (m_history_index + 1 >= m_history_size)
            return;

        ++m_history_index;
        m_current_dir = m_history[m_history_index];
        clear_selection();
        m_entries_dirty = true;
    }


    void content_browser_window::navigate_up() {

        if (m_current_dir.empty() || paths_equal(m_current_dir, m_content_dir))
            return;

        navigate_to(m_current_dir.parent_path());
    }


    void content_browser_window::refresh_directory_entries() {

        m_entries.clear();
        m_entries_dirty = false;
        if (m_current_dir.empty())
            return;

        std::error_code error{};

        auto iterator = GLT::vfs::directory_iterator(m_current_dir, error);
        for (auto& entry : iterator) {

            const auto& p = entry.path();
            if (is_hidden_entry(p)) continue;

            dir_entry loc_dir_entry{};
            loc_dir_entry.path = p;
            loc_dir_entry.name = p.filename().replace_extension("").string();
            loc_dir_entry.is_directory = entry.is_directory(error);

            if (!loc_dir_entry.is_directory) {

                std::string ext = p.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                loc_dir_entry.extension = std::move(ext);
            }

            m_entries.push_back(std::move(loc_dir_entry));
        }

        // Directories before files, then alphabetical within each group.
        std::sort(m_entries.begin(), m_entries.end(),
            [](const dir_entry& a, const dir_entry& b) {
                if (a.is_directory != b.is_directory) return a.is_directory;
                return a.name < b.name;
            });
    }

}
