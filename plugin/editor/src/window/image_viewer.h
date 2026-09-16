
#pragma once

#include <imgui.h>

#include <render/image.h>

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

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Dockable editor window that previews an image / texture and shows its
    // metadata alongside.
    //
    // Layout:
    //   ------------------------------------------------------------
    //   | details (left, ~400 px)  |  image preview (right, larger) |
    //   ------------------------------------------------------------
    //
    // Interactions:
    //   - Mouse wheel over the preview zooms in/out.
    //   - Left- or middle-drag pans the image.
    //   - "Fit" refits the image to the panel; "1:1" is one image pixel per
    //     screen pixel.
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

        // ---- drawing helpers -----------------------------------------------
        void draw_details_panel();

        void draw_image_panel();

        void draw_image_toolbar();

        void draw_canvas();

        void draw_empty_state(ImDrawList* draw, const ImVec2& min, const ImVec2& max);

        void draw_error_state(ImDrawList* draw, const ImVec2& min, const ImVec2& max);

        void handle_drag_drop();

        void populate_details();


        image_details                               m_details{};
        bool                                        m_has_image = false;
        bool                                        m_load_failed = false;

        // Display state for the image panel.
        f32                                         m_zoom = 1.0f;
        ImVec2                                      m_pan = ImVec2(0.0f, 0.0f);
        bool                                        m_fit_pending = true;
        bool                                        m_show_checker = true;
        GLT::unique_ref<GLT::render::image>         m_image{};

    };

}
