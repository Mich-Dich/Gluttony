
#include "util/pch.h"
#include "content_browser.h"

#include <imgui.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    namespace {
        constexpr f32                   k_left_panel_min_width = 160.0f;
        constexpr f32                   k_left_panel_max_width = 600.0f;
        constexpr f32                   k_cell_width = 96.0f;
        constexpr f32                   k_cell_height = 96.0f;
        constexpr const char*           k_drag_payload_id = "CONTENT_BROWSER_ITEM";
    }

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    bool is_hidden_entry(const std::filesystem::path& p);

    bool paths_equal(const std::filesystem::path& a, const std::filesystem::path& b);
    
    // Textual placeholders until the editor gets a real icon font.
    const char* icon_for_extension(const std::string& ext);
    
    std::vector<std::filesystem::path> list_subdirectories(const std::filesystem::path& dir);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    bool is_hidden_entry(const std::filesystem::path& p) {

        const std::string name = p.filename().string();
        return !name.empty() && name.front() == '.';
    }


    bool paths_equal(const std::filesystem::path& a, const std::filesystem::path& b) { 
        
        return a.lexically_normal() == b.lexically_normal();
    }


    const char* icon_for_extension(const std::string& ext) {

        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
            ext == ".bmp" || ext == ".tga" || ext == ".hdr")
            return "IMG";

        if (ext == ".gltf" || ext == ".glb" || ext == ".obj" || ext == ".fbx")
            return "3D";

        if (ext == ".glsl" || ext == ".vert" || ext == ".frag" || ext == ".comp")
            return "SHD";

        if (ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".c" || ext == ".inl")
            return "SRC";

        if (ext == ".json" || ext == ".yaml" || ext == ".yml" || ext == ".toml")
            return "CFG";

        if (ext == ".wav" || ext == ".ogg" || ext == ".mp3")
            return "SND";

        return "---";
    }


    std::vector<std::filesystem::path> list_subdirectories(const std::filesystem::path& dir) {

        std::vector<std::filesystem::path> out{};
        std::error_code error{};

        auto iterator = GLT::vfs::directory_iterator(dir, error);
        if (!error) {
            for (auto& entry : iterator)
                if ((entry.is_directory(error) && !error) && !is_hidden_entry(entry.path()))
                    out.push_back(entry.path());
        }

        std::sort(out.begin(), out.end());
        return out;
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    content_browser_window::content_browser_window() {

        make_window_name("Content Browser");
    }


    content_browser_window::~content_browser_window() { }

    // CLASS PUBLIC ====================================================================================================

    void content_browser_window::window(const f32 delta_time) {

        if (!m_show_window)
            return;

        ImGui::Begin(m_window_title.c_str(), &m_show_window);

        // First-frame bootstrap: pick a sensible content root.
        if (m_content_dir.empty()) {

            std::error_code error{};
            m_content_dir = GLT::util::get_executable_path() / GLT::config::CONTENT_DIR;
            if (!GLT::vfs::exists(m_content_dir, error))
                GLT::vfs::create_directories(m_content_dir, error);

            navigate_to(m_content_dir);
        }

        // Any state change marks the entry cache as stale; we rebuild it here
        // rather than inside each draw call so a single frame never scans the
        // filesystem more than once.
        if (m_entries_dirty) {
            refresh_directory_entries();
        }

        draw_toolbar();
        ImGui::Separator();

        // ---- split layout --------------------------------------------------
        ImGui::BeginChild("##cb_tree", ImVec2(m_left_panel_width, 0.0f), true);
        draw_directory_tree();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##cb_files", ImVec2(0.0f, 0.0f), true);
        draw_file_view();
        ImGui::EndChild();

        draw_popups();

        ImGui::End();
    }


    void content_browser_window::update(const f32 delta_time) {

        // All filesystem work happens on the frame the state becomes dirty;
        // nothing to do per-frame here yet. If you later add async scanning,
        // this is where the completion callback would land.
    }


    bool content_browser_window::serialize(const std::filesystem::path& project_file, const GLT::serializer::option option) {

    }

    // CLASS PROTECTED =================================================================================================

    // void content_browser_window::make_window_name(const char* /*base_name*/) {
    //
    //     ASSERT(base_name, "", "make_window_name() base_name must not be null");
    //     m_window_title = base_name;
    //     m_window_id = std::string(base_name) + "##" + GLT::util::to_string(m_index);
    // }

    // CLASS PRIVATE ===================================================================================================

    // ---- toolbar & breadcrumbs --------------------------------------------------------------------------------------

    void content_browser_window::draw_toolbar() {

        const bool can_back = m_history_index > 0;
        const bool can_fwd  = m_history_index >= 0 && m_history_index + 1 < static_cast<i32>(m_history.size());
        const bool can_up   = !m_current_dir.empty() && !paths_equal(m_current_dir, m_content_dir);

        ImGui::BeginDisabled(!can_back);
        if (ImGui::Button("<##cb_back")) navigate_back();
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!can_fwd);
        if (ImGui::Button(">##cb_fwd")) navigate_forward();
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!can_up);
        if (ImGui::Button("^##cb_up")) navigate_up();
        ImGui::EndDisabled();

        ImGui::SameLine();
        draw_breadcrumbs();

        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputTextWithHint("##cb_search", "Search...", m_search_buffer, sizeof(m_search_buffer));

        ImGui::SameLine();
        if (ImGui::Button("Refresh")) m_entries_dirty = true;

        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragFloat("##cb_split", &m_left_panel_width, 1.0f, k_left_panel_min_width, k_left_panel_max_width, "%.0f");
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

    // ---- directory tree ---------------------------------------------------------------------------------------------

    void content_browser_window::draw_directory_tree() {

        if (m_content_dir.empty())
            return;

        ImGui::TextUnformatted("Folders");
        ImGui::Separator();

        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 10.0f);
        draw_directory_tree_recursive(m_content_dir);
        ImGui::PopStyleVar();
    }


    void content_browser_window::draw_directory_tree_recursive(const std::filesystem::path& dir) {

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

    // ---- file grid --------------------------------------------------------------------------------------------------

    void content_browser_window::draw_file_view() {

        const std::string filter = m_search_buffer;
        const f32 avail = ImGui::GetContentRegionAvail().x;
        const i32 columns = std::max(1, static_cast<i32>(avail / k_cell_width));
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

        // Right-click on empty space.
        if (ImGui::BeginPopupContextWindow("##cb_bg", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {

            draw_background_context_menu();
            ImGui::EndPopup();
        }
    }


    void content_browser_window::draw_file_item(const dir_entry& entry) {

        ImGui::PushID(entry.path.string().c_str());

        const ImVec2 cell_size(k_cell_width, k_cell_height);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const bool selected = !m_selected_path.empty() && paths_equal(entry.path, m_selected_path);

        if (ImGui::InvisibleButton("##cell", cell_size)) {

            m_selected_path = entry.path;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (entry.is_directory)
                    navigate_to(entry.path);
                else {
                    // TODO: broadcast an "open asset" event to the editor.
                }
            }
        }

        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 cell_max(origin.x + cell_size.x, origin.y + cell_size.y);

        // Background highlight.
        if (selected)
            draw->AddRectFilled(origin, cell_max, IM_COL32(60, 90, 150, 200), 4.0f);
        else if (hovered)
            draw->AddRectFilled(origin, cell_max, IM_COL32(90, 90, 90, 120), 4.0f);

        // "Icon" placeholder.
        const char*  glyph      = entry.is_directory ? "DIR" : icon_for_extension(entry.extension);
        const ImVec2 glyph_size = ImGui::CalcTextSize(glyph);
        draw->AddText(ImVec2(origin.x + (cell_size.x - glyph_size.x) * 0.5f,
            origin.y + (cell_size.y - glyph_size.y) * 0.5f - 10.0f), 
            IM_COL32_WHITE, glyph);

        // Filename, clipped to the cell.
        draw->PushClipRect(origin, cell_max, true);
        const ImVec2 name_size = ImGui::CalcTextSize(entry.name.c_str());
        const f32    name_x    = origin.x + std::max(2.0f, (cell_size.x - name_size.x) * 0.5f);
        draw->AddText(ImVec2(name_x, origin.y + cell_size.y - 20.0f), IM_COL32_WHITE, entry.name.c_str());
        draw->PopClipRect();

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {     // Drag source so other editor windows can accept the path.

            const std::string path_str = entry.path.string();
            ImGui::SetDragDropPayload(k_drag_payload_id, path_str.c_str(), path_str.size() + 1); // include null terminator
            ImGui::TextUnformatted(entry.name.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginPopupContextItem("##item_ctx")) {                           // Per-item context menu.

            draw_item_context_menu(entry);
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    // ---- context menus ----------------------------------------------------------------------------------------------

    void content_browser_window::draw_background_context_menu() {

        if (ImGui::MenuItem("New Folder")) {

            std::error_code error{};
            std::filesystem::path candidate = m_current_dir / "New Folder";
            int suffix = 1;
            while (GLT::vfs::exists(candidate, error))
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

    // ---- modal popups -----------------------------------------------------------------------------------------------

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

        // ---- rename modal ------------------------------------------------
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
                if (!GLT::vfs::exists(target, error) && !error)
                    GLT::vfs::rename(m_pending_rename_path, target, error);

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

        // ---- delete modal ------------------------------------------------
        if (ImGui::BeginPopupModal("Delete##cb", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {

            const std::string name = m_pending_delete_path.filename().string();
            ImGui::Text("Delete '%s'?", name.c_str());
            ImGui::TextDisabled("This cannot be undone.");

            if (ImGui::Button("Delete", ImVec2(80, 0))) {

                std::error_code error{};
                GLT::vfs::remove_all(m_pending_delete_path, error);
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

    // ---- navigation -------------------------------------------------------------------------------------------------

    void content_browser_window::navigate_to(const std::filesystem::path& dir) {

        std::error_code error;
        if (!GLT::vfs::is_directory(dir, error))
            return;

        if (!m_current_dir.empty() && paths_equal(dir, m_current_dir))
            return;

        // Any new navigation truncates the forward history.
        if (m_history_index + 1 < static_cast<i32>(m_history.size()))
            m_history.erase(m_history.begin() + m_history_index + 1, m_history.end());

        m_history.push_back(dir);
        m_history_index = static_cast<i32>(m_history.size()) - 1;

        m_current_dir   = dir;
        m_selected_path.clear();
        m_entries_dirty = true;
    }


    void content_browser_window::navigate_back() {

        if (m_history_index <= 0)
            return;

        --m_history_index;
        m_current_dir   = m_history[m_history_index];
        m_selected_path.clear();
        m_entries_dirty = true;
    }


    void content_browser_window::navigate_forward() {

        if (m_history_index + 1 >= static_cast<i32>(m_history.size()))
            return;

        ++m_history_index;
        m_current_dir   = m_history[m_history_index];
        m_selected_path.clear();
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
        if (!error) {

            for (auto& entry : iterator) {

                const auto& p = it->path();
                if (is_hidden_entry(p))
                    continue;

                if (!entry.is_directory()) {

                    std::string ext = p.extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    entry.extension = std::move(ext);
                }
                m_entries.push_back(entry);
            }
        }

        // Directories before files, then alphabetical within each group.
        std::sort(m_entries.begin(), m_entries.end(),
            [](const dir_entry& a, const dir_entry& b) {
                if (a.is_directory != b.is_directory) 
                    return a.is_directory;

                return a.name < b.name;
            });
    }

}
