
#include "util/pch.h"
#include "content_browser.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <application.h>
#include <config/imgui_config.h>
#include <event/event_bus.h>
#include <asset/i_asset_registry.h>
#include <asset/material.h>

#include "util/event/asset_event.h"
#include "util/ui/panel_collection.h"
#include "resource_manager/icon_manager.h"
#include "util/io/file_dialog.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    constexpr f32                                                   LEFT_PANEL_MIN_WIDTH = 160.0f;

    constexpr f32                                                   LEFT_PANEL_MAX_WIDTH = 600.0f;

    constexpr f32                                                   ICON_SIZE_MIN = 32.0f;     // grid zoom range
    constexpr f32                                                   ICON_SIZE_MAX = 128.0f;

    constexpr f32                                                   ICON_TOP_MARGIN = 8.0f;



    // Cell height reserves a name field below the 56px icon: 2px gap above it, 4px pad below, leaving 28px of usable height
    constexpr f32                                                   CELL_NAME_HEIGHT = 29.f;   // two lines of file name below the icon

    // constexpr f32                                                   LABEL_BOTTOM_MARGIN = 20.0f;

	const std::vector<std::pair<std::string, std::string>>          POSSIBLE_IMPORT_TILE_TYPES = {

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
    icon_manager::icon_type extension_to_icon(const std::string& ext);

    // Resolve the .glt_* extension on a browser entry to a live asset type.
    // Goes through the registry first so plugin-registered types resolve too;
    // falls back to the compile-time core-type table otherwise.
    GLT::asset::type resolve_entry_type(std::string_view ext);

    // Accent colour for one asset type, ready to hand to ImDrawList.
    ImU32 type_to_color(GLT::asset::type t);

    // HSV -> packed opaque RGBA. h in [0, 360), s and v in [0, 1].
    ImU32 hsv_to_u32(f32 h, f32 s, f32 v);

    std::vector<std::filesystem::path> list_subdirectories(const std::filesystem::path& dir);

    std::vector<std::filesystem::path> parse_drag_payload_impl(const ImGuiPayload* payload);

    // Greedy 2-line wrap for a filename cell.
    //
    // Break points are the usual "word separator" characters: whitespace, and punctuation that people use to join words inside
    // a single identifier ("my-long-asset", "wall_tile_01", "v2.3.4"). We pick the LAST break character whose prefix still fits
    // on line 1 — that keeps line 1 as full as possible, and line 2 carries the leftover
    //
    // If no break character fits on line 1, we fall back to a hard character chop
    // If line 2 still doesn't fit, it's truncated with a trailing "..."
    void wrap_to_two_lines(std::string_view text, const f32 max_width, std::string& out_line1, std::string& out_line2);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    bool is_hidden_entry(const std::filesystem::path& p) {

        const std::string name = p.filename().string();
        return !name.empty() && name.front() == '.';
    }


    bool paths_equal(const std::filesystem::path& a, const std::filesystem::path& b) {

        return a.lexically_normal() == b.lexically_normal();
    }


    icon_manager::icon_type extension_to_icon(const std::string& ext) {

        // Fast path: this is a ".glt_*" asset file.
        const GLT::asset::type t = resolve_entry_type(ext);
        if (t != GLT::asset::core_types::invalid) {

            switch (t.value) {

                case GLT::asset::core_types::texture2d.value:           [[fallthrough]];
                case GLT::asset::core_types::texture3d.value:           [[fallthrough]];
                case GLT::asset::core_types::cube_map.value:            return icon_manager::icon::texture_big;

                case GLT::asset::core_types::world.value:               [[fallthrough]];
                case GLT::asset::core_types::region.value:              return icon_manager::icon::world;

                case GLT::asset::core_types::material.value:            [[fallthrough]];
                case GLT::asset::core_types::material_instance.value:   return icon_manager::icon::material_big;

                case GLT::asset::core_types::static_mesh.value:         [[fallthrough]];
                case GLT::asset::core_types::procedural_mesh.value:     [[fallthrough]];
                case GLT::asset::core_types::dynamic_mesh.value:        [[fallthrough]];
                case GLT::asset::core_types::skeletal_mesh.value:       [[fallthrough]];
                case GLT::asset::core_types::mesh_collection.value:     return icon_manager::icon::mesh_asset_big;

                case GLT::asset::core_types::audio.value:               return icon_manager::icon::file_big;    // TODO: dedicated audio icon
                default:                                                return icon_manager::icon::file_big;
            }
        }

        // Not an asset — source code / shaders / scripts that live in the content dir but were never imported
        // These still deserve a distinct icon
        if (ext == ".glsl" || ext == ".vert"  || ext == ".frag" || ext == ".comp" ||
            ext == ".cpp"  || ext == ".c"     || ext == ".h"    || ext == ".hpp"  ||
            ext == ".inl"  || ext == ".py"    || ext == ".cs"   || ext == ".lua")
            return icon_manager::icon::script_big;

        return icon_manager::icon::file_big;
    }


    GLT::asset::type resolve_entry_type(std::string_view ext) {

        // Strip a leading '.' — the browser stores extensions with it, the registry's type_from_name() doesn't want it
        if (!ext.empty() && ext.front() == '.')
            ext.remove_prefix(1);

        if (ext.size() <= 4 || ext.substr(0, 4) != "glt_")
            return GLT::asset::core_types::invalid;

        // Plugin types live in the registry's runtime name map, not in the
        // compile-time reflection table. Ask it first, fall back to the constexpr core resolver
        if (auto registry = GLT::asset::registry::get_ref()) {
            if (auto t = registry->type_from_name(ext.substr(4)))
                return *t;
        }

        return GLT::asset::extension_to_type(ext);
    }


    ImU32 hsv_to_u32(const f32 h, const f32 s, const f32 v) {

        const f32 c = v * s;
        const f32 hp = h / 60.0f;
        const f32 x = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
        const int i = static_cast<int>(hp) % 6;

        f32 r = 0.0f, g = 0.0f, b = 0.0f;
        switch (i) {
            case 0: r = c; g = x; break;
            case 1: r = x; g = c; break;
            case 2: g = c; b = x; break;
            case 3: g = x; b = c; break;
            case 4: r = x; b = c; break;
            case 5: r = c; b = x; break;
        }

        const f32 m = v - c;
        return IM_COL32(
            static_cast<int>((r + m) * 255.0f + 0.5f),
            static_cast<int>((g + m) * 255.0f + 0.5f),
            static_cast<int>((b + m) * 255.0f + 0.5f),
            255);
    }


    ImU32 type_to_color(const GLT::asset::type t) {

        // Core types get a hand-tuned hue. Value is pinned at 1.0 (full brightness) and alpha at 255 (fully opaque)
        // the accent strip is meant to be read, not blended into the background
        //
        // Saturation is kept moderate so no colour comes out neon. Adjacent flavours within a family (e.g. the five mesh variants)
        // share a hue band but sit a few degrees apart so they're still distinguishable
        if (t.value < GLT::asset::CUSTOM_TYPE_BEGIN) {

            f32 h = 0.0f, s = 0.55f;

            switch (t.value) {

                // meshes — cyan / teal family
                case GLT::asset::core_types::static_mesh.value:         h = 185.0f; s = 0.55f; break;
                case GLT::asset::core_types::procedural_mesh.value:     h = 200.0f; s = 0.50f; break;
                case GLT::asset::core_types::dynamic_mesh.value:        h = 215.0f; s = 0.55f; break;
                case GLT::asset::core_types::skeletal_mesh.value:       h = 170.0f; s = 0.55f; break;
                case GLT::asset::core_types::mesh_collection.value:     h = 155.0f; s = 0.50f; break;

                // textures — blue family
                case GLT::asset::core_types::texture2d.value:           h = 210.0f; s = 0.55f; break;
                case GLT::asset::core_types::texture3d.value:           h = 225.0f; s = 0.55f; break;
                case GLT::asset::core_types::cube_map.value:            h = 240.0f; s = 0.50f; break;

                // materials — violet / purple family
                case GLT::asset::core_types::material.value:            h = 280.0f; s = 0.50f; break;
                case GLT::asset::core_types::material_instance.value:   h = 300.0f; s = 0.50f; break;

                // worlds — warm orange family
                case GLT::asset::core_types::world.value:               h =  30.0f; s = 0.65f; break;
                case GLT::asset::core_types::region.value:              h =  45.0f; s = 0.60f; break;

                // audio — pink / salmon
                case GLT::asset::core_types::audio.value:               h = 340.0f; s = 0.55f; break;

                // assorted
                case GLT::asset::core_types::anim.value:                h = 120.0f; s = 0.55f; break;
                case GLT::asset::core_types::light.value:               h =  55.0f; s = 0.60f; break;
                case GLT::asset::core_types::bvh.value:                 h =   0.0f; s = 0.45f; break;
                case GLT::asset::core_types::volume.value:              h = 130.0f; s = 0.45f; break;

                // "invalid" and anything else in the reserved range: neutral grey. `s = 0` means the hue is irrelevant
                default:                        h =   0.0f; s = 0.00f; break;
            }

            return hsv_to_u32(h, s, 1.0f);
        }

        // Plugin type. The registry hands out ids sequentially from CUSTOM_TYPE_BEGIN, so multiplying by the golden-ratio
        // conjugate and taking the fractional part gives a low-discrepancy walk of the hue circle: consecutive ids land far
        // apart, and ids that are powers of two apart also land far apart — no clustering like a plain hash can produce
        constexpr f32 GOLDEN_CONJ = 0.6180339887498948f;

        const f32 frac = static_cast<f32>(t.value) * GOLDEN_CONJ;
        const f32 hue = (frac - std::floor(frac)) * 360.0f;

        return hsv_to_u32(hue, 0.55f, 1.0f);
    }


    std::vector<std::filesystem::path> list_subdirectories(const std::filesystem::path& dir) {

        std::vector<std::filesystem::path> out;
        std::error_code error{};
        auto iterator = GLT::vfs::directory_iterator(dir, error);
        VALIDATE(!error, return {}, "", "Failed to create directory iterator for [{}]", (dir.generic_string()))
        for (auto& entry : iterator) {

            if (entry.is_directory(error) && !is_hidden_entry(entry.path()))
                out.push_back(entry.path());
        }

        std::sort(out.begin(), out.end());
        return out;
    }


    std::vector<std::filesystem::path> parse_drag_payload_impl(const ImGuiPayload* payload) {

        std::vector<std::filesystem::path> out;
        if (!payload || !payload->Data || payload->DataSize <= 0)
            return out;

        const char* p = static_cast<const char*>(payload->Data);
        const char* end = p + payload->DataSize;
        const char* line_begin = p;
        while (p < end) {

            const char c = *p;
            if (c == '\n' || c == '\0') {

                if (p > line_begin)
                    out.emplace_back(std::string(line_begin, p));
                line_begin = p + 1;
            }
            p++;
        }
        if (line_begin < end)
            out.emplace_back(std::string(line_begin, end));

        return out;
    }


    // Greedy 2-line wrap for a filename cell.
    //
    // Break points are the usual "word separator" characters: whitespace, and punctuation that people use to join words inside
    // a single identifier ("my-long-asset", "wall_tile_01", "v2.3.4"). We pick the LAST break character whose prefix still fits
    // on line 1 — that keeps line 1 as full as possible, and line 2 carries the leftover
    //
    // If no break character fits on line 1, we fall back to a hard character chop
    // If line 2 still doesn't fit, it's truncated with a trailing "..."
    void wrap_to_two_lines(std::string_view text, const f32 max_width, std::string& out_line1, std::string& out_line2) {

        out_line1.clear();
        out_line2.clear();

        if (text.empty())
            return;

        auto width_of = [](std::string_view s) {
            return ImGui::CalcTextSize(s.data(), s.data() + s.size()).x;
        };
        auto fits = [&](std::string_view s) { return width_of(s) <= max_width; };

        // Fast path: single line.
        if (fits(text)) {
            out_line1.assign(text);
            return;
        }

        const f32 ellipsis_w = ImGui::CalcTextSize("...").x;

        // Which characters count as a preferred break?
        //
        // Whitespace and "joining" punctuation are both fair game. We deliberately
        // exclude '.' when it looks like a file extension? No — that heuristic
        // would need the caller's knowledge of what's an extension and what isn't,
        // and dropping a break on ".",  for names like "v2.3.4" would just fall
        // through to the hard-chop path. Keeping '.' as a break char is the
        // lesser evil.
        auto is_break = [](char c) {
            switch (c) {
                case ' ':  case '\t':
                case '-':  case '_':
                case '.':  case ',':
                case '/':  case '\\':
                case ':':  case ';':
                case '+':  case '&':
                case '|':  case '@':
                    return true;
                default:
                    return false;
            }
        };

        // Scan left-to-right, remembering the last break whose prefix fits.
        // Stop at the first break whose prefix does NOT fit — anything further
        // right can only be wider, so it can't fit either.
        size_t best_break = std::string_view::npos;
        for (size_t i = 0; i < text.size(); ++i) {

            if (!is_break(text[i]))
                continue;

            if (fits(text.substr(0, i + 1)))
                best_break = i;
            else
                break;
        }

        auto truncate_with_ellipsis = [&](std::string_view s, std::string& out) {
            size_t n = s.size();
            while (n > 0) {
                const std::string_view candidate = s.substr(0, n);
                if (width_of(candidate) + ellipsis_w <= max_width)
                    break;
                --n;
            }
            out.assign(s.substr(0, n));
            if (n < s.size())
                out += "...";
        };

        std::string_view rest;

        if (best_break == std::string_view::npos) {

            // No usable break point anywhere on line 1 — hard-chop to the widest
            // prefix that fits and let the rest flow to line 2.
            size_t n = text.size();
            while (n > 1 && !fits(text.substr(0, n)))
                --n;

            out_line1.assign(text.substr(0, n));
            rest = text.substr(n);

        } else {

            const char brk = text[best_break];
            const bool is_whitespace = (brk == ' ' || brk == '\t');

            // Whitespace breaks are "consumed" — we drop the trailing space from
            // line 1 so it doesn't skew the centred layout. Other break chars
            // (hyphen, underscore, dot, …) stay attached to line 1 so the two
            // halves still read as a joined identifier: "my-long" / "asset-name"
            // is clearer than "my" / "-long-asset-name".
            const size_t line1_end = is_whitespace ? best_break : best_break + 1;

            out_line1.assign(text.substr(0, line1_end));
            rest = text.substr(best_break + 1);
        }

        // Strip any leading whitespace left over from the split, otherwise line 2
        // inherits a stray gap that misaligns the centred render.
        while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
            rest.remove_prefix(1);

        if (rest.empty())
            return;

        if (fits(rest))
            out_line2.assign(rest);
        else
            truncate_with_ellipsis(rest, out_line2);
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

        GLT::event_bus::unsubscribe(m_file_event_sub_handle);
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

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        const bool open = ImGui::Begin(m_window_id.c_str(), &m_show_window);
        ImGui::PopStyleVar();

        if (open) {

            // ---- folder sidebar (resizable) ----
            ImVec4 sidebar_bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
            sidebar_bg.x = std::max(0.f, sidebar_bg.x - 0.03f);
            sidebar_bg.y = std::max(0.f, sidebar_bg.y - 0.03f);
            sidebar_bg.z = std::max(0.f, sidebar_bg.z - 0.03f);

            ImGui::SetNextWindowSizeConstraints(ImVec2(LEFT_PANEL_MIN_WIDTH, 0), ImVec2(LEFT_PANEL_MAX_WIDTH, std::numeric_limits<f32>::max()));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, sidebar_bg);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 8.f));
            ImGui::BeginChild("##folders", ImVec2(200.f, 0.f), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_ResizeX);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            draw_directory_tree();
            ImGui::EndChild();

            // ---- toolbar, grid, status bar ----
            ImGui::SameLine(0.f, 0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 8.f));
            ImGui::BeginChild("##browser_main", ImVec2(0.f, 0.f), ImGuiChildFlags_AlwaysUseWindowPadding,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleVar();

            draw_toolbar();
            ImGui::Dummy(ImVec2(0.f, 2.f));

            // divider (4) + one frame-high row, plus the item spacing in between
            const f32 status_h = ImGui::GetFrameHeight() + 4.f + ImGui::GetStyle().ItemSpacing.y * 2.f;
            ImGui::BeginChild("##view_of_dir", ImVec2(0.f, -status_h), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
            m_shown_items = draw_file_view();
            ImGui::EndChild();

            draw_status_bar(m_shown_items);
            ImGui::EndChild();

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

    namespace {

        // Square button with an arrow, for back / forward / up
        bool nav_button(const char* id, const ImGuiDir dir, const bool enabled, const char* tooltip) {

            const f32 size = ImGui::GetFrameHeight();
            ImGui::BeginDisabled(!enabled);
            const bool clicked = GLT::UI::subtle_button(id, ImVec2(size, size));
            const ImVec2 min = ImGui::GetItemRectMin();
            const f32 font = ImGui::GetFontSize() * 0.7f;
            ImGui::RenderArrow(ImGui::GetWindowDrawList(), ImVec2(min.x + (size - font) * 0.5f, min.y + (size - font) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_Text), dir, 0.7f);
            ImGui::EndDisabled();

            if (enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", tooltip);
            return clicked;
        }

    }


    void content_browser_window::draw_toolbar() {

        const bool can_back = m_history_index > 0;
        const bool can_fwd = m_history_index >= 0 && (m_history_index + 1) < m_history_size;
        const bool can_up = !m_current_dir.empty() && !paths_equal(m_current_dir, m_content_dir);

        const f32 frame_h = ImGui::GetFrameHeight();
        const ImGuiStyle& style = ImGui::GetStyle();
        const f32 start_x = ImGui::GetCursorPosX();
        const f32 width = ImGui::GetContentRegionAvail().x;

        if (nav_button("##cb_back", ImGuiDir_Left, can_back, "Back"))       navigate_back();
        ImGui::SameLine(0.f, 2.f);
        if (nav_button("##cb_fwd", ImGuiDir_Right, can_fwd, "Forward"))     navigate_forward();
        ImGui::SameLine(0.f, 2.f);
        if (nav_button("##cb_up", ImGuiDir_Up, can_up, "Parent folder"))    navigate_up();

        // right side, laid out from the right edge: [search][refresh][Import]
        const char* import_label = "Import";
        const f32 import_w = ImGui::CalcTextSize(import_label).x + style.FramePadding.x * 4.f;
        const f32 search_w = std::clamp(width * 0.28f, 120.f, 240.f);
        const f32 right_w = search_w + frame_h + import_w + style.ItemSpacing.x * 2.f;

        // breadcrumbs fill the space in between
        ImGui::SameLine(0.f, 10.f);
        const f32 crumbs_w = std::max(1.f, start_x + width - right_w - ImGui::GetCursorPosX() - 10.f);     // 0 would mean "fill"
        ImGui::BeginChild("##crumbs", ImVec2(crumbs_w, frame_h), ImGuiChildFlags_None,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
        draw_breadcrumbs();
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::SetCursorPosX(start_x + width - right_w);
        UI::search_field("##cb_search", m_search_buffer, sizeof(m_search_buffer), "Search", search_w);

        ImGui::SameLine();
        if (UI::reset_button("##cb_refresh", frame_h, "Refresh"))
            m_entries_dirty = true;

        ImGui::SameLine();
        if (UI::accent_button(import_label, ImVec2(import_w, 0.f))) {

            const auto import_paths = GLT::editor::io::file_dialog_multi("Import asset", POSSIBLE_IMPORT_TILE_TYPES);
            if (!import_paths.empty())
                GLT::event_bus::post(GLT::editor::asset_import_request_event(import_paths, m_current_dir));
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Import files into this folder");
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

        auto crumb_label = [&](const size_t i) {
            const std::string name = crumbs[i].filename().string();
            return i == 0 ? std::string("Content") : (name.empty() ? std::string("<root>") : name);
        };

        // Too long? Drop folders from the left (after the root) and show "..." instead
        const f32 avail = ImGui::GetContentRegionAvail().x;
        const f32 sep_w = ImGui::GetFontSize() * 0.9f;
        auto width_from = [&](const size_t first) {
            f32 w = ImGui::CalcTextSize(crumb_label(0).c_str()).x + 12.f;
            for (size_t i = first; i < crumbs.size(); ++i)
                w += sep_w + ImGui::CalcTextSize(crumb_label(i).c_str()).x + 12.f;
            return w;
        };
        size_t first = 1;
        while (first + 1 < crumbs.size() && width_from(first) > avail)
            ++first;

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GLT::imgui_config::get_action_color_gray_hover_ref());
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, GLT::imgui_config::get_action_color_gray_active_ref());
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.f, ImGui::GetStyle().FramePadding.y));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));

        auto separator = [&](const bool ellipsis) {
            ImGui::SameLine();
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const f32 h = ImGui::GetFrameHeight();
            if (ellipsis) {
                ImGui::TextDisabled("...");
                ImGui::SameLine();
            }
            const ImVec2 arrow_pos = ImGui::GetCursorScreenPos();
            ImGui::RenderArrow(ImGui::GetWindowDrawList(), ImVec2(arrow_pos.x + 2.f, pos.y + (h - ImGui::GetFontSize() * 0.6f) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_TextDisabled), ImGuiDir_Right, 0.6f);
            ImGui::Dummy(ImVec2(sep_w, h));
            ImGui::SameLine();
        };

        for (size_t i = 0; i < crumbs.size(); ++i) {

            if (i > 0 && i < first)
                continue;
            if (i > 0)
                separator(i == first && first > 1);

            ImGui::PushID(static_cast<int>(i));
            const bool is_last = i + 1 == crumbs.size();
            if (is_last)
                ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::bold));
            const bool clicked = ImGui::Button(crumb_label(i).c_str());
            if (is_last)
                ImGui::PopFont();
            ImGui::PopID();

            if (clicked) {
                navigate_to(crumbs[i]);
                break;
            }
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
    }


    // directory tree --------------------------------------------------------------------------------------------------

    void content_browser_window::draw_directory_tree() {

        if (m_content_dir.empty())
            return;

        // --- header: "FOLDERS" + collapse button --------------------------------
        ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::small));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("FOLDERS");
        ImGui::PopFont();

        constexpr const char* COLLAPSE_LABEL = "Collapse";
        const f32 collapse_w = ImGui::CalcTextSize(COLLAPSE_LABEL).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - collapse_w));
        const bool force_collapse = UI::subtle_button(COLLAPSE_LABEL);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Collapse all folders");

        ImGui::Dummy(ImVec2(0.f, 2.f));

        // --- tree, the content root is the top node -------------------------------
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.f, 3.f));
        ImGui::PushStyleColor(ImGuiCol_Header, GLT::imgui_config::get_action_color00_faded_ref());
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, GLT::imgui_config::get_action_color_gray_hover_ref());
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, GLT::imgui_config::get_action_color_gray_active_ref());

        if (!force_collapse)
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        draw_directory_tree_recursive(m_content_dir, force_collapse);

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);
    }


    void content_browser_window::draw_directory_tree_recursive(const std::filesystem::path& dir, const bool collapse_tree) {

        // TODO: cache per-node subdirectory lists - right now we re-scan the
        //       filesystem on every frame for every visible tree node.
        const auto subdirs = list_subdirectories(dir);
        const bool is_root = paths_equal(dir, m_content_dir);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
            | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;

        if (paths_equal(dir, m_current_dir))
            flags |= ImGuiTreeNodeFlags_Selected;

        if (subdirs.empty())
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        if (collapse_tree && !is_root)                              // Force this node closed while the collapse flag is live.
            ImGui::SetNextItemOpen(false, ImGuiCond_Always);

        ImGui::PushID(dir.string().c_str());

        // leading spaces make room for the folder icon, drawn on top afterwards
        const f32 icon = ImGui::GetTextLineHeight();
        const int pad = static_cast<int>(std::ceil((icon + 5.f) / std::max(1.f, ImGui::CalcTextSize(" ").x)));
        const std::string name = is_root ? std::string("Content") : (dir.filename().empty() ? dir.string() : dir.filename().string());
        const std::string label = std::string(static_cast<size_t>(pad), ' ') + name;
        const bool open = ImGui::TreeNodeEx(label.c_str(), flags);

        // Only respond to clicks on the label area, not on the expand arrow.
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            navigate_to(dir);

        // folder icon
        {
            const ImVec2 min = ImGui::GetItemRectMin();
            const f32 x = min.x + ImGui::GetTreeNodeToLabelSpacing();
            const f32 y = min.y + ImGui::GetStyle().FramePadding.y;
            const auto icon_data = icon_manager::get((open && !subdirs.empty()) ? icon_manager::icon::folder_open : icon_manager::icon::folder);
            ImGui::GetWindowDrawList()->AddImage(icon_data.tex_ref, ImVec2(x, y), ImVec2(x + icon, y + icon), icon_data.uv0, icon_data.uv1);
        }

        // drop assets on a folder in the tree to move them there
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(GLT::UI::ASSET_DRAG_PAYLOAD))
                move_assets_to(parse_drag_payload_impl(payload), dir);
            ImGui::EndDragDropTarget();
        }

        if (open && !subdirs.empty()) {
            for (const auto& sub : subdirs)
                draw_directory_tree_recursive(sub, collapse_tree);

            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    // status bar ------------------------------------------------------------------------------------------------------

    void content_browser_window::draw_status_bar(const u32 shown_items) {

        const ImVec2 start = ImGui::GetCursorScreenPos();
        const f32 width = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddLine(start, ImVec2(start.x + width, start.y), ImGui::GetColorU32(ImGuiCol_Separator, 0.6f));
        ImGui::Dummy(ImVec2(0.f, 4.f));

        ImGui::AlignTextToFramePadding();
        if (m_selected_paths.empty())
            ImGui::TextDisabled("%u item%s", shown_items, shown_items == 1 ? "" : "s");
        else
            ImGui::TextDisabled("%u item%s  \u00b7  %zu selected", shown_items, shown_items == 1 ? "" : "s", m_selected_paths.size());

        constexpr f32 SLIDER_W = 110.f;
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - SLIDER_W));
        ImGui::SetNextItemWidth(SLIDER_W);
        ImGui::SliderFloat("##cb_zoom", &m_icon_size, ICON_SIZE_MIN, ICON_SIZE_MAX, "", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Icon size");
    }

    // file grid -------------------------------------------------------------------------------------------------------

    u32 content_browser_window::draw_file_view() {

        m_any_item_drop_target_active = false;

        // case insensitive search
        std::string filter = m_search_buffer;
        std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        const f32 cell_w = m_icon_size + ICON_TOP_MARGIN * 2.f;
        const f32 spacing = ImGui::GetStyle().ItemSpacing.x;
        const f32 avail = ImGui::GetContentRegionAvail().x;
        const i32 columns = std::max(1, static_cast<i32>(std::floor((avail + spacing) / (cell_w + spacing))));
        i32  col = 0;
        u32  shown = 0;

        for (const auto& entry : m_entries) {

            if (!filter.empty()) {
                std::string name = entry.name;
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (name.find(filter) == std::string::npos)
                    continue;
            }

            draw_file_item(entry);
            ++shown;

            if (++col < columns)
                ImGui::SameLine();
            else
                col = 0;
        }

        if (shown == 0) {
            if (filter.empty())
                UI::empty_state("This folder is empty", "Drop files here, use Import or right-click to create something");
            else
                UI::empty_state("Nothing matches your search", "Search looks at the names in this folder only");
        }

        // Background drop target. We allocate the item only while a drag is in flight, so normal clicks fall through to the grid and
        // the "click on empty space clears selection" path below still works
        if (!m_any_item_drop_target_active && ImGui::GetDragDropPayload() != nullptr) {

            const ImVec2 remaining = ImGui::GetContentRegionAvail();
            if (remaining.x > 0.0f && remaining.y > 0.0f) {

                ImGui::InvisibleButton("##cb_bg_drop", remaining);
                if (ImGui::BeginDragDropTarget()) {

                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(GLT::UI::ASSET_DRAG_PAYLOAD)) {
                        const auto sources = parse_drag_payload_impl(payload);
                        move_assets_to(sources, m_current_dir);
                    }
                    ImGui::EndDragDropTarget();
                }
            }
        }

        // Click on empty space (no item hovered) clears the selection
        // Modifier keys suppress this so Ctrl/Shift+click on empty space does nothing unexpected
        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsWindowHovered() 
            && !ImGui::IsAnyItemHovered() 
            && ImGui::IsMouseClicked(ImGuiMouseButton_Left) 
            && !io.KeyCtrl 
            && !io.KeyShift) {

            clear_selection();
        }

        // Right-click on empty space
        if (ImGui::BeginPopupContextWindow("##cb_bg", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {

            draw_background_context_menu();
            ImGui::EndPopup();
        }

        return shown;
    }


    void content_browser_window::draw_file_item(const dir_entry& entry) {

        ImGui::PushID(entry.path.string().c_str());

        const f32 icon_size = m_icon_size;
        const ImVec2 cell_size(icon_size + ICON_TOP_MARGIN * 2.f, icon_size + ICON_TOP_MARGIN * 2.f + CELL_NAME_HEIGHT);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const bool selected = is_selected(entry.path);
        const bool button = ImGui::InvisibleButton("##cell", cell_size);
        const auto interaction = UI::get_mouse_interation_on_item();

        if (interaction == UI::mouse_interation::left_double_clicked) {

            // Double-click: reduce to a single selection, then act on it
            // We deliberately ignore Ctrl/Shift here - the second click of a double should not toggle the item back off or extend a range
            select_single(entry.path);

            if (entry.is_directory)
                navigate_to(entry.path);

            else {

                GLT::asset::type resolved = GLT::asset::core_types::invalid;
                const auto project_relative_path = GLT::project::extract_path_from_project_content_dir(entry.path);
                if (auto registry = GLT::asset::registry::get_ref()) {

                    if (auto loaded = registry->load(project_relative_path))
                        resolved = registry->info(*loaded).asset_type;
                    else
                        LOG(warn, "Failed to load [{}]", entry.path)
                }
                GLT::event_bus::post(asset_open_event{ resolved, project_relative_path });
            }

        } else if(button) {

            // Single click: apply the modifier-aware selection policy
            const ImGuiIO& io = ImGui::GetIO();
            const bool ctrl = io.KeyCtrl;
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

        constexpr f32 CELL_ROUNDING = 6.f;
        if (selected) {
            draw->AddRectFilled(origin, cell_max, ImGui::GetColorU32(GLT::imgui_config::get_action_color00_faded_ref()), CELL_ROUNDING);
            draw->AddRect(origin, cell_max, ImGui::GetColorU32(GLT::imgui_config::get_action_color00_hover_ref()), CELL_ROUNDING, 0, 1.5f);
        } else if (interaction == UI::mouse_interation::hovered)
            draw->AddRectFilled(origin, cell_max, ImGui::GetColorU32(GLT::imgui_config::get_action_color_gray_hover_ref()), CELL_ROUNDING);

        // icon tile behind files (folders stand on their own)
        if (!entry.is_directory) {
            const ImVec2 tile_min(origin.x + ICON_TOP_MARGIN * 0.5f, origin.y + ICON_TOP_MARGIN * 0.5f);
            const ImVec2 tile_max(cell_max.x - ICON_TOP_MARGIN * 0.5f, origin.y + ICON_TOP_MARGIN * 1.5f + icon_size);
            draw->AddRectFilled(tile_min, tile_max, ImGui::GetColorU32(GLT::imgui_config::get_action_color_gray_default_ref()), CELL_ROUNDING - 2.f);
        }

        // file type accent --------------------------------------------------------------------------------------------
        if (!entry.is_directory) {

            const ImU32 accent = type_to_color(resolve_entry_type(entry.extension));

            constexpr f32 STRIP_HEIGHT = 3.f;
            const f32 tile_bottom = origin.y + ICON_TOP_MARGIN * 1.5f + icon_size;
            const ImVec2 strip_min(origin.x + ICON_TOP_MARGIN * 0.5f, tile_bottom - STRIP_HEIGHT);
            const ImVec2 strip_max(cell_max.x - ICON_TOP_MARGIN * 0.5f, tile_bottom);
            draw->AddRectFilled(strip_min, strip_max, accent, CELL_ROUNDING - 2.f, ImDrawFlags_RoundCornersBottom);
        }

        // icon / thumbnail --------------------------------------------------------------------------------------------
        bool drew_thumbnail = false;

        // Two sources of previews:
        //   - raw images that were dropped in but not yet imported  ->  STB fallback path
        //   - .glt_texture2d assets                                 ->  baked CHUNK_THUMBNAIL
        // Everything else (meshes, materials, folders, shaders …) falls through to the icon atlas
        if (!entry.is_directory) {

            const bool wants_thumbnail = GLT::render::is_image_extension(entry.extension) ||
                resolve_entry_type(entry.extension) == GLT::asset::core_types::texture2d;

            if (wants_thumbnail) {

                const auto thumb = icon_manager::get_thumbnail(entry.path);
                if (thumb.state == icon_manager::thumbnail_state::ready && thumb.image_size.x > 0.0f) {

                    const f32 scale = std::min(icon_size / thumb.image_size.x, icon_size / thumb.image_size.y);
                    const f32 draw_w = thumb.image_size.x * scale;
                    const f32 draw_h = thumb.image_size.y * scale;
                    const ImVec2 t_min(origin.x + (cell_size.x - draw_w) * 0.5f, 
                        origin.y + ICON_TOP_MARGIN + (icon_size - draw_h) * 0.5f);
                    const ImVec2 t_max(t_min.x + draw_w, t_min.y + draw_h);

                    draw->AddImage(thumb.tex_ref, t_min, t_max, thumb.uv0, thumb.uv1);
                    drew_thumbnail = true;
                }
            }
        }

        if (!drew_thumbnail) {

            const icon_manager::icon_data icon = icon_manager::get(
                entry.is_directory ? icon_manager::icon::folder_big : extension_to_icon(entry.extension));
            if (icon.image_size.x > 0.0f) {

                const ImVec2 icon_min(origin.x + (cell_size.x - icon_size) * 0.5f, origin.y + ICON_TOP_MARGIN);
                const ImVec2 icon_max(icon_min.x + icon_size, icon_min.y + icon_size);
                draw->AddImage(icon.tex_ref, icon_min, icon_max, icon.uv0, icon.uv1, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, .75f)));
            }
        }

        // filename, wrapped to at most two lines ----------------------------------------------------------------------

        // The name field is the band between the icon and the bottom of the cell, with a small gap above
        // Both single- and two-line names are drawn as a block that is vertically centred inside this band
        constexpr f32 NAME_PAD_X = 8.0f;
        constexpr f32 NAME_FIELD_GAP_ABOVE = 2.0f;
        constexpr f32 NAME_FIELD_GAP_BELOW = 4.0f;
        constexpr f32 NAME_LINE_TIGHTEN = 2.0f;
        constexpr f32 NAME_BLOCK_OFFSET_Y = 3.0f;           // Nudge the whole name block down

        const f32 name_max_w = cell_size.x - 2.0f * NAME_PAD_X;
        const f32 line_h = ImGui::GetTextLineHeight();
        const f32 line_advance = line_h - NAME_LINE_TIGHTEN;
        const f32 field_top = origin.y + ICON_TOP_MARGIN + icon_size + NAME_FIELD_GAP_ABOVE;
        const f32 field_bottom = cell_max.y - NAME_FIELD_GAP_BELOW;
        const f32 field_height = std::max(0.0f, field_bottom - field_top);

        std::string name_line1;
        std::string name_line2;
        wrap_to_two_lines(entry.name, name_max_w, name_line1, name_line2);

        draw->PushClipRect(ImVec2(origin.x + NAME_PAD_X, origin.y), ImVec2(cell_max.x - NAME_PAD_X, cell_max.y), true);

        auto draw_line_centered = [&](const std::string& string, const f32 y) {
            if (string.empty())
                return;
            const f32 w = ImGui::CalcTextSize(string.c_str()).x;
            const f32 x = origin.x + std::max(2.0f, (cell_size.x - w) * 0.5f);
            draw->AddText(ImVec2(x, y), IM_COL32_WHITE, string.c_str());
        };

        if (name_line2.empty()) {

            // Single line: centre the one line's height inside the field
            const f32 block_h = line_h;
            const f32 y = field_top + (field_height - block_h) * 0.5f + NAME_BLOCK_OFFSET_Y;
            draw_line_centered(name_line1, y);

        } else {

            // Two lines: centre the combined block, then stack the second line at the tightened advance
            const f32 block_h = line_h + line_advance;
            const f32 y1 = field_top + (field_height - block_h) * 0.5f + NAME_BLOCK_OFFSET_Y;
            const f32 y2 = y1 + line_advance;

            draw_line_centered(name_line1, y1);
            draw_line_centered(name_line2, y2);
        }

        draw->PopClipRect();

        // drag source -------------------------------------------------------------------------------------------------
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {

            std::string payload;
            const bool drag_multi = is_selected(entry.path) && m_selected_paths.size() > 1;

            if (drag_multi) {
                for (size_t i = 0; i < m_selected_paths.size(); ++i) {
                    if (i) 
                        payload += '\n';
                    payload += m_selected_paths[i].string();
                }
            } else
                payload = entry.path.string();

            ImGui::SetDragDropPayload(GLT::UI::ASSET_DRAG_PAYLOAD, payload.data(), payload.size());

            if (drag_multi)
                ImGui::Text("%zu items", m_selected_paths.size());
            else
                ImGui::TextUnformatted(entry.name.c_str());

            ImGui::EndDragDropSource();
        }

        // drop target (folders only) ----------------------------------------------------------------------------------
        if (entry.is_directory && ImGui::BeginDragDropTarget()) {

            m_any_item_drop_target_active = true;
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(GLT::UI::ASSET_DRAG_PAYLOAD)) {

                const auto sources = parse_drag_payload_impl(payload);
                move_assets_to(sources, entry.path);
            }
            ImGui::EndDragDropTarget();
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

        if (ImGui::MenuItem("Create Asset..."))
            GLT::event_bus::post(GLT::editor::asset_create_request_event(m_current_dir));

        if (ImGui::MenuItem("New Folder"))
            create_folder();

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

                GLT::asset::type resolved = GLT::asset::core_types::invalid;
                const auto project_relative_path = GLT::project::extract_path_from_project_content_dir(entry.path);
                auto registry = GLT::asset::registry::get_ref();
                if (!project_relative_path.empty() && registry) {

                    if (auto loaded = registry->load(project_relative_path))
                        resolved = registry->info(*loaded).asset_type;
                    else
                        LOG(warn, "Failed to load [{}]", project_relative_path)
                }
                GLT::event_bus::post(asset_open_event{ resolved, project_relative_path });
            }
        }

        if (ImGui::MenuItem("Rename")) {

            m_pending_rename_path = entry.path;
            std::snprintf(m_rename_buffer, sizeof(m_rename_buffer), "%s", entry.name.c_str());
            m_open_rename_popup = true;
            m_rename_focus_pending = true;
            m_rename_deletes_on_cancel = false;
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

        // OpenPopup must be issued at the same ID-stack level as BeginPopupModal, so we defer it via flags set from the (deeper) context menus
        if (m_open_rename_popup) {
            ImGui::OpenPopup("Rename##cb");
            m_open_rename_popup = false;
        }

        if (m_open_delete_popup) {
            ImGui::OpenPopup("Delete##cb");
            m_open_delete_popup = false;
        }

        // Both modals want to appear dead-centre of the main viewport. We recompute
        // this every frame so a window resize (or docking change) re-centres them
        // instead of leaving them stranded at their original screen pixel.
        const ImVec2 viewport_center = ImGui::GetMainViewport()->GetCenter();

        // rename modal ------------------------------------------------------------------------------------------------
        ImGui::SetNextWindowPos(viewport_center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Rename##cb", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {

            ImGui::TextUnformatted("New name:");

            // One-shot focus grab. SetKeyboardFocusHere() applies to the next item in the same frame; AutoSelectAll selects
            // the pre-filled text the moment focus lands
            if (m_rename_focus_pending) {
                ImGui::SetKeyboardFocusHere();
                m_rename_focus_pending = false;
            }

            ImGui::SetNextItemWidth(260.0f);
            ImGui::InputText("##cb_rename", m_rename_buffer, sizeof(m_rename_buffer),
                ImGuiInputTextFlags_AutoSelectAll);

            const bool confirm = ImGui::Button("OK", ImVec2(80, 0));
            ImGui::SameLine();
            const bool cancel = ImGui::Button("Cancel", ImVec2(80, 0));

            if (confirm && m_rename_buffer[0] != '\0') {

                auto registry = GLT::asset::registry::get_ref();
                if (registry) {
                    const auto target = m_pending_rename_path.parent_path() / m_rename_buffer;
                    if (auto res = registry->move(m_pending_rename_path, target); !res) {
                        LOG(warn, "rename [{}] -> [{}] failed (error {})",
                            m_pending_rename_path.generic_string(), target.generic_string(), GLT::util::enum_to_string(res.error()));
                    }
                }

                clear_selection();
                m_pending_rename_path.clear();
                m_rename_deletes_on_cancel = false;
                m_entries_dirty = true;
                ImGui::CloseCurrentPopup();
            }

            if (cancel) {

                // If we're cancelling out of the modal that was auto-opened by create_folder(), take the placeholder with us so the user
                // isn't left with a stray "New Folder"
                if (m_rename_deletes_on_cancel) {
                    std::error_code error{};
                    GLT::vfs::remove_all(m_pending_rename_path, error);
                    m_entries_dirty = true;
                }

                m_pending_rename_path.clear();
                m_rename_deletes_on_cancel = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // delete modal ------------------------------------------------------------------------------------------------
        ImGui::SetNextWindowPos(viewport_center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Delete##cb", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {

            const std::string name = m_pending_delete_path.filename().string();
            ImGui::Text("Delete '%s'?", name.c_str());
            ImGui::TextDisabled("This cannot be undone.");

            if (ImGui::Button("Delete", ImVec2(80, 0))) {

                for (auto& path : m_selected_paths) {
                    
                    std::error_code error{};
                    GLT::vfs::remove(path, error);
                    VALIDATE(!error, continue, "", "Failed to delete [{}]", (path.generic_string()))
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
            VALIDATE(!bindings.empty(), continue, "", "no factory for [{}]", (src.generic_string()))

            // Pick a target type. If multiple candidates, prompt (deferred to a
            // modal - for now take the first one).
            GLT::asset::type target = bindings.front().target_type;
            if (bindings.size() > 1) {
                // TODO: open a modal with one radio button per binding.
            }

            // TODO: create actual import wizard, currently just checking if file exits and the exiting -> need user input for the target location
            const std::filesystem::path target_path = m_current_dir / src.filename()
                .replace_extension(std::string(".") + std::string(GLT::asset::type_to_extension(target)));
            std::error_code error{};
            VALIDATE(!GLT::vfs::exists(target_path, error) && !error, continue, 
                "", "Asset under that name already exists [{}]", (target_path.generic_string()))

            // Fire and forget on the thread pool.
            // The Assimp parse and vertex processing happen on the worker thread.
            // The registry's load() mutex only serializes the final insertion,
            // which is microseconds of work.
            GLT::thread_pool::push([registry, src, target, target_path]() {

                auto result = registry->import(src, target, target_path);
                VALIDATE(result, return, "", "import [{}] -> type {} failed", (src.generic_string(), target.value))
                const GLT::asset::handle h = *result;
                LOG(info, "imported [{}] -> [{}]", src.generic_string(), registry->info(h).virtual_path.generic_string());

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


    void content_browser_window::on_file_event([[maybe_unused]] const file_event& event) {

        LOG(info, "{}", event.to_string());
        m_entries_dirty = true;
    }


    void content_browser_window::create_folder() {

        std::error_code error{};
        std::filesystem::path candidate = m_current_dir / "New Folder";
        for (u32 suffix = 1; GLT::vfs::exists(candidate, error) && !error; ++suffix)
            candidate = m_current_dir / ("New Folder " + std::to_string(suffix));

        GLT::vfs::create_directory(candidate, error);
        VALIDATE(!error, return, "", "failed to create folder [{}]", (candidate.generic_string()))

        m_entries_dirty = true;

        // Hand the freshly-created folder straight to the rename modal. The focus flag makes the input grab keyboard
        // focus and pre-select the default name so the user can just start typing
        m_pending_rename_path = candidate;
        std::snprintf(m_rename_buffer, sizeof(m_rename_buffer), "%s", candidate.filename().string().c_str());
        m_open_rename_popup = true;
        m_rename_focus_pending = true;
        m_rename_deletes_on_cancel = true;
    }


    void content_browser_window::create_material() {

        auto* registry = GLT::asset::registry::get_ptr();
        VALIDATE(registry, return, "", "no asset registry")

        // The picker speaks project-relative paths; m_current_dir is absolute. extract_path_from_project_content_dir gives us the registry's dialect
        const std::string stem = "New Material";
        const std::string ext = "." + std::string(GLT::asset::type_to_extension(GLT::asset::core_types::material));

        std::filesystem::path relative_target{};
        std::error_code error{};
        for (u32 suffix = 0; suffix < 1024; ++suffix) {

            const std::string filename = (suffix == 0) ? stem + ext : stem + " " + std::to_string(suffix) + ext;
            const auto candidate_abs = m_current_dir / filename;
            if (!GLT::vfs::exists(candidate_abs, error) && !error) {
                relative_target = GLT::project::extract_path_from_project_content_dir(candidate_abs);
                break;
            }
        }

        VALIDATE(!relative_target.empty(), return, "",
            "could not find a free filename for a new material in [{}]", (m_current_dir.generic_string()))

        // ---- build in-memory material ---------------------------------------
        auto asset = GLT::create_unique_ref<GLT::asset::material::material_asset>();
        asset->asset_type = GLT::asset::core_types::material;
        asset->textures.fill(INVALID_HANDLE);
        asset->name = relative_target.stem().string();

        // ---- register + persist -------------------------------------------------

        auto registered = registry->register_runtime(std::move(asset), relative_target, relative_target.stem().string());
        VALIDATE(registered.has_value(), return, 
            "", "register_runtime failed for [{}]", (relative_target.generic_string()))

        if (auto save_result = registry->save(*registered); !save_result) {

            LOG(error, "failed to write new material [{}] (error {})",
                relative_target.generic_string(), static_cast<int>(save_result.error()));
            // `registered` is an asset_ref, its destructor releases the slot
            return;
        }

        LOG(info, "created new material [{}]", relative_target.generic_string());
        m_entries_dirty = true;

        // Open the editor. Same event the double-click path uses, so the registry lookup, docking, and editor dispatch all stay consistent
        GLT::event_bus::post(asset_open_event{ GLT::asset::core_types::material, relative_target });
    }


    std::vector<std::filesystem::path> content_browser_window::parse_drag_payload(const ImGuiPayload* payload) { 

        return parse_drag_payload_impl(payload); 
    }


    void content_browser_window::move_assets_to(const std::vector<std::filesystem::path>& sources, const std::filesystem::path& target_dir) {

        if (sources.empty())
            return;

        auto registry = GLT::asset::registry::get_ref();
        VALIDATE(registry, return, "", "no asset registry");

        bool any_moved = false;

        for (const auto& src : sources) {

            const auto name = src.filename();
            if (name.empty())
                continue;

            const auto dst = target_dir / name;
            if (paths_equal(src, dst))
                continue;   // no-op

            // Refuse moving a directory into its own subtree
            std::error_code error{};
            if (GLT::vfs::is_directory(src, error) && !error) {

                const auto src_norm = src.lexically_normal();
                const auto dst_norm = dst.lexically_normal();
                auto si = src_norm.begin();
                auto di = dst_norm.begin();
                bool inside = true;
                for (; si != src_norm.end(); ++si, ++di) {
                    if (di == dst_norm.end() || *si != *di) {
                        inside = false;
                        break;
                    }
                }
                if (inside)
                    continue;
            }

            if (auto res = registry->move(src, dst); !res) {
                LOG(warn, "failed to move [{}] -> [{}] (error {})", src.generic_string(), dst.generic_string(), 
                    static_cast<int>(res.error()));
                continue;
            }

            LOG(info, "moved [{}] -> [{}]", src.generic_string(), dst.generic_string());
            any_moved = true;
        }

        if (any_moved) {
            // Selection points at paths that no longer exist
            clear_selection();
            m_entries_dirty = true;
        }
    }

}
