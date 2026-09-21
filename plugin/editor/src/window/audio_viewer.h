#pragma once

#include <imgui.h>
#include <glm/vec2.hpp>

#include <array>
#include <vector>
#include <filesystem>
#include <string>

#include <asset/type.h>                 // GLT::asset::handle, INVALID_HANDLE
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


    struct audio_peak_pair {

        f32                                             min = 0.0f;
        f32                                             max = 0.0f;
    };


    struct waveform_view {

        f32                                             visible_start_sec = 0.0f;
        f32                                             visible_end_sec = 0.0f;
    };


    // Produced on the worker thread and applied on the main thread.
    struct decode_result {

        bool                                            ok = false;
        audio_details                                   details{};
        std::vector<std::vector<audio_peak_pair>>       peaks{};
        u32                                             peak_count = 0;

        // Registry handle for the loaded audio asset. On discard this is
        // unloaded by the main-thread callback; on success it becomes
        // m_asset_handle and is used for playback.
        GLT::asset::handle                              asset_handle = INVALID_HANDLE;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Dockable editor window that previews an audio file (loaded through the
    // asset registry), shows its metadata, and provides basic transport
    // controls (play/pause/stop, seek, volume, pan, playback speed, loop).
    class audio_viewer_window : public base_window {
    public:

        audio_viewer_window(const std::filesystem::path& path);
        ~audio_viewer_window();

        DEFAULT_GETTER(audio_details, details)


        void open(const std::filesystem::path& path);


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

        void draw_waveform_channel(ImDrawList* draw, const std::vector<audio_peak_pair>& peaks, const ImVec2& ch_min, 
            const ImVec2& ch_max, f32 visible_start_sec, f32 visible_end_sec);

        void draw_time_ruler(ImDrawList* draw, const ImVec2& min, const ImVec2& max, f32 visible_start_sec, f32 visible_end_sec);

        void draw_empty_state(ImDrawList* draw, const ImVec2& min, const ImVec2& max);

        void draw_error_state(ImDrawList* draw, const ImVec2& min, const ImVec2& max);

        void handle_drag_drop();

        // details sections --------------------------------------------------------------------------------------------
        void draw_file_section();

        void draw_audio_section();

        void draw_format_section();

        void draw_playback_section();

        // transport ---------------------------------------------------------------------------------------------------
        void update_playhead();

        void transport_play();

        void transport_pause();

        void transport_stop();

        void transport_seek(f32 seconds);

        void transport_toggle();


        void apply_decode_result(decode_result&& result);

        // Stops the active voice (if any) and unloads the current asset.
        void teardown_playback();


        std::shared_ptr<int>                            m_lifetime_token = std::make_shared<int>(0);
        bool                                            m_loading = false;
        u64                                             m_load_generation = 0;

        audio_details                                   m_details{};
        bool                                            m_has_audio = false;
        bool                                            m_load_failed = false;

        std::vector<std::vector<audio_peak_pair>>       m_peaks{};
        u32                                             m_peak_count = 0;

        // Display state.
        f32                                             m_visible_start_sec = 0.0f;
        f32                                             m_visible_end_sec   = 0.0f;
        bool                                            m_fit_pending = true;

        // Playback state.
        GLT::weak_ref<GLT::audio::i_audio_plugin>       m_audio_manager{};
        GLT::asset::handle                              m_asset_handle = INVALID_HANDLE;
        handle                                          m_voice_handle = 0;
        bool                                            m_playing = false;
        bool                                            m_looping = false;
        f32                                             m_volume = 1.0f;
        f32                                             m_pan = 0.0f;
        f32                                             m_playback_speed = 1.0f;
        f32                                             m_playhead_sec = 0.0f;
    };

}
