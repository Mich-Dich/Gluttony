#pragma once

#include <imgui.h>
#include <glm/vec2.hpp>

#include <array>
#include <vector>
#include <filesystem>
#include <string>

#include <plugin_system/plugin_manager.h>

#include "window/base_window.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::audio { 
    class i_audio_plugin; 
}

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Metadata describing the currently displayed audio file. Populated from
    // the decoded stream where available, plus filesystem stats.
    struct audio_details {

        std::filesystem::path                           path{};
        std::string                                     name{};
        std::string                                     extension{};            // lowercase, includes leading dot
        std::string                                     format{};               // short display name, e.g. "WAV"
        u64                                             file_size = 0;
        f32                                             duration_sec = 0.0f;
        u32                                             sample_rate = 0;
        u32                                             channels = 0;
        u32                                             bit_depth = 0;
        u32                                             bitrate_kbps = 0;
        GLT::system_time                                last_modified{};
    };


    // Min/max amplitude over one bucket of samples. One pair per channel.
    struct audio_peak_pair {

        f32                                             min = 0.0f;
        f32                                             max = 0.0f;
    };


    // Horizontal view state for the waveform canvas.
    struct waveform_view {

        f32                                             visible_start_sec = 0.0f;
        f32                                             visible_end_sec = 0.0f;
    };


    // Lives on the worker thread, then gets applied on the main thread.
    // Contains only POD-ish data — nothing tied to ImGui, the GPU, or `this`.
    struct decode_result {

        bool                                            ok = false;
        audio_details                                   details{};
        std::vector<std::vector<audio_peak_pair>>       peaks{};
        u32                                             peak_count = 0;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Dockable editor window that previews an audio file, shows its metadata,
    // and provides basic transport controls (play/pause/stop, seek, volume,
    // pan, playback speed, loop).
    //
    // Layout:
    //   -----------------------------------------------------------------
    //   |  details (left, ~400 px) | waveform + transport (right)       |
    //   -----------------------------------------------------------------
    //
    // Interactions on the waveform canvas:
    //   - Mouse wheel over the waveform zooms around the cursor.
    //   - Left- or middle-drag pans horizontally.
    //   - Left-click on the waveform seeks to that time.
    //   - "Fit" refits the whole file; "1s" sets 1 second per screen.
    //
    // Accepts "CONTENT_BROWSER_ITEM" drag/drop payloads from the content
    // browser; audio files are opened, other file types are ignored.
    class audio_viewer_window : public base_window {
    public:

        audio_viewer_window(const std::filesystem::path& path);
        ~audio_viewer_window();

        DEFAULT_GETTER(audio_details, details)


        // Loads the given file and shows it. Repeated calls replace the
        // current contents. On failure the viewer still opens and displays
        // the error in the waveform panel.
        void open(const std::filesystem::path& path);


        // Stops playback and clears the currently loaded file.
        void close_audio();


        void window(const f32 delta_time) override;


        void update(const f32 delta_time) override;


        bool serialize(const std::filesystem::path& project_file, const GLT::serializer::option option) override;

    private:

        // drawing helpers ---------------------------------------------------------------------------------------------
        void draw_details_panel();

        void draw_audio_panel();

        void draw_transport_toolbar();

        void draw_waveform_canvas();

        void draw_waveform_channel(ImDrawList* draw, const std::vector<audio_peak_pair>& peaks, const ImVec2& ch_min, const ImVec2& ch_max, 
            f32 visible_start_sec, f32 visible_end_sec);

        void draw_time_ruler(ImDrawList* draw, const ImVec2& min, const ImVec2& max, f32 visible_start_sec, f32 visible_end_sec);

        void draw_empty_state(ImDrawList* draw, const ImVec2& min, const ImVec2& max);

        void draw_error_state(ImDrawList* draw, const ImVec2& min, const ImVec2& max);

        void handle_drag_drop();

        // details sections --------------------------------------------------------------------------------------------
        void draw_file_section();

        void draw_audio_section();

        void draw_format_section();

        void draw_playback_section();

        // processing helpers ------------------------------------------------------------------------------------------
        void populate_details();

        // Decode the file into interleaved f32 samples. Fills m_raw_samples,
        // m_raw_channels, m_raw_sample_rate. Returns false on failure.
        bool decode_file();

        // Compute per-channel min/max peaks from m_raw_samples at
        // PEAKS_PER_SECOND resolution, then free m_raw_samples.
        void compute_peaks();

        // Refresh m_playhead_sec from the audio plugin (if playing).
        void update_playhead();

        // Start / stop / pause / resume / seek via the audio plugin.
        void transport_play();
        void transport_pause();
        void transport_stop();
        void transport_seek(f32 seconds);
        void transport_toggle();

        // Cache a weak ref to the audio plugin, load the file into it for playback, and store the resulting sound handle.
        // Register with the audio plugin (main-thread only - SoLoud's public API is not thread-safe).
        // This is what actually loads the sound for playback; the waveform display above came from dr_libs.
        void register_with_audio_plugin();

        void apply_decode_result(decode_result&& result);


        // Lifetime token. All background callbacks capture a weak_ptr to it and bail if it has expired.
        // Prevents them from touching a destroyed window.
        std::shared_ptr<int>                            m_lifetime_token = std::make_shared<int>(0);
        bool                                            m_loading = false;          // UI state while the background decode runs.

        audio_details                                   m_details{};
        bool                                            m_has_audio = false;
        bool                                            m_load_failed = false;

        // Decoded raw samples (interleaved f32). Only valid during decode and
        // peak computation; freed afterwards to keep memory low.
        std::vector<f32>                                m_raw_samples{};
        u32                                             m_raw_channels = 0;
        u32                                             m_raw_sample_rate = 0;

        // Peak data per channel: m_peaks[ch] is a vector of min/max pairs.
        std::vector<std::vector<audio_peak_pair>>       m_peaks{};
        u32                                             m_peak_count = 0;

        // Display state.
        f32                                             m_visible_start_sec = 0.0f;
        f32                                             m_visible_end_sec   = 0.0f;
        bool                                            m_fit_pending = true;

        // Playback state.
        GLT::weak_ref<GLT::audio::i_audio_plugin>       m_audio_manager{};
        handle                                          m_sound_handle = 0;
        handle                                          m_voice_handle = 0;
        bool                                            m_playing = false;
        bool                                            m_looping = false;
        f32                                             m_volume = 1.0f;
        f32                                             m_pan = 0.0f;
        f32                                             m_playback_speed = 1.0f;
        f32                                             m_playhead_sec = 0.0f;

        bool                                            m_is_dragging_playhead = false;

    };

}
