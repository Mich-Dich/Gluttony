
#include "util/pch.h"
#include "content_browser.h"

#include <imgui.h>

#include <application.h>
#include <config/imgui_config.h>
#include <event/event_bus.h>
#include <plugin_system/i_asset_registry_plugin.h>

#include "util/event/asset_event.h"
#include "util/ui/pannel_collection.h"
#include "resource_manager/icon_manager.h"
#include "util/io/file_dialog.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    constexpr f32                               LEFT_PANEL_MIN_WIDTH = 160.0f;

    constexpr f32                               LEFT_PANEL_MAX_WIDTH = 600.0f;

    constexpr f32                               ICON_RENDER_SIZE = 56.0f;

    constexpr f32                               ICON_TOP_MARGIN = 8.0f;

    constexpr f32                               CELL_WIDTH = ICON_RENDER_SIZE + (ICON_TOP_MARGIN * 2);

    constexpr f32                               CELL_HEIGHT = CELL_WIDTH + (18.f);

    constexpr f32                               LABEL_BOTTOM_MARGIN = 20.0f;

    constexpr const char*                       DRAG_PAYLOAD_ID = "CONTENT_BROWSER_ITEM";

	const std::vector<std::pair<std::string, std::string>> POSSIBLE_IMPORT_TILE_TYPES = {

		//									mesh																 image
		{"All supported file types",    	"*.fbx;*.gltf;*.glb;*.obj;*.stl;*.3mf;*.dae;*.xml;*.ply;*.plyb;*.3ds;*.png;*.jpg;*.jpeg;*.jpe;*.tga;*.bmp;*.psd;*.gif;*.hdr;*.pic;*.ppm;*.pgm*.wav;*.ogg;*.mp3;*.flac"},

		// Common 3D meshes
		{"Mesh", 							"*.fbx;*.gltf;*.glb;*.obj;*.stl;*.3mf;*.dae;*.xml;*.ply;*.plyb;*.3ds;"},
		{"Image",                 			"*.png;*.jpg;*.jpeg;*.jpe;*.tga;*.bmp;*.psd;*.gif;*.hdr;*.pic;*.ppm;*.pgm"},					// all images
		{"Audio",                 			"*.wav;*.ogg;*.mp3;*.flac"},
    };

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    bool is_hidden_entry(const std::filesystem::path& p);

    bool paths_equal(const std::filesystem::path& a, const std::filesystem::path& b);

    // Maps a file extension to the icon that should represent it in the browser.
    icon_manager::icon extension_to_icon(const std::string& ext);

    // Categorizes a file by extension for the accent strip.
    asset_category categorize_extension(const std::string& ext);

    // Accent color for a category, chosen to read well against both the neutral cell background and the selection highlight.
    ImU32 category_accent_color(asset_category cat);

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


    icon_manager::icon extension_to_icon(const std::string& ext) {

        switch (categorize_extension(ext)) {

            case asset_category::image:     return icon_manager::icon::texture_big;
            case asset_category::world:     return icon_manager::icon::world;
            case asset_category::source:    return icon_manager::icon::script_big;
            case asset_category::material:  return icon_manager::icon::material_big;
            case asset_category::mesh:      return icon_manager::icon::mesh_asset_big;
            case asset_category::config:    return icon_manager::icon::settings;
            case asset_category::audio:     return icon_manager::icon::file_big;      // TODO: dedicated icon
            default:                        return icon_manager::icon::file_big;
        }
    }


    asset_category categorize_extension(const std::string& ext) {

        // Images
        if (ext == ".png"  || ext == ".jpg"  || ext == ".jpeg" ||
            ext == ".bmp"  || ext == ".tga"  || ext == ".hdr"  ||
            ext == ".psd"  || ext == ".gif"  || ext == ".pic"  || ext == ".pnm")
            return asset_category::image;

        // Worlds / levels
        if (ext == ".world" || ext == ".scene" || ext == ".level" || ext == ".map")
            return asset_category::world;

        // Source code, shaders, scripts
        if (ext == ".glsl" || ext == ".vert"  || ext == ".frag" || ext == ".comp" ||
            ext == ".cpp"  || ext == ".c"     || ext == ".h"    || ext == ".hpp"  ||
            ext == ".inl"  || ext == ".py"    || ext == ".cs"   || ext == ".lua")
            return asset_category::source;

        // Materials
        if (ext == ".mat" || ext == ".material" || ext == ".matinst")
            return asset_category::material;

        // Meshes
        if (ext == ".gltf" || ext == ".glb"  || ext == ".obj" ||
            ext == ".fbx"  || ext == ".dae"  || ext == ".ply" || ext == ".stl")
            return asset_category::mesh;

        // Config / data
        if (ext == ".json" || ext == ".yaml" || ext == ".yml" || ext == ".toml" ||
            ext == ".xml"  || ext == ".ini"  || ext == ".cfg")
            return asset_category::config;

        // Audio
        if (ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac")
            return asset_category::audio;

        return asset_category::other;
    }


    ImU32 category_accent_color(asset_category cat) {

        // Palette is deliberately desaturated: these hues need to sit
        // quietly next to a thumbnail or a folder icon without competing
        // for attention. Alphas are near-opaque so the strip stays legible
        // on top of the selection highlight.
        switch (cat) {

            case asset_category::image:    return IM_COL32( 91, 155, 213, 100);  // soft blue
            case asset_category::world:    return IM_COL32(224, 136,  64, 100);  // warm orange
            case asset_category::source:   return IM_COL32(103, 194, 106, 100);  // fresh green
            case asset_category::material: return IM_COL32(176, 107, 216, 100);  // muted violet
            case asset_category::mesh:     return IM_COL32( 77, 194, 194, 100);  // teal
            case asset_category::config:   return IM_COL32(224, 192,  70, 100);  // amber
            case asset_category::audio:    return IM_COL32(224, 122, 138, 100);  // salmon
            default:                      return IM_COL32(140, 140, 140, 100);  // neutral gray
        }
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

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    content_browser_window::content_browser_window() {

        make_window_name("Content Browser");

        m_content_dir = PROJECT_CONTENT_DIR;
        m_file_event_sub_handle = GLT::event_bus::subscribe<file_event>(std::bind_front(&content_browser_window::on_file_event, this));

        // Make sure the content root actually exists before we try to browse it.
        std::error_code error{};
        if (!m_content_dir.empty() && !GLT::vfs::exists(m_content_dir, error) && !error)
            GLT::vfs::create_directories(m_content_dir, error);

        // Route through navigate_to() so the history stack starts populated
        // (otherwise back/forward buttons would be permanently dead on launch).
        if (!m_content_dir.empty())
            navigate_to(m_content_dir);
    }


    content_browser_window::~content_browser_window() {

        // GLT::event_bus::unsubscribe(m_file_event_sub_handle);
    }

    // CLASS PUBLIC ====================================================================================================

    std::filesystem::path content_browser_window::selected_path() const {

        return m_selected_paths.empty() ? std::filesystem::path{} : m_selected_paths.front();
    }


    void content_browser_window::window(const f32 /*delta_time*/) {

        if (!m_show_window)
            return;

        // Process any thumbnails that finished loading since the last frame.
        icon_manager::flush_thumbnail_uploads();

        // Any state change marks the entry cache as stale
        if (m_entries_dirty)
            refresh_directory_entries();

        if (ImGui::Begin(m_window_id.c_str(), &m_show_window)) {
    
            ImGui::SetNextWindowSizeConstraints(ImVec2(LEFT_PANEL_MIN_WIDTH, 0), ImVec2(LEFT_PANEL_MAX_WIDTH, std::numeric_limits<f32>::max()));
            UI::custom_frame(200, true, ImGui::GetColorU32(GLT::imgui_config::get_default_gray1_ref()),
                [this]() { 
                    draw_directory_tree();
                },
                [this]() {
                    draw_toolbar();
                    ImGui::BeginChild("##view_of_dir", ImVec2(0, 0));
                    draw_file_view();
                    ImGui::EndChild();
                });
    
            draw_popups();
        }

        ImGui::End();
    }


    void content_browser_window::update(const f32 /*delta_time*/) {

        static u8 count = 0;
        if (count++ >= 9) {

            refresh_directory_entries();        // refresh every 10 frames
            count = 0;
        }
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

        // Deliberately do NOT update m_selection_anchor - repeated Shift+click
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
        if (ImGui::Button("Import##content_browser_import")) {

            const auto import_paths = GLT::editor::io::file_dialog_multi("Import asset", POSSIBLE_IMPORT_TILE_TYPES);
            if (!import_paths.empty()) {

                GLT::event_bus::post(GLT::editor::asset_import_request_event(import_paths, m_current_dir));
            }
        }

        ImGui::SameLine();
        draw_breadcrumbs();

        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputTextWithHint("##cb_search", "Search...", m_search_buffer, sizeof(m_search_buffer));

        ImGui::SameLine();
        if (ImGui::Button("Refresh")) 
            m_entries_dirty = true;
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

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.f, 0.f, 0.f, 0.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 0.f));

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
		ImGui::PopStyleVar();
		ImGui::PopStyleColor(3);
    }

    // directory tree --------------------------------------------------------------------------------------------------

    void content_browser_window::draw_directory_tree() {

        if (m_content_dir.empty())
            return;

        // --- Collapse All button, right-aligned on its own row ----------------
        constexpr const char* COLLAPSE_LABEL = "Collapse All";
        const f32 collapse_w = ImGui::CalcTextSize(COLLAPSE_LABEL).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        const f32 gap = ImGui::GetContentRegionAvail().x - collapse_w;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(gap, 0.0f));
        const bool force_collapse = ImGui::Button(COLLAPSE_LABEL);

        // --- Tree, starting on the next row -----------------------------------
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 10.0f);
        const auto subdirs = list_subdirectories(m_content_dir);
        if (subdirs.empty())
            ImGui::TextDisabled("(empty)");
        else
            for (const auto& sub : subdirs)
                draw_directory_tree_recursive(sub, force_collapse);
        ImGui::PopStyleVar();
    }


    void content_browser_window::draw_directory_tree_recursive(const std::filesystem::path& dir, const bool collapse_tree) {

        // TODO: cache per-node subdirectory lists - right now we re-scan the
        //       filesystem on every frame for every visible tree node.
        const auto subdirs = list_subdirectories(dir);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

        if (paths_equal(dir, m_current_dir))
            flags |= ImGuiTreeNodeFlags_Selected;

        if (subdirs.empty())
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        if (collapse_tree)                                          // Force this node closed while the collapse flag is live.
            ImGui::SetNextItemOpen(false, ImGuiCond_Always);
            
        ImGui::PushID(dir.string().c_str());

        const std::string label = dir.filename().empty() ? dir.string() : dir.filename().string();
        const bool open = ImGui::TreeNodeEx(label.c_str(), flags);

        // Only respond to clicks on the label area, not on the expand arrow.
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            navigate_to(dir);

        if (open && !subdirs.empty()) {
            for (const auto& sub : subdirs)
                draw_directory_tree_recursive(sub, collapse_tree);

            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    // file grid -------------------------------------------------------------------------------------------------------

    void content_browser_window::draw_file_view() {

        const std::string filter = m_search_buffer;
        const f32 avail = ImGui::GetContentRegionAvail().x;
        const i32 columns = std::max(1, static_cast<i32>(std::floor(avail / CELL_WIDTH) - 1));
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

        const ImVec2 cell_size(CELL_WIDTH, CELL_HEIGHT);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const bool selected = is_selected(entry.path);
        const bool button = ImGui::InvisibleButton("##cell", cell_size);
        const auto interaction = UI::get_mouse_interation_on_item();

        if (interaction == UI::mouse_interation::left_double_clicked) {

            // Double-click: reduce to a single selection, then act on it.
            // We deliberately ignore Ctrl/Shift here - the second click of
            // a double should not toggle the item back off or extend a range.
            select_single(entry.path);

            if (entry.is_directory)
                navigate_to(entry.path);

            else {

                // Ask the registry what a file with this extension actually maps to. If nothing does, fall back to 
                // the informational category - the editor will log "no editor registered" rather than silently open the wrong thing.
                GLT::asset::type resolved = GLT::asset::core_types::invalid;
                if (auto registry = GLT::asset::registry::get_ref()) {

                    if (auto loaded = registry->load(entry.path))
                        resolved = registry->info(*loaded).asset_type;
                    else
                        LOG(warn, "Failed to load [{}]", entry.path)
                }
                GLT::event_bus::post(asset_open_event{ resolved, entry.path });
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

        if (selected)
            draw->AddRectFilled(origin, cell_max, ImGui::GetColorU32(GLT::imgui_config::get_main_color_ref()), 4.0f);

        else if (interaction == UI::mouse_interation::hovered)
            draw->AddRectFilled(origin, cell_max, IM_COL32(90, 90, 90, 120), 4.0f);

        // file type accent --------------------------------------------------------------------------------------------
        // A thin colored strip along the bottom edge of the cell identifies the
        // file's category at a glance. Folders are skipped - their icon already
        // communicates what they are, and striping them would just add noise.
        if (!entry.is_directory) {

            const ImU32 accent = category_accent_color(categorize_extension(entry.extension));

            constexpr f32 STRIP_HEIGHT  = 2.5f;
            constexpr f32 STRIP_INSET_X = 6.0f;
            constexpr f32 STRIP_OFFSET_Y = 19.0f;

            const ImVec2 strip_min(origin.x + STRIP_INSET_X, cell_max.y - STRIP_OFFSET_Y - STRIP_HEIGHT);
            const ImVec2 strip_max(cell_max.x - STRIP_INSET_X, cell_max.y - STRIP_OFFSET_Y);
            draw->AddRectFilled(strip_min, strip_max, accent, STRIP_HEIGHT * 0.5f);
        }

        // icon / thumbnail --------------------------------------------------------------------------------------------
        bool drew_thumbnail = false;

        if (!entry.is_directory && GLT::render::is_image_extension(entry.extension)) {

            const auto thumb = icon_manager::get_thumbnail(entry.path);
            if (thumb.state == icon_manager::thumbnail_state::ready && thumb.image_size.x > 0.0f) {

                // Fit the thumbnail into the icon render box, preserving aspect ratio.
                const f32 scale = std::min(ICON_RENDER_SIZE / thumb.image_size.x, ICON_RENDER_SIZE / thumb.image_size.y);
                const f32 draw_w = thumb.image_size.x * scale;
                const f32 draw_h = thumb.image_size.y * scale;
                const ImVec2 t_min(
                    origin.x + (cell_size.x - draw_w) * 0.5f,
                    origin.y + ICON_TOP_MARGIN + (ICON_RENDER_SIZE - draw_h) * 0.5f);
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
                    origin.x + (cell_size.x - ICON_RENDER_SIZE) * 0.5f,
                    origin.y + ICON_TOP_MARGIN);
                const ImVec2 icon_max(
                    icon_min.x + ICON_RENDER_SIZE,
                    icon_min.y + ICON_RENDER_SIZE);

                draw->AddImage(icon.tex_ref, icon_min, icon_max, icon.uv0, icon.uv1, 
                    ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, .75f)));
            }
        }

        // filename, clipped to the cell -------------------------------------------------------------------------------
        draw->PushClipRect(ImVec2(origin.x + 10.f, origin.y), ImVec2(cell_max.x - 10.f, cell_max.y), true);
        const ImVec2 name_size = ImGui::CalcTextSize(entry.name.c_str());
        const f32    name_x    = origin.x + std::max(2.0f, (cell_size.x - name_size.x) * 0.5f);
        const f32    name_y    = origin.y + cell_size.y - LABEL_BOTTOM_MARGIN;
        draw->AddText(ImVec2(name_x, name_y), IM_COL32_WHITE, entry.name.c_str());
        draw->PopClipRect();

        // drag source -------------------------------------------------------------------------------------------------
        // If the dragged item is part of the current multi-selection, drag
        // only that item for now - see notes at the bottom of the message.
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {

            const std::string path_str = entry.path.string();
            ImGui::SetDragDropPayload(DRAG_PAYLOAD_ID, path_str.c_str(), path_str.size() + 1); // include null terminator
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

                // Ask the registry what a file with this extension actually maps to. If nothing does, fall back to 
                // the informational category - the editor will log "no editor registered" rather than silently open the wrong thing.
                GLT::asset::type resolved = GLT::asset::core_types::invalid;
                if (auto registry = GLT::asset::registry::get_ref()) {

                    if (auto loaded = registry->load(entry.path))
                        resolved = registry->info(*loaded).asset_type;
                    else
                        LOG(warn, "Failed to load [{}]", entry.path)
                }
                GLT::event_bus::post(asset_open_event{ resolved, entry.path });
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

                // If the renamed path was in the selection, drop it - the old
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
            if (is_hidden_entry(p))
                continue;

            dir_entry loc_dir_entry{};
            loc_dir_entry.path = p;
            loc_dir_entry.name = p.filename().replace_extension("").string();
            loc_dir_entry.is_directory = entry.is_directory(error);

            if (!loc_dir_entry.is_directory) {

                std::string ext = p.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                loc_dir_entry.extension = std::move(ext);
            }
            m_entries.push_back(std::move(loc_dir_entry));
        }

        // Directories before files, then alphabetical within each group.
        std::sort(m_entries.begin(), m_entries.end(),
            [](const dir_entry& a, const dir_entry& b) {
                if (a.is_directory != b.is_directory)
                    return a.is_directory;
                return a.name < b.name;
            });
    }


    void content_browser_window::import_files(const std::vector<std::filesystem::path>& paths) {
            
        if (paths.empty())
            return;

        auto registry = GLT::asset::registry::get_ref();
        VALIDATE(registry, return, "", "asset registry not available");

        for (const auto& src : paths) {

            // Ask the registry which factories can handle this file.
            auto bindings = registry->candidate_imports(src);
            VALIDATE(!bindings.empty(), continue, "", "content_browser: no factory for [{}]", src.generic_string());

            // Pick a target type. If multiple candidates, prompt (deferred to a
            // modal - for now take the first one).
            GLT::asset::type target = bindings.front().target_type;
            if (bindings.size() > 1) {
                // TODO: open a modal with one radio button per binding.
            }

            // TODO: create actual import wizard, currently just checking if file exits and the exiting -> need user input for the target location
            const std::filesystem::path target_path = m_current_dir / src.filename()
                .replace_extension(std::string(".") + std::string(GLT::asset::extension_for_type(target)));
            std::error_code error{};
            VALIDATE(!GLT::vfs::exists(target_path, error) && !error, continue, 
                "", "Asset under that name already exists [{}]", target_path.generic_string())

            // Fire and forget on the thread pool.
            // The Assimp parse and vertex processing happen on the worker thread.
            // The registry's load() mutex only serializes the final insertion,
            // which is microseconds of work.
            GLT::thread_pool::push([registry, src, target, target_path]() {

                auto result = registry->import(src, target, target_path);
                VALIDATE(result, return, "", "import [{}] → type {} failed", src.generic_string(), target.value);
                const GLT::asset::handle h = *result;
                LOG(info, "imported [{}] → [{}]", src.generic_string(), registry->info(h).virtual_path.generic_string());

                // Hop back to the main thread to touch ImGui / content browser state.
                GLT::thread_pool::push_main([registry, h, src]() {

                    // Example: navigate to / reveal the imported file.
                    if (const auto& info = registry->info(h); !info.virtual_path.empty()) {
                        // content_browser.reveal(info.virtual_path);
                    }
                });
            });
        }
    }


    void content_browser_window::on_file_event(const file_event& event) {

        LOG(info, "{}", event.to_string());
        m_entries_dirty = true;
    }

}
