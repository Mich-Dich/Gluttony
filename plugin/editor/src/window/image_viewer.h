#pragma once

#include <imgui.h>

#include <render/image.h>

#include <array>
#include <vector>

#include "window/base_window.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Metadata describing the currently displayed image.
    //
    // Filesystem-backed fields (size, timestamps) are always populated when
    // the file exists. Image-specific fields are best-effort: width/height
    // come from the async decoder, while channels/bit depth are inferred
    // from the file extension until a format-aware parser is wired in.
    //
    // Note: width/height reflect the *currently displayed* image, which is
    // the base resolution when m_mip_level == 0 and the mip's resolution
    // otherwise.
    struct image_details {

        std::filesystem::path                   path{};
        std::string                             name{};
        std::string                             extension{};        // lowercase, includes leading dot
        std::string                             format{};           // short display name, e.g. "PNG"
        u64                                     file_size = 0;
        u32                                     width = 0;
        u32                                     height = 0;
        u32                                     channels = 0;
        u32                                     bit_depth = 0;
        u32                                     mip_levels = 1;
        bool                                    is_hdr = false;
        bool                                    has_alpha = false;
        bool                                    is_srgb = true;
        std::filesystem::file_time_type         last_modified{};
    };


    // Channel-isolation display modes for the image preview. Non-selected
    // colour channels are zeroed; a non-selected alpha channel is forced
    // fully opaque so inspecting RGB never accidentally hides pixels behind
    // transparency. Selecting A alone therefore shows the alpha shape as a
    // black overlay on the checkerboard.
    enum class image_channel : u8 {

        Original = 0,
        R,
        G,
        B,
        A,
        RG,
        RB,
        GB,
        RA,
        GA,
        BA,
        RGB,
        RGA,
        RBA,
        GBA,
    };


    // How the area behind the image is painted.
    enum class background_mode : u8 {

        Checkerboard = 0,
        SolidColor,
    };


    // Reorders which source channel ends up in which output slot. Applied
    // before grayscale / channel masking so those operate on the permuted
    // data.
    enum class channel_swizzle : u8 {

        RGBA = 0,
        BGRA,
        ARGB,
        ABGR,
    };


    // Cached histogram + per-channel min/max, used for the histogram display
    // and for "normalize" (contrast stretch). Computed once per image load
    // from the base-resolution source pixels.
    struct histogram_data {

        std::array<u32, 256>        r{};
        std::array<u32, 256>        g{};
        std::array<u32, 256>        b{};
        std::array<u32, 256>        luma{};
        u8                          min_r = 0, max_r = 255;
        u8                          min_g = 0, max_g = 255;
        u8                          min_b = 0, max_b = 255;
        u64                         pixel_count = 0;
        bool                        valid = false;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Dockable editor window that previews an image / texture and shows its
    // metadata alongside.
    //
    // Layout:
    //   -------------------------------------------------------------
    //   | details (left, ~400 px)  |  image preview (right, larger) |
    //   -------------------------------------------------------------
    //
    // Interactions:
    //   - Mouse wheel over the preview zooms in/out.
    //   - Left- or middle-drag pans the image.
    //   - "Fit" refits the image to the panel; "1:1" is one image pixel per
    //     screen pixel (of the currently displayed mip level).
    //   - Accepts "CONTENT_BROWSER_ITEM" drag/drop payloads. Image files are
    //     opened; other file types are ignored.
    class image_viewer_window : public base_window {
    public:

        image_viewer_window(const std::filesystem::path& path);
        ~image_viewer_window();

        DEFAULT_GETTER(image_details, details)


        // Loads the given file and shows it. Repeated calls replace the
        // current contents. On failure the viewer still opens and displays
        // the error in the image panel.
        void open(const std::filesystem::path& path);


        // Clears the currently loaded image and shows the empty state.
        void close_image();


        void window(const f32 delta_time) override;


        void update(const f32 delta_time) override;


        bool serialize(const std::filesystem::path& project_file, const GLT::serializer::option option) override;

    private:

        // drawing helpers ---------------------------------------------------------------------------------------------
        void draw_details_panel();

        void draw_image_panel();

        void draw_image_toolbar();

        void draw_canvas();

        void draw_empty_state(ImDrawList* draw, const ImVec2& min, const ImVec2& max);

        void draw_error_state(ImDrawList* draw, const ImVec2& min, const ImVec2& max);

        void handle_drag_drop();

        void populate_details();

        // details sections --------------------------------------------------------------------------------------------
        void draw_display_section();

        void draw_format_section();

        void draw_histogram_section();

        void draw_pixel_section();

        // processing helpers ------------------------------------------------------------------------------------------
        // Read CPU-side pixels (RGBA8) from the loaded image and keep them
        // around for later masking / mip generation. Uses the renderer's
        // public image::load() API, so no renderer changes are required.
        void load_source_pixels();

        // Rebuild the GPU image from m_source_pixels, applying the current
        // mip level, swizzle, normalize, grayscale and channel mask.
        // Recreates the image when the target resolution changes; otherwise
        // just reuploads.
        void apply_display_mode();

        // Populate m_histogram from m_source_pixels. Cheap enough to run on
        // load; only rerun if the source cache changes.
        void compute_histogram();

        // Map the current mouse position to a pixel in the displayed image
        // (if any) and cache it for the Pixel section. Called from
        // draw_canvas().
        void update_pixel_inspection(const ImVec2& canvas_min, const ImVec2& canvas_max,
            const ImVec2& grid_min, const ImVec2& grid_max, f32 tile_w, f32 tile_h);

        // Write m_display_pixels to disk as an RGBA PNG.
        void save_display_as_png(const std::filesystem::path& path);


        image_details                               m_details{};
        bool                                        m_has_image = false;
        bool                                        m_load_failed = false;

        // Display state for the image panel.
        f32                                         m_zoom = 1.0f;
        ImVec2                                      m_pan = ImVec2(0.0f, 0.0f);
        bool                                        m_fit_pending = true;
        GLT::unique_ref<GLT::render::image>         m_image{};

        // Display options ---------------------------------------------------------------------------------------------
        image_channel                               m_channel_mode = image_channel::Original;
        channel_swizzle                             m_swizzle = channel_swizzle::RGBA;
        bool                                        m_grayscale = false;
        bool                                        m_normalize = false;
        int                                         m_mip_level = 0;
        int                                         m_tile_count = 1;
        background_mode                             m_background_mode = background_mode::Checkerboard;
        ImVec4                                      m_background_color = ImVec4(0.30f, 0.30f, 0.30f, 1.0f);

        // CPU-side pixel cache (RGBA8, tightly packed, base resolution).
        std::vector<u8>                             m_source_pixels{};
        u32                                         m_source_width = 0;
        u32                                         m_source_height = 0;

        // Currently displayed pixels (post-transform, at current mip level).
        // Kept around for pixel inspection and PNG export.
        std::vector<u8>                             m_display_pixels{};
        u32                                         m_display_width = 0;
        u32                                         m_display_height = 0;

        // Histogram / per-channel range for the loaded image.
        histogram_data                              m_histogram{};

        // Pixel inspection state (refreshed each frame in draw_canvas).
        bool                                        m_cursor_in_image = false;
        u32                                         m_cursor_img_x = 0;
        u32                                         m_cursor_img_y = 0;

    };

}
