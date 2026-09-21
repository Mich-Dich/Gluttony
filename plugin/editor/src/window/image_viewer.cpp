#include "util/pch.h"
#include "image_viewer.h"

#include <imgui.h>
#include <imgui_internal.h>

#if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wsign-compare"
    #pragma GCC diagnostic ignored "-Wunused-but-set-variable"
    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#include <stb_image.h>
#include <stb_image_write.h>

#if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic pop
#endif

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

    constexpr int                               MAX_TILE_COUNT = 4;

    constexpr u8                                CHANNEL_BIT_R = 0b0001;         // Bit layout: 0: R, 1: G, 2: B, 3: A.

    constexpr u8                                CHANNEL_BIT_G = 0b0010;         // Bit layout: 0: R, 1: G, 2: B, 3: A.

    constexpr u8                                CHANNEL_BIT_B = 0b0100;         // Bit layout: 0: R, 1: G, 2: B, 3: A.

    constexpr u8                                CHANNEL_BIT_A = 0b1000;         // Bit layout: 0: R, 1: G, 2: B, 3: A.

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // All the per-pixel display options packed into one struct so the transform can happen in a single pass.
    struct pixel_transform {

        image_channel                           channel_mode = image_channel::Original;
        channel_swizzle                         swizzle = channel_swizzle::RGBA;
        bool                                    grayscale = false;
        bool                                    normalize = false;
        u8                                      min_r = 0, max_r = 255;
        u8                                      min_g = 0, max_g = 255;
        u8                                      min_b = 0, max_b = 255;
    };

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    namespace image {
        
        // Convert ".[ext]" to a short human-readable format tag.
        std::string pretty_format(const std::string& ext);
    }

    // Checkerboard painted behind the image so alpha reads correctly.
    void draw_checkerboard(ImDrawList* draw, const ImVec2& min, const ImVec2& max);

    // Maps a channel mode to the set of source channels that survive the mask.
    u8 channel_mask_for(image_channel mode);

    // Apply normalize -> swizzle -> grayscale -> mask in a single pass over the pixel buffer. `pixels` is tightly packed RGBA8.
    void apply_pixel_transform(std::vector<u8>& pixels, const pixel_transform& t);

    // Halve the image N times with a 2x2 box filter. `levels == 0` copies src straight through.
    void downsample_rgba(const std::vector<u8>& src, u32 src_w, u32 src_h, int levels, std::vector<u8>& out, u32& out_w, u32& out_h);

    // Largest useful mip index for the given base resolution.
    int  compute_max_mip(u32 width, u32 height);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    namespace image {

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

            return GLT::util::to_lower(ext.substr(1));     // strip the leading dot
        }

    }

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


    u8 channel_mask_for(image_channel mode) {

        switch (mode) {

            case image_channel::Original: return 0b1111;
            case image_channel::R:        return CHANNEL_BIT_R;
            case image_channel::G:        return CHANNEL_BIT_G;
            case image_channel::B:        return CHANNEL_BIT_B;
            case image_channel::A:        return CHANNEL_BIT_A;
            case image_channel::RG:       return CHANNEL_BIT_R | CHANNEL_BIT_G;
            case image_channel::RB:       return CHANNEL_BIT_R | CHANNEL_BIT_B;
            case image_channel::GB:       return CHANNEL_BIT_G | CHANNEL_BIT_B;
            case image_channel::RA:       return CHANNEL_BIT_R | CHANNEL_BIT_A;
            case image_channel::GA:       return CHANNEL_BIT_G | CHANNEL_BIT_A;
            case image_channel::BA:       return CHANNEL_BIT_B | CHANNEL_BIT_A;
            case image_channel::RGB:      return CHANNEL_BIT_R | CHANNEL_BIT_G | CHANNEL_BIT_B;
            case image_channel::RGA:      return CHANNEL_BIT_R | CHANNEL_BIT_G | CHANNEL_BIT_A;
            case image_channel::RBA:      return CHANNEL_BIT_R | CHANNEL_BIT_B | CHANNEL_BIT_A;
            case image_channel::GBA:      return CHANNEL_BIT_G | CHANNEL_BIT_B | CHANNEL_BIT_A;
        }
        return 0b1111;
    }


    void apply_pixel_transform(std::vector<u8>& pixels, const pixel_transform& t) {

        // Swizzle permutation indices: out.{r,g,b,a} = in[src_idx].
        int sr_idx = 0, sg_idx = 1, sb_idx = 2;
        switch (t.swizzle) {

            case channel_swizzle::RGBA: break;
            case channel_swizzle::BGRA: sr_idx = 2; sb_idx = 0; break;
            case channel_swizzle::ARGB: sr_idx = 3; sg_idx = 0; sb_idx = 1; break;
            case channel_swizzle::ABGR: sr_idx = 3; sg_idx = 2; sb_idx = 1; break;
        }

        // Normalize scales, computed once.
        auto make_scale = [](u8 lo, u8 hi) -> f32 {
            if (hi <= lo) return 1.0f;
            return 255.0f / static_cast<f32>(hi - lo);
        };
        const f32 scale_r = make_scale(t.min_r, t.max_r);
        const f32 scale_g = make_scale(t.min_g, t.max_g);
        const f32 scale_b = make_scale(t.min_b, t.max_b);

        const u8 mask = channel_mask_for(t.channel_mode);
        const bool keep_r = (mask & CHANNEL_BIT_R) != 0;
        const bool keep_g = (mask & CHANNEL_BIT_G) != 0;
        const bool keep_b = (mask & CHANNEL_BIT_B) != 0;
        const bool keep_a = (mask & CHANNEL_BIT_A) != 0;

        if ((t.channel_mode == image_channel::Original) 
            && (t.swizzle == channel_swizzle::RGBA) 
            && !t.grayscale && !t.normalize) {

            return;
        }

        for (size_t i = 0; i + 3 < pixels.size(); i += 4) {

            // Normalize on the original channel order so the per-channel min/max we measured line up
            u8 in[4] = { pixels[i + 0], pixels[i + 1], pixels[i + 2], pixels[i + 3] };

            if (t.normalize) {
                in[0] = static_cast<u8>(std::clamp((in[0] - t.min_r) * scale_r, 0.0f, 255.0f));
                in[1] = static_cast<u8>(std::clamp((in[1] - t.min_g) * scale_g, 0.0f, 255.0f));
                in[2] = static_cast<u8>(std::clamp((in[2] - t.min_b) * scale_b, 0.0f, 255.0f));
            }

            // Swizzle
            u8 r = in[sr_idx];
            u8 g = in[sg_idx];
            u8 b = in[sb_idx];

            // Grayscale (Rec. 709 luminance)
            if (t.grayscale) {
                const u8 l = static_cast<u8>(
                    0.2126f * static_cast<f32>(r) +
                    0.7152f * static_cast<f32>(g) +
                    0.0722f * static_cast<f32>(b) + 0.5f);
                r = g = b = l;
            }

            // Channel isolation
            if (!keep_r) r = 0;
            if (!keep_g) g = 0;
            if (!keep_b) b = 0;

            pixels[i + 0] = r;
            pixels[i + 1] = g;
            pixels[i + 2] = b;
            if (!keep_a) pixels[i + 3] = 255;
        }
    }


    void downsample_rgba(const std::vector<u8>& src, u32 src_w, u32 src_h, int levels, std::vector<u8>& out, u32& out_w, u32& out_h) {

        out_w = std::max(1u, src_w >> levels);
        out_h = std::max(1u, src_h >> levels);

        if (levels <= 0 || src_w <= 1 || src_h <= 1) {
            out = src;
            out_w = src_w;
            out_h = src_h;
            return;
        }

        // Iteratively halve with a 2x2 box filter so we never touch more
        // memory than we need.
        std::vector<u8> cur = src;
        u32 cur_w = src_w;
        u32 cur_h = src_h;

        for (int lvl = 0; lvl < levels; ++lvl) {

            const u32 next_w = std::max(1u, cur_w >> 1);
            const u32 next_h = std::max(1u, cur_h >> 1);

            std::vector<u8> next(static_cast<size_t>(next_w) * next_h * 4u);

            for (u32 y = 0; y < next_h; ++y) {
                for (u32 x = 0; x < next_w; ++x) {

                    u32 sum[4] = { 0, 0, 0, 0 };
                    u32 count = 0;

                    for (u32 dy = 0; dy < 2; ++dy) {
                        for (u32 dx = 0; dx < 2; ++dx) {

                            const u32 sx = std::min(x * 2u + dx, cur_w - 1u);
                            const u32 sy = std::min(y * 2u + dy, cur_h - 1u);
                            const size_t idx = (static_cast<size_t>(sy) * cur_w + sx) * 4u;

                            for (int c = 0; c < 4; ++c) sum[c] += cur[idx + c];
                            ++count;
                        }
                    }

                    const size_t out_idx = (static_cast<size_t>(y) * next_w + x) * 4u;
                    for (int c = 0; c < 4; ++c) next[out_idx + c] = static_cast<u8>(sum[c] / count);
                }
            }

            cur = std::move(next);
            cur_w = next_w;
            cur_h = next_h;
        }

        out = std::move(cur);
        out_w = cur_w;
        out_h = cur_h;
    }


    int compute_max_mip(u32 width, u32 height) {

        if (width == 0 || height == 0) return 0;
        const u32 max_dim = std::max(width, height);
        return static_cast<int>(std::floor(std::log2(static_cast<f32>(max_dim))));
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    image_viewer_window::image_viewer_window(const std::filesystem::path& path) {

        open(path);
    }


    image_viewer_window::~image_viewer_window() { }

    // CLASS PUBLIC ====================================================================================================

    void image_viewer_window::open(const std::filesystem::path& path) {

        // Reset state up-front so a failed load never leaves the previous
        // image visible.
        m_details = {};
        m_has_image = false;
        m_load_failed = false;
        m_show_window = true;

        // Drop any cached pixel data from the previous image.
        m_source_pixels.clear();
        m_source_width = 0;
        m_source_height = 0;

        m_display_pixels.clear();
        m_display_width = 0;
        m_display_height = 0;

        m_histogram = {};

        m_cursor_in_image = false;
        m_cursor_img_x = 0;
        m_cursor_img_y = 0;

        m_details.path = GLT::project::extract_path_from_project_content_dir(path);
        m_details.name = path.filename().string();
        m_details.extension = GLT::util::to_lower(path.extension().string());
        m_details.format = image::pretty_format(m_details.extension);

        cache_window_state(); 
        make_window_name((std::string("IMG: ") + m_details.name).c_str());

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

        // Cache CPU pixels for channel masking / mip generation / inspection.
        // If this fails the image is still displayed, we just can't offer
        // the CPU-driven features.
        load_source_pixels();

        if (!m_source_pixels.empty()) {

            compute_histogram();

            // Prime the display cache for pixel inspection / export.
            m_display_pixels = m_source_pixels;
            m_display_width = m_source_width;
            m_display_height = m_source_height;

            const bool needs_rebuild =
                (m_mip_level > 0) ||
                (m_channel_mode != image_channel::Original) ||
                (m_swizzle != channel_swizzle::RGBA) ||
                m_grayscale || m_normalize;

            if (needs_rebuild)
                apply_display_mode();
        }
    }


    void image_viewer_window::close_image() {

        m_image.reset();
        m_details = {};
        m_has_image = false;
        m_load_failed = false;
        m_source_pixels.clear();
        m_source_width = 0;
        m_source_height = 0;
        m_display_pixels.clear();
        m_display_width = 0;
        m_display_height = 0;
        m_histogram = {};
        m_cursor_in_image = false;
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
        // Nothing to update per frame - decoding is driven by the icon
        // manager's async queue and consumed inside draw_canvas().
    }


    bool image_viewer_window::serialize(const std::filesystem::path& /*project_file*/, const GLT::serializer::option /*option*/) { return false; }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    // details panel ---------------------------------------------------------------------------------------------------

    void image_viewer_window::draw_details_panel() {

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
            return;
        }

        if (!m_has_image) {

            ImGui::TextDisabled("No image loaded.");
            return;
        }

        // Sections (order matters - Display first, then info, then the two
        // "hot" sections that update per-frame from the canvas.)
        draw_display_section();
        draw_histogram_section();
        draw_pixel_section();
        draw_format_section();

        // File section ---------------------------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("File", ImGuiTreeNodeFlags_DefaultOpen)) {

            if (UI::begin_table("image_detail_and_edit", false)) {
                
                UI::table_row("name",   m_details.name.c_str());
                UI::table_row("format", m_details.format.c_str());
                UI::table_row("size",   GLT::util::format_bytes(m_details.file_size));
                UI::end_table();
            }
        }

        // Image section --------------------------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("Image", ImGuiTreeNodeFlags_DefaultOpen)) {

            if (UI::begin_table("image_detail_and_edit", false)) {
                
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

            if (UI::begin_table("image_detail_and_edit", false)) {
                
                UI::table_row("HDR",   m_details.is_hdr    ? std::string_view("yes") : std::string_view("no"));
                UI::table_row("alpha", m_details.has_alpha ? std::string_view("yes") : std::string_view("no"));
                UI::table_row("sRGB",  m_details.is_srgb   ? std::string_view("yes") : std::string_view("no"));
                UI::end_table();
            }
        }
    }


    void image_viewer_window::draw_display_section() {

        if (!ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        const bool channels_available = !m_source_pixels.empty();

        if (UI::begin_table("image_detail_and_edit", false)) {

            // Channel isolation --------------------------------------------------------------------------------
            UI::table_row(
                []{ ImGui::TextUnformatted("channel"); },
                [this, channels_available] {

                    static const char* k_modes[] = {
                        "Original",
                        "R",  "G",  "B",  "A",
                        "RG", "RB", "GB", "RA", "GA", "BA",
                        "RGB", "RGA", "RBA", "GBA",
                    };
                    static_assert(IM_ARRAYSIZE(k_modes) == 15, "channel combo out of sync with image_channel");

                    int current = static_cast<int>(m_channel_mode);
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::BeginDisabled(!channels_available);
                    if (ImGui::Combo("##iv_channel", &current, k_modes, IM_ARRAYSIZE(k_modes))) {

                        const image_channel next = static_cast<image_channel>(current);
                        if (next != m_channel_mode) {

                            m_channel_mode = next;
                            apply_display_mode();
                        }
                    }
                    ImGui::EndDisabled();
                });

            // Swizzle ------------------------------------------------------------------------------------------
            UI::table_row(
                []{ ImGui::TextUnformatted("swizzle"); },
                [this, channels_available] {

                    static const char* k_swizzles[] = { "RGBA", "BGRA", "ARGB", "ABGR" };
                    int current = static_cast<int>(m_swizzle);
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::BeginDisabled(!channels_available);
                    if (ImGui::Combo("##iv_swizzle", &current, k_swizzles, IM_ARRAYSIZE(k_swizzles))) {

                        const channel_swizzle next = static_cast<channel_swizzle>(current);
                        if (next != m_swizzle) {

                            m_swizzle = next;
                            apply_display_mode();
                        }
                    }
                    ImGui::EndDisabled();
                });

            // Grayscale ----------------------------------------------------------------------------------------
            {
                const bool prev = m_grayscale;
                ImGui::BeginDisabled(!channels_available);
                UI::table_row("grayscale", m_grayscale);
                ImGui::EndDisabled();
                if (prev != m_grayscale)
                    apply_display_mode();
            }

            // Normalize / contrast stretch ---------------------------------------------------------------------
            {
                const bool prev = m_normalize;
                ImGui::BeginDisabled(!channels_available || !m_histogram.valid);
                UI::table_row("normalize", m_normalize);
                ImGui::EndDisabled();
                if (prev != m_normalize)
                    apply_display_mode();
            }

            // Mip level ----------------------------------------------------------------------------------------
            {
                const int max_mip = compute_max_mip(m_source_width, m_source_height);
                UI::table_row(
                    []{ ImGui::TextUnformatted("mip level"); },
                    [this, max_mip, channels_available] {

                        int value = m_mip_level;
                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::BeginDisabled(!channels_available || max_mip <= 0);
                        if (ImGui::SliderInt("##iv_mip", &value, 0, max_mip)) {

                            if (value != m_mip_level) {

                                m_mip_level = value;
                                apply_display_mode();
                                m_fit_pending = true;
                            }
                        }
                        ImGui::EndDisabled();
                    });
            }

            // Background mode ----------------------------------------------------------------------------------
            UI::table_row(
                []{ ImGui::TextUnformatted("background"); },
                [this] {

                    static const char* k_bg[] = { "Checkerboard", "Solid Color" };
                    int current = static_cast<int>(m_background_mode);
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::Combo("##iv_bg", &current, k_bg, IM_ARRAYSIZE(k_bg)))
                        m_background_mode = static_cast<background_mode>(current);
                });

            // Background colour (only relevant in Solid Color mode) -------------------------------------------
            if (m_background_mode == background_mode::SolidColor) {

                UI::table_row(
                    []{ ImGui::TextUnformatted("bg color"); },
                    [this] {

                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::ColorEdit4("##iv_bgcol", &m_background_color.x,
                            ImGuiColorEditFlags_NoInputs |
                            ImGuiColorEditFlags_AlphaPreview |
                            ImGuiColorEditFlags_AlphaBar);
                    });
            }

            // Tiled preview ------------------------------------------------------------------------------------
            UI::table_row(
                []{ ImGui::TextUnformatted("tiled"); },
                [this] {

                    static const char* k_tiles[] = { "1x1", "2x2", "3x3", "4x4" };
                    int current = m_tile_count - 1;
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::Combo("##iv_tiles", &current, k_tiles, IM_ARRAYSIZE(k_tiles))) {

                        const int next = current + 1;
                        if (next != m_tile_count) {

                            m_tile_count = next;
                            m_fit_pending = true;
                        }
                    }
                });

            // Export -------------------------------------------------------------------------------------------
            UI::table_row(
                []{ ImGui::TextUnformatted("export"); },
                [this] {

                    ImGui::BeginDisabled(m_display_pixels.empty());
                    if (ImGui::Button("Save as PNG##iv_save", ImVec2(-1.0f, 0.0f))) {

                        std::filesystem::path out = m_details.path;
                        out.replace_filename(m_details.path.stem().string() + "_display.png");
                        save_display_as_png(PROJECT_CONTENT_DIR / out);
                    }
                    ImGui::EndDisabled();
                });

            UI::end_table();
        }

        if (!channels_available) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("Channel / mip controls unavailable for this image.");
            ImGui::PopStyleColor();
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
    }


    void image_viewer_window::draw_format_section() {

        if (!ImGui::CollapsingHeader("Format", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        // Best-effort description of the *source* file format. The GPU-side
        // image is always RGBA8 internally; this mirrors what the file
        // actually contains so the user can tell an 8-bit PNG apart from a
        // 32-bit float HDR.
        std::string pixel_format = "Unknown";
        if      (m_details.channels == 4 && m_details.bit_depth ==  8) pixel_format = "RGBA8_UNORM";
        else if (m_details.channels == 3 && m_details.bit_depth ==  8) pixel_format = "RGB8_UNORM";
        else if (m_details.channels == 4 && m_details.bit_depth == 32) pixel_format = "RGBA32F";
        else if (m_details.channels == 3 && m_details.bit_depth == 32) pixel_format = "RGB32F";

        const u32 bytes_per_channel = m_details.bit_depth ? (m_details.bit_depth / 8u) : 0u;
        const u32 bytes_per_pixel = m_details.channels * bytes_per_channel;

        if (UI::begin_table("image_detail_and_edit", false)) {

            UI::table_row("pixel format", std::string_view(pixel_format));
            UI::table_row("colour space", m_details.is_srgb ? std::string_view("sRGB") : std::string_view("Linear"));
            UI::table_row("range", m_details.is_hdr  ? std::string_view("HDR")  : std::string_view("LDR"));
            UI::table_row("bytes/pixel", std::string_view(std::to_string(bytes_per_pixel)));
            UI::end_table();
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
    }


    void image_viewer_window::draw_histogram_section() {

        if (!ImGui::CollapsingHeader("Histogram", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        if (!m_histogram.valid) {

            ImGui::TextDisabled("No histogram available.");
            return;
        }

        const f32 width  = std::min(ImGui::GetContentRegionAvail().x, 360.0f);
        const f32 height = 90.0f;

        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 p1 = ImVec2(p0.x + width, p0.y + height);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(p0, p1, IM_COL32(20, 20, 22, 255));

        // Common scale across all three channels so their relative heights
        // are directly comparable.
        u32 max_count = 1;
        for (int i = 0; i < 256; ++i) {
            max_count = std::max(max_count, m_histogram.r[i]);
            max_count = std::max(max_count, m_histogram.g[i]);
            max_count = std::max(max_count, m_histogram.b[i]);
        }

        const f32 bin_w = width / 256.0f;

        auto draw_channel = [&](const std::array<u32, 256>& hist, ImU32 color) {
            for (int i = 0; i < 256; ++i) {
                const f32 h = (static_cast<f32>(hist[i]) / static_cast<f32>(max_count)) * height;
                const ImVec2 b0(p0.x + i * bin_w, p1.y - h);
                const ImVec2 b1(p0.x + (i + 1) * bin_w, p1.y);
                draw->AddRectFilled(b0, b1, color);
            }
        };

        // Additive alpha so overlaps read as brighter / whiter regions.
        draw_channel(m_histogram.r, IM_COL32(220,  80,  80, 90));
        draw_channel(m_histogram.g, IM_COL32( 80, 220,  80, 90));
        draw_channel(m_histogram.b, IM_COL32( 80, 120, 220, 90));

        draw->AddRect(p0, p1, IM_COL32(70, 70, 70, 255));

        ImGui::Dummy(ImVec2(width, height));

        // Per-channel min/max in matching colours.
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.50f, 0.50f, 1.0f));
        ImGui::Text("R %d..%d", m_histogram.min_r, m_histogram.max_r);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.90f, 0.50f, 1.0f));
        ImGui::Text("G %d..%d", m_histogram.min_g, m_histogram.max_g);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.60f, 0.90f, 1.0f));
        ImGui::Text("B %d..%d", m_histogram.min_b, m_histogram.max_b);
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
    }


    void image_viewer_window::draw_pixel_section() {

        if (!ImGui::CollapsingHeader("Pixel", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        if (!m_cursor_in_image || m_display_pixels.empty()) {

            ImGui::TextDisabled("Hover over the image to inspect a pixel.");
            return;
        }

        const u32 x = m_cursor_img_x;
        const u32 y = m_cursor_img_y;
        const size_t idx = (static_cast<size_t>(y) * m_display_width + x) * 4u;

        const u8 r = m_display_pixels[idx + 0];
        const u8 g = m_display_pixels[idx + 1];
        const u8 b = m_display_pixels[idx + 2];
        const u8 a = m_display_pixels[idx + 3];

        if (UI::begin_table("image_detail_and_edit", false)) {

            UI::table_row_text("coord",   "%u, %u (mip %d)", x, y, m_mip_level);
            UI::table_row_text("display", "%u, %u, %u, %u", r, g, b, a);
            UI::table_row_text("norm",    "%.3f, %.3f, %.3f, %.3f", r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);

            if (!m_source_pixels.empty() && m_source_width > 0 && m_source_height > 0) {

                const u32 src_x = std::min(x << m_mip_level, m_source_width  - 1u);
                const u32 src_y = std::min(y << m_mip_level, m_source_height - 1u);
                const size_t sidx = (static_cast<size_t>(src_y) * m_source_width + src_x) * 4u;

                UI::table_row_text("source", "%u, %u, %u, %u",
                    m_source_pixels[sidx + 0], m_source_pixels[sidx + 1],
                    m_source_pixels[sidx + 2], m_source_pixels[sidx + 3]);
            }

            UI::end_table();
        }

        // Small swatch of the displayed pixel.
        const ImVec4 swatch(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
        ImGui::ColorButton("##iv_pixel_swatch", swatch, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_AlphaPreview, 
            ImVec2(28.0f, 28.0f));

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
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
    }


    void image_viewer_window::draw_canvas() {

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
        const ImVec2 canvas_max = ImVec2(canvas_min.x + avail.x, canvas_min.y + avail.y);

        // Reserve the canvas so we get hover/active state for pan & zoom.
        ImGui::InvisibleButton("##iv_canvas", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);

        ImDrawList* draw = ImGui::GetWindowDrawList();

        if (m_load_failed) {
            m_cursor_in_image = false;
            draw_error_state(draw, canvas_min, canvas_max);
            return;
        }

        if (!m_has_image) {
            m_cursor_in_image = false;
            draw_empty_state(draw, canvas_min, canvas_max);
            return;
        }

        if (!m_image->get_descriptor_set()) {       // Image not ready yet

            m_cursor_in_image = false;
            const char* msg = "Loading...";
            const ImVec2 sz = ImGui::CalcTextSize(msg);
            draw->AddText(ImVec2((canvas_min.x + canvas_max.x - sz.x) * 0.5f, (canvas_min.y + canvas_max.y - sz.y) * 0.5f), 
                IM_COL32(200, 200, 200, 255), msg);
            return;
        }

        // Keep the reported size in sync with the currently displayed image
        // (which equals the mip's resolution when m_mip_level > 0).
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

        const f32 tiles = static_cast<f32>(m_tile_count);

        // Fit-to-window on demand - fits the entire tile grid, not a single tile.
        if (m_fit_pending) {

            const f32 sx = avail.x / (m_image->get_size().x * tiles);
            const f32 sy = avail.y / (m_image->get_size().y * tiles);
            m_zoom = std::clamp(std::min(sx, sy) * FIT_PADDING, MIN_ZOOM, MAX_ZOOM);
            m_pan = ImVec2(0.0f, 0.0f);
            m_fit_pending = false;
        }

        // Compute the draw rect ---------------------------------------------------------------------
        const f32 tile_w = m_image->get_size().x * m_zoom;
        const f32 tile_h = m_image->get_size().y * m_zoom;
        const f32 grid_w = tile_w * tiles;
        const f32 grid_h = tile_h * tiles;

        const ImVec2 center(
            (canvas_min.x + canvas_max.x) * 0.5f + m_pan.x,
            (canvas_min.y + canvas_max.y) * 0.5f + m_pan.y);
        const ImVec2 grid_min(center.x - grid_w * 0.5f, center.y - grid_h * 0.5f);
        const ImVec2 grid_max(center.x + grid_w * 0.5f, center.y + grid_h * 0.5f);

        // Pixel inspection - uses the same geometry as the paint below.
        update_pixel_inspection(canvas_min, canvas_max, grid_min, grid_max, tile_w, tile_h);

        // Paint -------------------------------------------------------------------------------------
        draw->PushClipRect(canvas_min, canvas_max, true);

        if (m_background_mode == background_mode::Checkerboard) {

            draw_checkerboard(draw, grid_min, grid_max);

        } else {

            draw->AddRectFilled(grid_min, grid_max,
                ImGui::ColorConvertFloat4ToU32(m_background_color));
        }

        const ImTextureRef tex = static_cast<ImTextureRef>(m_image->get_descriptor_set());
        for (int ty = 0; ty < m_tile_count; ++ty) {
            for (int tx = 0; tx < m_tile_count; ++tx) {

                const ImVec2 t_min(grid_min.x + tx * tile_w, grid_min.y + ty * tile_h);
                const ImVec2 t_max(t_min.x + tile_w, t_min.y + tile_h);
                draw->AddImage(tex, t_min, t_max);
            }
        }

        // Highlight the inspected pixel when zoomed in far enough for it to
        // be visible (at least 4 screen pixels per image pixel).
        if (m_cursor_in_image && m_zoom >= 4.0f) {

            const ImVec2 px_min(grid_min.x + m_cursor_img_x * m_zoom,
                                grid_min.y + m_cursor_img_y * m_zoom);
            const ImVec2 px_max(px_min.x + m_zoom, px_min.y + m_zoom);
            draw->AddRect(px_min, px_max, IM_COL32(255, 220, 80, 220), 0.0f, 0, 1.5f);
        }

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
                if (GLT::render::is_image_extension(GLT::util::to_lower(dropped.extension().string())))
                    open(dropped);
            }
        }

        ImGui::EndDragDropTarget();
    }

    // helpers ---------------------------------------------------------------------------------------------------------

    void image_viewer_window::populate_details() {

        std::error_code error{};

        const auto size = GLT::vfs::file_size(m_details.path, error);
        if (!error)
            m_details.file_size = static_cast<u64>(size);

        error.clear();
        const auto ftime = GLT::vfs::last_write_time(m_details.path, error);
        if (!error)
            m_details.last_modified = ftime;

        const std::string& ext = m_details.extension;

        m_details.is_hdr = (ext == ".hdr");
        m_details.has_alpha = (ext == ".png" || ext == ".tga" || ext == ".psd" || ext == ".gif");
        m_details.is_srgb = !m_details.is_hdr;

        // Best-effort defaults. A format-aware decoder can refine these once
        // the header has been parsed - for now we infer from the extension
        // so the details panel has something useful to show.
        if      (ext == ".png")                     { m_details.channels = 4; m_details.bit_depth =  8; }
        else if (ext == ".jpg" || ext == ".jpeg")   { m_details.channels = 3; m_details.bit_depth =  8; }
        else if (ext == ".bmp")                     { m_details.channels = 3; m_details.bit_depth =  8; }
        else if (ext == ".tga")                     { m_details.channels = 4; m_details.bit_depth =  8; }
        else if (ext == ".hdr")                     { m_details.channels = 3; m_details.bit_depth = 32; }
        else                                        { m_details.channels = 0; m_details.bit_depth =  0; }

        m_details.mip_levels = 1;
    }


    void image_viewer_window::load_source_pixels() {

        m_source_pixels.clear();
        m_source_width  = 0;
        m_source_height = 0;

        if (!m_image)
            return;

        // image::load() returns an RGBA8 buffer owned by stb_image.
        // Copy it out (we need it for the lifetime of the window) and free
        // the original.
        u32 width  = 0;
        u32 height = 0;
        void* raw = m_image->load(PROJECT_CONTENT_DIR / m_details.path, width, height);

        if (!raw || width == 0 || height == 0) {
            if (raw) stbi_image_free(raw);
            return;
        }

        m_source_width  = width;
        m_source_height = height;

        const size_t byte_count = static_cast<size_t>(width) * height * 4u;
        m_source_pixels.resize(byte_count);
        std::memcpy(m_source_pixels.data(), raw, byte_count);

        stbi_image_free(raw);
    }


    void image_viewer_window::compute_histogram() {

        m_histogram = {};

        if (m_source_pixels.empty() || m_source_width == 0 || m_source_height == 0)
            return;

        u8 min_r = 255, max_r = 0;
        u8 min_g = 255, max_g = 0;
        u8 min_b = 255, max_b = 0;

        const size_t pixel_count = static_cast<size_t>(m_source_width) * m_source_height;
        for (size_t i = 0; i < pixel_count; ++i) {

            const u8 r = m_source_pixels[i * 4 + 0];
            const u8 g = m_source_pixels[i * 4 + 1];
            const u8 b = m_source_pixels[i * 4 + 2];

            ++m_histogram.r[r];
            ++m_histogram.g[g];
            ++m_histogram.b[b];

            const u8 l = static_cast<u8>(
                0.2126f * static_cast<f32>(r) +
                0.7152f * static_cast<f32>(g) +
                0.0722f * static_cast<f32>(b) + 0.5f);
            ++m_histogram.luma[l];

            if (r < min_r) min_r = r;
            if (r > max_r) max_r = r;
            if (g < min_g) min_g = g;
            if (g > max_g) max_g = g;
            if (b < min_b) min_b = b;
            if (b > max_b) max_b = b;
        }

        m_histogram.min_r = min_r; m_histogram.max_r = max_r;
        m_histogram.min_g = min_g; m_histogram.max_g = max_g;
        m_histogram.min_b = min_b; m_histogram.max_b = max_b;
        m_histogram.pixel_count = pixel_count;
        m_histogram.valid = true;
    }


    void image_viewer_window::apply_display_mode() {

        if (!m_has_image || !m_image || m_source_pixels.empty())
            return;

        // 1) Downsample the CPU cache to the selected mip level.
        std::vector<u8> mip_pixels;
        u32 mip_w = 0;
        u32 mip_h = 0;
        downsample_rgba(m_source_pixels, m_source_width, m_source_height,
            m_mip_level, mip_pixels, mip_w, mip_h);

        // 2) Apply the whole display pipeline in one pass.
        pixel_transform t{};
        t.channel_mode = m_channel_mode;
        t.swizzle      = m_swizzle;
        t.grayscale    = m_grayscale;
        t.normalize    = m_normalize && m_histogram.valid;
        if (t.normalize) {
            t.min_r = m_histogram.min_r; t.max_r = m_histogram.max_r;
            t.min_g = m_histogram.min_g; t.max_g = m_histogram.max_g;
            t.min_b = m_histogram.min_b; t.max_b = m_histogram.max_b;
        }
        apply_pixel_transform(mip_pixels, t);

        // 3) Cache for pixel inspection / export.
        m_display_pixels = mip_pixels;
        m_display_width  = mip_w;
        m_display_height = mip_h;

        // 4) Recreate the GPU image when the target resolution changes,
        //    otherwise just push new bytes into the existing allocation.
        //
        //    We deliberately avoid image::resize() here: it reallocates with
        //    null data, which leaves the image in vk::ImageLayout::eUndefined
        //    and makes the subsequent reupload() (which assumes
        //    ShaderReadOnlyOptimal) invalid. The (data, w, h, mipmapped)
        //    constructor does the correct Undefined -> ShaderReadOnly
        //    transition as part of assign_data().
        const glm::uvec2 current_size = m_image->get_size();
        const bool size_changed = (current_size.x != mip_w) || (current_size.y != mip_h);

        if (size_changed) {

            m_image.reset();
            m_image = GLT::create_unique_ref<GLT::render::image>(
                static_cast<const void*>(mip_pixels.data()), mip_w, mip_h, false);

        } else {

            m_image->reupload(mip_pixels.data());
        }
    }


    void image_viewer_window::update_pixel_inspection(const ImVec2& canvas_min, const ImVec2& canvas_max,
        const ImVec2& grid_min, const ImVec2& grid_max, f32 tile_w, f32 tile_h) {

        m_cursor_in_image = false;

        if (!ImGui::IsItemHovered())
            return;

        if (m_display_pixels.empty() || m_display_width == 0 || m_display_height == 0)
            return;

        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (mouse.x < canvas_min.x || mouse.x >= canvas_max.x ||
            mouse.y < canvas_min.y || mouse.y >= canvas_max.y)
            return;

        if (mouse.x < grid_min.x || mouse.x >= grid_max.x ||
            mouse.y < grid_min.y || mouse.y >= grid_max.y)
            return;

        // Which tile are we over?
        const f32 local_x = mouse.x - grid_min.x;
        const f32 local_y = mouse.y - grid_min.y;

        const int tile_x = static_cast<int>(local_x / tile_w);
        const int tile_y = static_cast<int>(local_y / tile_h);
        if (tile_x < 0 || tile_x >= m_tile_count || tile_y < 0 || tile_y >= m_tile_count)
            return;

        // Coordinates within the tile, then converted to image pixels.
        const f32 in_tile_x = local_x - static_cast<f32>(tile_x) * tile_w;
        const f32 in_tile_y = local_y - static_cast<f32>(tile_y) * tile_h;

        const u32 px = static_cast<u32>(in_tile_x / m_zoom);
        const u32 py = static_cast<u32>(in_tile_y / m_zoom);

        if (px >= m_display_width || py >= m_display_height)
            return;

        m_cursor_in_image = true;
        m_cursor_img_x = px;
        m_cursor_img_y = py;
    }


    void image_viewer_window::save_display_as_png(const std::filesystem::path& path) {

        if (m_display_pixels.empty() || m_display_width == 0 || m_display_height == 0)
            return;

        const int stride = static_cast<int>(m_display_width) * 4;

        if (stbi_write_png(path.string().c_str(),
                static_cast<int>(m_display_width),
                static_cast<int>(m_display_height),
                4,
                m_display_pixels.data(),
                stride) == 0) {

            LOG(error, "failed to save image to [{}]", path.generic_string());

        } else {

            LOG(info, "saved image to [{}]", path.generic_string());
        }
    }

}
