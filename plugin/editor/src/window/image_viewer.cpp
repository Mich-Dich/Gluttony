
#include "util/pch.h"
#include "image_viewer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <config/imgui_config.h>

#include <resource_manager/icon_manager.h>
#include "util/ui/pannel_collection.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    constexpr f32                               DETAILS_PANEL_WIDTH = 400.0f;

    constexpr f32                               MIN_ZOOM = 0.05f;
    
    constexpr f32                               MAX_ZOOM = 32.0f;
    
    constexpr f32                               ZOOM_STEP = 1.15f;
    
    constexpr f32                               FIT_PADDING = 0.95f;

    constexpr const char*                       DRAG_PAYLOAD_ID = "CONTENT_BROWSER_ITEM";

    constexpr f32                               CHECKER_CELL_SIZE = 12.0f;


    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    std::string to_lower(std::string s);

    std::string pretty_format(const std::string& ext);

    void draw_checkerboard(ImDrawList* draw, const ImVec2& min, const ImVec2& max);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // Lowercase a copy — used to normalise file extensions.
    std::string to_lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }


    // Convert ".[ext]" to a short human-readable format tag.
    std::string pretty_format(const std::string& ext) {

        if (ext == ".png")                      return "PNG";
        if (ext == ".jpg" || ext == ".jpeg")    return "JPEG";
        if (ext == ".bmp")                      return "BMP";
        if (ext == ".tga")                      return "TGA";
        if (ext == ".hdr")                      return "HDR";
        if (ext == ".psd")                      return "PSD";
        if (ext == ".gif")                      return "GIF";
        if (ext == ".pic" || ext == ".pnm")     return "PNM";
        if (ext.empty())                        return "Unknown";

        return to_lower(ext.substr(1));     // strip the leading dot
    }

    // Checkerboard painted behind the image so alpha reads correctly.
    void draw_checkerboard(ImDrawList* draw, const ImVec2& min, const ImVec2& max) {

        const ImU32 dark  = IM_COL32(58, 58, 58, 255);
        const ImU32 light = IM_COL32(88, 88, 88, 255);

        const i32 x0 = static_cast<i32>(std::floor(min.x / CHECKER_CELL_SIZE));
        const i32 y0 = static_cast<i32>(std::floor(min.y / CHECKER_CELL_SIZE));
        const i32 x1 = static_cast<i32>(std::ceil (max.x / CHECKER_CELL_SIZE));
        const i32 y1 = static_cast<i32>(std::ceil (max.y / CHECKER_CELL_SIZE));

        draw->PushClipRect(min, max, true);
        for (i32 y = y0; y < y1; ++y) {
            for (i32 x = x0; x < x1; ++x) {

                const bool alt = ((x + y) & 1) != 0;
                const ImVec2 c0(x * CHECKER_CELL_SIZE, y * CHECKER_CELL_SIZE);
                const ImVec2 c1(c0.x + CHECKER_CELL_SIZE, c0.y + CHECKER_CELL_SIZE);
                draw->AddRectFilled(c0, c1, alt ? light : dark);
            }
        }
        draw->PopClipRect();
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    image_viewer_window::image_viewer_window(const std::filesystem::path& path) {

        make_window_name("Image Viewer");
        open(path);
    }


    image_viewer_window::~image_viewer_window() { }

    // CLASS PUBLIC ====================================================================================================

    void image_viewer_window::open(const std::filesystem::path& path) {

        // Reset state up-front so a failed load never leaves the previous
        // image visible.
        m_details     = {};
        m_has_image   = false;
        m_load_failed = false;
        m_show_window = true;

        m_details.path      = path;
        m_details.name      = path.filename().string();
        m_details.extension = to_lower(path.extension().string());
        m_details.format    = pretty_format(m_details.extension);

        std::error_code error{};
        if (!GLT::vfs::exists(path, error) || error) {

            m_load_failed = true;
            LOG(warn, "image_viewer_window: file not found [{}]", path.generic_string());
            return;
        }

        if (GLT::vfs::is_directory(path, error)) {

            m_load_failed = true;
            LOG(warn, "image_viewer_window: path is a directory [{}]", path.generic_string());
            return;
        }

        populate_details();
        m_image = GLT::create_unique_ref<GLT::render::image>(path);

        m_has_image   = true;
        m_fit_pending = true;
    }


    void image_viewer_window::close_image() {

        m_image.reset();
        m_details = {};
        m_has_image = false;
        m_load_failed = false;
    }


    void image_viewer_window::window(const f32 /*delta_time*/) {

        if (!m_show_window)
            return;

        apply_pending_dock(); 
        ImGui::SetNextWindowSizeConstraints(ImVec2(640.0f, 400.0f),  ImVec2(std::numeric_limits<f32>::max(), std::numeric_limits<f32>::max()));

        if (ImGui::Begin(m_window_id.c_str(), &m_show_window)) {

            handle_drag_drop();

            // Same layout pattern as content_browser_window: a splitter with
            // a caller-supplied left panel width and per-side callbacks.

            UI::custom_frame(DETAILS_PANEL_WIDTH, true, ImGui::GetColorU32(GLT::imgui_config::get_default_gray1_ref()),
                [this]() {
                    draw_details_panel();
                },
                [this]() {
                    draw_image_panel();
                });
        }

        ImGui::End();
    }


    void image_viewer_window::update(const f32 /*delta_time*/) {
        // Nothing to update per frame — decoding is driven by the icon
        // manager's async queue and consumed inside draw_canvas().
    }


    bool image_viewer_window::serialize(const std::filesystem::path& /*project_file*/, const GLT::serializer::option /*option*/) { return false; }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    // details panel ---------------------------------------------------------------------------------------------------

    void image_viewer_window::draw_details_panel() {

        // ImGui::Dummy(ImVec2(0.0f, 8.0f));

        // Title & path ---------------------------------------------------------------------------------------------
        const std::string title = m_details.name.empty() ? std::string("(no image)") : m_details.name;
        ImGui::TextUnformatted(title.c_str());

        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", m_details.path.generic_string().c_str());
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        // Empty / failure states -----------------------------------------------------------------------------------
        if (m_load_failed) {

            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 90, 90, 255));
            ImGui::TextUnformatted("Failed to open file.");
            ImGui::PopStyleColor();
            ImGui::Unindent(12.0f);
            return;
        }

        if (!m_has_image) {

            ImGui::TextDisabled("No image loaded.");
            ImGui::Unindent(12.0f);
            return;
        }

        // File section ---------------------------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("File", ImGuiTreeNodeFlags_DefaultOpen)) {

            if (UI::begin_table("details", false)) {
                
                UI::table_row("name",   m_details.name.c_str());
                UI::table_row("format", m_details.format.c_str());
                UI::table_row("size",   GLT::util::format_bytes(m_details.file_size));
                UI::end_table();
            }
        }

        // Image section --------------------------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("Image", ImGuiTreeNodeFlags_DefaultOpen)) {

            if (UI::begin_table("details", false)) {
                
                UI::table_row("width",      m_details.width     ? std::to_string(m_details.width)     : std::string_view("--"));
                UI::table_row("height",     m_details.height    ? std::to_string(m_details.height)    : std::string_view("--"));
                UI::table_row("channels",   m_details.channels  ? std::to_string(m_details.channels)  : std::string_view("--"));
                UI::table_row("bit depth",  m_details.bit_depth ? std::to_string(m_details.bit_depth) : std::string_view("--"));
                UI::table_row("mip levels", std::to_string(m_details.mip_levels));
                UI::end_table();
            }
        }

        // Flags ----------------------------------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("Flags", ImGuiTreeNodeFlags_DefaultOpen)) {

            if (UI::begin_table("details", false)) {
                
                UI::table_row("HDR",   m_details.is_hdr    ? std::string_view("yes") : std::string_view("no"));
                UI::table_row("alpha", m_details.has_alpha ? std::string_view("yes") : std::string_view("no"));
                UI::table_row("sRGB",  m_details.is_srgb   ? std::string_view("yes") : std::string_view("no"));
                UI::end_table();
            }
        }
    }

    // image panel -----------------------------------------------------------------------------------------------------

    void image_viewer_window::draw_image_panel() {

        draw_image_toolbar();
        ImGui::Separator();
        draw_canvas();
    }


    void image_viewer_window::draw_image_toolbar() {

        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ImGui::SameLine();

        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::SliderFloat("##iv_zoom", &m_zoom, MIN_ZOOM, MAX_ZOOM, "%.2fx"))
            m_fit_pending = false;

        ImGui::SameLine();
        if (ImGui::Button("Fit"))
            m_fit_pending = true;

        ImGui::SameLine();
        if (ImGui::Button("1:1")) {

            m_fit_pending = false;
            m_zoom        = 1.0f;
            m_pan         = ImVec2(0.0f, 0.0f);
        }

        ImGui::SameLine();
        ImGui::Checkbox("Checker", &m_show_checker);
    }


    void image_viewer_window::draw_canvas() {

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
        const ImVec2 canvas_max = ImVec2(canvas_min.x + avail.x, canvas_min.y + avail.y);

        // Reserve the canvas so we get hover/active state for pan & zoom.
        ImGui::InvisibleButton("##iv_canvas", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);

        ImDrawList* draw = ImGui::GetWindowDrawList();

        // Solid backdrop; the checkerboard (if enabled) is drawn only behind
        // the image itself so the surrounding area stays neutral.
        // draw->AddRectFilled(canvas_min, canvas_max, IM_COL32(28, 28, 30, 255));

        if (m_load_failed) {
            draw_error_state(draw, canvas_min, canvas_max);
            return;
        }

        if (!m_has_image) {
            draw_empty_state(draw, canvas_min, canvas_max);
            return;
        }

        if (!m_image->get_descriptor_set()) {       // Image not ready yet

            const char* msg = "Loading...";
            const ImVec2 sz = ImGui::CalcTextSize(msg);
            draw->AddText(ImVec2((canvas_min.x + canvas_max.x - sz.x) * 0.5f, (canvas_min.y + canvas_max.y - sz.y) * 0.5f), 
                IM_COL32(200, 200, 200, 255), msg);
            return;
        }

        // Keep the reported size in sync with the decoded image — cheap, and
        // lets the details panel fill in width/height once the decode lands.
        m_details.width  = static_cast<u32>(m_image->get_size().x);
        m_details.height = static_cast<u32>(m_image->get_size().y);

        // Input: wheel-zoom, drag-pan ---------------------------------------------------------------
        const bool hovered = ImGui::IsItemHovered();
        const bool active  = ImGui::IsItemActive();

        if (hovered) {

            const f32 wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {

                m_zoom = std::clamp(m_zoom * (wheel > 0.0f ? ZOOM_STEP : 1.0f / ZOOM_STEP), MIN_ZOOM, MAX_ZOOM);
                m_fit_pending = false;
            }
        }

        if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Left))) {

            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            m_pan.x += delta.x;
            m_pan.y += delta.y;
            m_fit_pending = false;
        }

        // Fit-to-window on demand -------------------------------------------------------------------
        if (m_fit_pending) {

            const f32 sx = avail.x / m_image->get_size().x;
            const f32 sy = avail.y / m_image->get_size().y;
            m_zoom = std::clamp(std::min(sx, sy) * FIT_PADDING, MIN_ZOOM, MAX_ZOOM);
            m_pan = ImVec2(0.0f, 0.0f);
            m_fit_pending = false;
        }

        // Compute the draw rect ---------------------------------------------------------------------
        const f32 draw_w = m_image->get_size().x * m_zoom;
        const f32 draw_h = m_image->get_size().y * m_zoom;

        const ImVec2 center(
            (canvas_min.x + canvas_max.x) * 0.5f + m_pan.x,
            (canvas_min.y + canvas_max.y) * 0.5f + m_pan.y);
        const ImVec2 img_min(center.x - draw_w * 0.5f, center.y - draw_h * 0.5f);
        const ImVec2 img_max(center.x + draw_w * 0.5f, center.y + draw_h * 0.5f);

        // Paint -------------------------------------------------------------------------------------
        draw->PushClipRect(canvas_min, canvas_max, true);

        if (m_show_checker)
            draw_checkerboard(draw, img_min, img_max);

        draw->AddImage(m_image->get_descriptor_set(), img_min, img_max);
        draw->PopClipRect();
    }


    void image_viewer_window::draw_empty_state(ImDrawList* draw, const ImVec2& min, const ImVec2& max) {

        const char* line1 = "No image loaded";
        const char* line2 = "Drag an image from the Content Browser, or double-click one there.";

        const ImVec2 s1 = ImGui::CalcTextSize(line1);
        const ImVec2 s2 = ImGui::CalcTextSize(line2);

        const f32 cx = (min.x + max.x) * 0.5f;
        const f32 cy = (min.y + max.y) * 0.5f;

        draw->AddText(ImVec2(cx - s1.x * 0.5f, cy - s1.y - 4.0f), IM_COL32(200, 200, 200, 255), line1);
        draw->AddText(ImVec2(cx - s2.x * 0.5f, cy + 4.0f),          IM_COL32(140, 140, 140, 255), line2);
    }


    void image_viewer_window::draw_error_state(ImDrawList* draw, const ImVec2& min, const ImVec2& max) {

        const char* line1 = "Failed to load image";
        const char* line2 = m_details.path.generic_string().c_str();

        const ImVec2 s1 = ImGui::CalcTextSize(line1);
        const ImVec2 s2 = ImGui::CalcTextSize(line2);

        const f32 cx = (min.x + max.x) * 0.5f;
        const f32 cy = (min.y + max.y) * 0.5f;

        draw->AddText(ImVec2(cx - s1.x * 0.5f, cy - s1.y - 4.0f), IM_COL32(230, 90, 90, 255), line1);
        draw->AddText(ImVec2(cx - s2.x * 0.5f, cy + 4.0f),          IM_COL32(140, 140, 140, 255), line2);
    }

    // drag & drop -----------------------------------------------------------------------------------------------------

    void image_viewer_window::handle_drag_drop() {

        // Register the entire window rect as a drop target so the user can
        // release anywhere on the panel, not only over the canvas.
        const ImVec2 min = ImGui::GetWindowPos();
        const ImVec2 max = ImVec2(min.x + ImGui::GetWindowSize().x, min.y + ImGui::GetWindowSize().y);

        if (!ImGui::BeginDragDropTargetCustom(ImRect(min, max), ImGui::GetID("##iv_drop_target")))
            return;

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DRAG_PAYLOAD_ID)) {

            const char* raw = static_cast<const char*>(payload->Data);
            if (raw && raw[0] != '\0') {

                const std::filesystem::path dropped(raw);
                if (GLT::render::is_image_extension(to_lower(dropped.extension().string())))
                    open(dropped);
            }
        }

        ImGui::EndDragDropTarget();
    }

    // helpers ---------------------------------------------------------------------------------------------------------

    void image_viewer_window::populate_details() {

        std::error_code error{};

        const auto size = std::filesystem::file_size(m_details.path, error);
        if (!error)
            m_details.file_size = static_cast<u64>(size);

        error.clear();
        const auto ftime = std::filesystem::last_write_time(m_details.path, error);
        if (!error)
            m_details.last_modified = ftime;

        const std::string& ext = m_details.extension;

        m_details.is_hdr = (ext == ".hdr");
        m_details.has_alpha = (ext == ".png" || ext == ".tga" || ext == ".psd" || ext == ".gif");
        m_details.is_srgb = !m_details.is_hdr;

        // Best-effort defaults. A format-aware decoder can refine these once
        // the header has been parsed — for now we infer from the extension
        // so the details panel has something useful to show.
        if      (ext == ".png")                     { m_details.channels = 4; m_details.bit_depth =  8; }
        else if (ext == ".jpg" || ext == ".jpeg")   { m_details.channels = 3; m_details.bit_depth =  8; }
        else if (ext == ".bmp")                     { m_details.channels = 3; m_details.bit_depth =  8; }
        else if (ext == ".tga")                     { m_details.channels = 4; m_details.bit_depth =  8; }
        else if (ext == ".hdr")                     { m_details.channels = 3; m_details.bit_depth = 32; }
        else                                        { m_details.channels = 0; m_details.bit_depth =  0; }

        m_details.mip_levels = 1;
    }

}
