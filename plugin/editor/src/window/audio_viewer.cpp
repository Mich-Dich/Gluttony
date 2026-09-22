
#include "util/pch.h"
#include "audio_viewer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <config/imgui_config.h>
#include <asset/audio.h>
#include <plugin_system/i_asset_registry_plugin.h>
#include <plugin_system/plugin_manager.h>
#include <plugin_system/i_audio_plugin.h>
#include <resource_manager/icon_manager.h>

#include "util/ui/pannel_collection.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    constexpr f32                                   DETAILS_PANEL_WIDTH = 400.0f;

    constexpr u32                                   RULER_TARGET_TICKS = 8;

    static constexpr const char*                    DRAG_PAYLOAD_ID = "CONTENT_BROWSER_ITEM";

    static constexpr u32                            PEAKS_PER_SECOND = 2048;

    static constexpr f32                            MIN_VISIBLE_SEC = 0.002f;

    static constexpr f32                            ZOOM_STEP = 1.25f;

    static constexpr f32                            FIT_PADDING = 0.98f;

    static constexpr f32                            RULER_HEIGHT = 22.0f;

    static constexpr f32                            CHANNEL_GAP = 6.0f;

    static constexpr f32                            WAVEFORM_PADDING = 6.0f;

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    namespace audio {

        std::string pretty_format(const std::string& ext);

    }

    bool is_audio_extension(const std::string& ext);

    std::string format_time(f32 seconds);

    f32  nice_time_interval(f32 visible_duration, int target_ticks);

    static audio_details populate_details_from_disk(const std::filesystem::path& path, const std::string& name, const std::string& ext);

    // Interleaved -> per-channel min/max peaks at PEAKS_PER_SECOND resolution.
    static void compute_peaks_from_asset(const GLT::asset::audio::audio_asset& asset, 
        std::vector<std::vector<audio_peak_pair>>& out_peaks, u32& out_peak_count);

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    namespace audio {

        std::string pretty_format(const std::string& ext) {

            if (ext == ".wav")                      return "WAV";
            if (ext == ".mp3")                      return "MP3";
            if (ext == ".flac")                     return "FLAC";
            if (ext == ".ogg" || ext == ".oga")     return "OGG";
            if (ext == ".opus")                     return "Opus";
            if (ext.empty())                        return "Unknown";
            return GLT::util::to_lower(ext.substr(1));
        }

    }


    bool is_audio_extension(const std::string& ext) {

        return ext == ".wav" || ext == ".mp3" || ext == ".flac"
            || ext == ".ogg" || ext == ".oga" || ext == ".opus";
    }


    std::string format_time(f32 seconds) {

        if (seconds < 0.0f) seconds = 0.0f;
        const int total_ms = static_cast<int>(seconds * 1000.0f + 0.5f);
        const int ms = total_ms % 1000;
        const int total_s = total_ms / 1000;
        const int s = total_s % 60;
        const int total_m = total_s / 60;
        const int m = total_m % 60;
        const int h = total_m / 60;

        char buf[32];
        if (h > 0)
            std::snprintf(buf, sizeof(buf), "%d:%02d:%02d.%03d", h, m, s, ms);
        else
            std::snprintf(buf, sizeof(buf), "%d:%02d.%03d", m, s, ms);

        return std::string(buf);
    }


    f32 nice_time_interval(f32 visible_duration, int target_ticks) {

        if (visible_duration <= 0.0f || target_ticks <= 0)
            return 1.0f;

        const f32 raw = visible_duration / static_cast<f32>(target_ticks);
        const f32 magnitude = std::pow(10.0f, std::floor(std::log10(raw)));
        const f32 normalized = raw / magnitude;

        f32 nice;
        if      (normalized < 1.5f)     nice = 1.0f;
        else if (normalized < 3.5f)     nice = 2.0f;
        else if (normalized < 7.5f)     nice = 5.0f;
        else                            nice = 10.0f;

        return nice * magnitude;
    }


    static audio_details populate_details_from_disk(const std::filesystem::path& path, const std::string& name, const std::string& ext) {

        audio_details details{};
        details.path = GLT::project::extract_path_from_project_content_dir(path);
        details.name = name;
        details.extension = ext;
        details.format = audio::pretty_format(ext);

        std::error_code error{};
        const auto size = GLT::vfs::file_size(path, error);
        if (!error)
            details.file_size = static_cast<u64>(size);

        const GLT::system_time time = GLT::vfs::last_write_time(path, error);
        if (!error)
            details.last_modified = time;

        return details;
    }


    static void compute_peaks_from_asset(const GLT::asset::audio::audio_asset& asset,
        std::vector<std::vector<audio_peak_pair>>& out_peaks, u32& out_peak_count) {

        out_peaks.clear();
        out_peak_count = 0;

        const u32 channels    = asset.format.channels;
        const u32 sample_rate = asset.format.sample_rate;
        if (channels == 0 || sample_rate == 0 || asset.samples.empty())
            return;

        const u64 total_frames     = asset.samples.size() / channels;
        const u64 samples_per_peak = std::max<u64>(1, sample_rate / PEAKS_PER_SECOND);
        const u64 peak_count       = (total_frames + samples_per_peak - 1) / samples_per_peak;

        out_peak_count = static_cast<u32>(peak_count);
        out_peaks.resize(channels);
        for (auto& v : out_peaks) v.resize(peak_count);

        for (u64 p = 0; p < peak_count; ++p) {

            const u64 start = p * samples_per_peak;
            const u64 end   = std::min(start + samples_per_peak, total_frames);

            for (u32 ch = 0; ch < channels; ++ch) {

                f32 mn = 0.0f, mx = 0.0f;
                for (u64 i = start; i < end; ++i) {
                    const f32 s = asset.samples[static_cast<size_t>(i) * channels + ch];
                    if (s < mn) mn = s;
                    if (s > mx) mx = s;
                }
                out_peaks[ch][static_cast<size_t>(p)] = { mn, mx };
            }
        }
    }

    // CLASS IMPLEMENTATION ============================================================================================

    audio_viewer_window::audio_viewer_window(const std::filesystem::path& path) {

        open(path);
    }


    audio_viewer_window::~audio_viewer_window() {

        teardown_playback();
    }

    // CLASS PUBLIC ====================================================================================================

    void audio_viewer_window::open(const std::filesystem::path& path) {

        // Invalidate any in-flight worker for a previous file.
        ++m_load_generation;

        // Stop playback and release the previous asset (main-thread only).
        teardown_playback();

        m_loading      = false;
        m_details      = {};
        m_has_audio    = false;
        m_load_failed  = false;

        m_peaks.clear();
        m_peak_count = 0;
        m_visible_start_sec = 0.0f;
        m_visible_end_sec   = 0.0f;
        m_playhead_sec      = 0.0f;
        m_fit_pending       = true;
        m_show_window       = true;

        m_details.path = GLT::project::extract_path_from_project_content_dir(path);
        m_details.name = path.filename().string();
        m_details.extension = GLT::util::to_lower(path.extension().string());
        m_details.format = audio::pretty_format(m_details.extension);

        cache_window_state();
        make_window_name((std::string("AUD: ") + m_details.name).c_str());

        std::error_code error{};
        VALIDATE(GLT::vfs::exists(path, error) && !error, m_load_failed = true; return, "",
            "file not found [{}]", path.generic_string())
        VALIDATE(!GLT::vfs::is_directory(path, error) && !error, m_load_failed = true; return, "",
            "path is a directory [{}]", path.generic_string())

        // --- background load through the asset registry -------------------------
        m_loading = true;

        const std::filesystem::path abs_path = path;
        const std::string name = m_details.name;
        const std::string ext  = m_details.extension;
        const u64 generation   = m_load_generation;
        std::weak_ptr<int> lifetime = m_lifetime_token;

        GLT::thread_pool::push([this, lifetime, abs_path, name, ext, generation]() {

            // worker thread -------------------------------------------------------------------------------------------
            auto result = std::make_shared<decode_result>();
            result->details = populate_details_from_disk(abs_path, name, ext);

            auto registry = GLT::asset::registry::get_ref();
            if (!registry) {
                result->ok = false;
            } else if (auto loaded = registry->load(abs_path); loaded) {

                result->asset_handle = *loaded;

                const auto* runtime = registry->data(result->asset_handle);
                const auto* asset   = dynamic_cast<const GLT::asset::audio::audio_asset*>(runtime);

                if (asset) {

                    result->details.sample_rate = asset->format.sample_rate;
                    result->details.channels    = asset->format.channels;
                    result->details.bit_depth   = asset->format.bit_depth;
                    result->details.duration_sec =
                        static_cast<f32>(asset->format.frame_count) /
                        static_cast<f32>(std::max<u32>(1, asset->format.sample_rate));

                    if (result->details.duration_sec > 0.0f) {
                        result->details.bitrate_kbps = static_cast<u32>(
                            (static_cast<f64>(result->details.file_size) * 8.0) /
                            (static_cast<f64>(result->details.duration_sec) * 1000.0) + 0.5);
                    }

                    compute_peaks_from_asset(*asset, result->peaks, result->peak_count);
                    result->ok = true;
                }
            }

            GLT::thread_pool::push_main([this, lifetime, result, generation]() {

                // main thread -----------------------------------------------------------------------------------------
                if (lifetime.expired())
                    return;                          // window was destroyed while loading

                if (generation != m_load_generation) {
                    // A newer open() superseded us - release whatever we loaded.
                    if (result->asset_handle != INVALID_HANDLE) {
                        if (auto registry = GLT::asset::registry::get_ref())
                            registry->unload(result->asset_handle);
                    }
                    return;
                }

                apply_decode_result(std::move(*result));
            });
        });
    }


    void audio_viewer_window::close_audio() {

        ++m_load_generation;                // invalidate in-flight workers
        teardown_playback();

        m_details = {};
        m_has_audio = false;
        m_load_failed = false;
        m_peaks.clear();
        m_peak_count = 0;
        m_visible_start_sec = 0.0f;
        m_visible_end_sec = 0.0f;
        m_playhead_sec = 0.0f;
    }


    void audio_viewer_window::window(const f32 /*delta_time*/) {

        if (!m_show_window)
            return;

        apply_pending_dock();
        ImGui::SetNextWindowSizeConstraints(ImVec2(720.0f, 420.0f), ImVec2(std::numeric_limits<f32>::max(), 
            std::numeric_limits<f32>::max()));

        if (ImGui::Begin(m_window_id.c_str(), &m_show_window)) {

            handle_drag_drop();
            UI::custom_frame(DETAILS_PANEL_WIDTH, true, ImGui::GetColorU32(GLT::imgui_config::get_default_gray1_ref()),
                [this]() {
                    draw_details_panel();
                },
                [this]() {
                    draw_audio_panel();
                });
        }
        ImGui::End();
    }
 

    void audio_viewer_window::update(const f32 /*delta_time*/) { update_playhead(); }


    bool audio_viewer_window::serialize(const std::filesystem::path& /*project_file*/, const GLT::serializer::option /*option*/) { return false; }

    // CLASS PRIVATE ===================================================================================================

    void audio_viewer_window::teardown_playback() {

        if (auto plugin = m_audio_manager.lock()) {
            if (m_voice_handle)
                plugin->stop(m_voice_handle);
        }
        m_voice_handle = 0;
        m_playing = false;

        if (m_asset_handle != INVALID_HANDLE) {
            if (auto registry = GLT::asset::registry::get_ref())
                registry->unload(m_asset_handle);
            m_asset_handle = INVALID_HANDLE;
        }
    }

    // details panel ---------------------------------------------------------------------------------------------------

    void audio_viewer_window::draw_details_panel() {

        const std::string title = m_details.name.empty() ? std::string("(no audio)") : m_details.name;
        ImGui::TextUnformatted(title.c_str());

        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", m_details.path.generic_string().c_str());
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        if (m_load_failed) {

            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 90, 90, 255));
            ImGui::TextUnformatted("Failed to open file.");
            ImGui::PopStyleColor();
            return;
        }

        if (!m_has_audio) {

            ImGui::TextDisabled("No audio loaded.");
            return;
        }

        draw_playback_section();
        draw_audio_section();
        draw_format_section();
        draw_file_section();
    }


    void audio_viewer_window::draw_file_section() {

        if (!ImGui::CollapsingHeader("File", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        if (UI::begin_table("audio_detail_and_edit", false)) {

            UI::table_row("name",   m_details.name.c_str());
            UI::table_row("format", m_details.format.c_str());
            UI::table_row("size",   GLT::util::format_bytes(m_details.file_size));
            UI::end_table();
        }
    }


    void audio_viewer_window::draw_audio_section() {

        if (!ImGui::CollapsingHeader("Audio", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        if (UI::begin_table("audio_detail_and_edit", false)) {

            UI::table_row("duration",    format_time(m_details.duration_sec).c_str());
            UI::table_row("sample rate", std::to_string(m_details.sample_rate) + " Hz");
            UI::table_row("channels",    m_details.channels == 1 ? "mono"
                                       : m_details.channels == 2 ? "stereo"
                                       : std::to_string(m_details.channels));
            UI::table_row("bit depth",   m_details.bit_depth ? std::to_string(m_details.bit_depth) + " bit" : std::string("--"));
            UI::table_row("bitrate",     m_details.bitrate_kbps ? std::to_string(m_details.bitrate_kbps) + " kbps" : std::string("--"));
            UI::table_row("peaks",       std::to_string(m_peak_count));
            UI::end_table();
        }
    }


    void audio_viewer_window::draw_format_section() {

        if (!ImGui::CollapsingHeader("Format", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        std::string codec = "Unknown";
        if      (m_details.extension == ".wav")                                  codec = "PCM / uncompressed";
        else if (m_details.extension == ".mp3")                                  codec = "MPEG-1 Audio Layer III";
        else if (m_details.extension == ".flac")                                 codec = "FLAC (lossless)";
        else if (m_details.extension == ".ogg" || m_details.extension == ".oga") codec = "Vorbis";
        else if (m_details.extension == ".opus")                                 codec = "Opus";

        if (UI::begin_table("audio_detail_and_edit", false)) {

            UI::table_row("codec",      codec.c_str());
            UI::table_row("compressed", std::string_view(GLT::util::to_string(m_details.extension == ".wav")));
            UI::end_table();
        }
    }


    void audio_viewer_window::draw_playback_section() {

        if (!ImGui::CollapsingHeader("Playback", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        if (UI::begin_table("audio_detail_and_edit", false)) {

            // Volume
            {
                f32 v = m_volume;
                UI::table_row(
                    []{ ImGui::TextUnformatted("volume"); },
                    [&v] {
                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::SliderFloat("##av_volume", &v, 0.0f, 1.5f, "%.2f");
                    });
                if (v != m_volume) {
                    m_volume = v;
                    if (auto plugin = m_audio_manager.lock(); plugin && m_voice_handle)
                        plugin->set_volume(m_voice_handle, m_volume);
                }
            }

            // Pan
            {
                f32 p = m_pan;
                UI::table_row(
                    []{ ImGui::TextUnformatted("pan"); },
                    [&p] {
                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::SliderFloat("##av_pan", &p, -1.0f, 1.0f, "%.2f");
                    });
                if (p != m_pan) {
                    m_pan = p;
                    if (auto plugin = m_audio_manager.lock(); plugin && m_voice_handle)
                        plugin->set_pan(m_voice_handle, m_pan);
                }
            }

            // Playback speed
            {
                f32 s = m_playback_speed;
                UI::table_row(
                    []{ ImGui::TextUnformatted("speed"); },
                    [&s] {
                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::SliderFloat("##av_speed", &s, 0.25f, 4.0f, "%.2fx");
                    });
                if (s != m_playback_speed) {
                    m_playback_speed = s;
                    if (auto plugin = m_audio_manager.lock(); plugin && m_voice_handle)
                        plugin->set_play_speed(m_voice_handle, m_playback_speed);
                }
            }

            // Loop
            {
                bool l = m_looping;
                UI::table_row("loop", l);
                if (l != m_looping) {
                    m_looping = l;
                    if (auto plugin = m_audio_manager.lock(); plugin && m_voice_handle)
                        plugin->set_looping(m_voice_handle, m_looping);
                }
            }

            UI::end_table();
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
    }

    // audio panel -----------------------------------------------------------------------------------------------------

    void audio_viewer_window::draw_audio_panel() {

        draw_transport_toolbar();
        ImGui::Separator();
        draw_waveform_canvas();
    }


    void audio_viewer_window::draw_transport_toolbar() {

        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ImGui::SameLine();

        const bool can_transport = m_has_audio && !m_loading && m_audio_manager.lock();
        ImGui::BeginDisabled(!can_transport);
        {
            if (ImGui::Button(m_playing ? "Pause" : "Play", ImVec2(64.0f, 0.0f)))
                transport_toggle();

            ImGui::SameLine();
            if (ImGui::Button("Stop", ImVec2(64.0f, 0.0f)))
                transport_stop();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled("|");

        ImGui::SameLine();
        ImGui::Text("%s / %s", format_time(m_playhead_sec).c_str(), format_time(m_details.duration_sec).c_str());

        ImGui::SameLine();
        ImGui::TextDisabled("|");

        ImGui::SameLine();
        if (ImGui::Button("Fit"))
            m_fit_pending = true;

        ImGui::SameLine();
        if (ImGui::Button("1s")) {

            m_fit_pending = false;
            const f32 center = 0.5f * (m_visible_start_sec + m_visible_end_sec);
            m_visible_start_sec = std::max(0.0f, center - 0.5f);
            m_visible_end_sec   = std::min(m_details.duration_sec, m_visible_start_sec + 1.0f);
        }
    }


    void audio_viewer_window::draw_waveform_canvas() {

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
        const ImVec2 canvas_max = ImVec2(canvas_min.x + avail.x, canvas_min.y + avail.y);
        auto& main_color = GLT::imgui_config::get_main_color_ref();
        const auto main_color_u32 = IM_COL32(main_color.x * 255, main_color.y * 255, main_color.z * 255, 255);

        ImGui::InvisibleButton("##av_canvas", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);

        ImDrawList* draw = ImGui::GetWindowDrawList();

        if (m_loading) {

            const char* msg = "Decoding...";
            const ImVec2 sz = ImGui::CalcTextSize(msg);
            draw->AddText(ImVec2((canvas_min.x + canvas_max.x - sz.x) * 0.5f, (canvas_min.y + canvas_max.y - sz.y) * 0.5f),
                IM_COL32(200, 200, 200, 255), msg);
            return;
        }

        if (m_load_failed) {
            draw_error_state(draw, canvas_min, canvas_max);
            return;
        }

        if (!m_has_audio || m_peaks.empty()) {
            draw_empty_state(draw, canvas_min, canvas_max);
            return;
        }

        // Input: pan & zoom -------------------------------------------------------------------------------------------
        const bool hovered = ImGui::IsItemHovered();
        const bool active  = ImGui::IsItemActive();

        if (m_fit_pending) {
            m_visible_start_sec = 0.0f;
            m_visible_end_sec   = std::max(MIN_VISIBLE_SEC, m_details.duration_sec);
            m_fit_pending = false;
        }

        m_visible_start_sec = std::clamp(m_visible_start_sec, 0.0f, std::max(0.0f, m_details.duration_sec - MIN_VISIBLE_SEC));
        m_visible_end_sec   = std::clamp(m_visible_end_sec, m_visible_start_sec + MIN_VISIBLE_SEC, m_details.duration_sec);

        const f32 visible_duration = m_visible_end_sec - m_visible_start_sec;
        const f32 width = avail.x;

        if (hovered) {

            const f32 wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {

                const f32 cursor_t = std::clamp((ImGui::GetIO().MousePos.x - canvas_min.x) / width, 0.0f, 1.0f);
                const f32 cursor_sec = m_visible_start_sec + cursor_t * visible_duration;

                const f32 scale = (wheel > 0.0f) ? (1.0f / ZOOM_STEP) : ZOOM_STEP;
                const f32 new_duration = std::clamp(visible_duration * scale, MIN_VISIBLE_SEC, m_details.duration_sec);

                m_visible_start_sec = cursor_sec - cursor_t * new_duration;
                m_visible_end_sec   = m_visible_start_sec + new_duration;

                m_visible_start_sec = std::clamp(m_visible_start_sec, 0.0f, std::max(0.0f, m_details.duration_sec - new_duration));
                m_visible_end_sec   = m_visible_start_sec + new_duration;
            }
        }

        if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {

            const f32 dx_sec = ImGui::GetIO().MouseDelta.x / width * visible_duration;
            m_visible_start_sec -= dx_sec;
            m_visible_end_sec   -= dx_sec;

            const f32 new_dur = m_visible_end_sec - m_visible_start_sec;
            m_visible_start_sec = std::clamp(m_visible_start_sec, 0.0f, std::max(0.0f, m_details.duration_sec - new_dur));
            m_visible_end_sec   = m_visible_start_sec + new_dur;
        }

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {

            const f32 t = std::clamp((ImGui::GetIO().MousePos.x - canvas_min.x) / width, 0.0f, 1.0f);
            transport_seek(m_visible_start_sec + t * visible_duration);
        }

        const f32 ruler_top    = canvas_min.y;
        const f32 ruler_bottom = ruler_top + RULER_HEIGHT;
        const f32 wave_top     = ruler_bottom + 4.0f;
        const f32 wave_bottom  = canvas_max.y;
        const ImVec2 ruler_min(canvas_min.x, ruler_top);
        const ImVec2 ruler_max(canvas_max.x, ruler_bottom);

        draw_time_ruler(draw, ruler_min, ruler_max, m_visible_start_sec, m_visible_end_sec);

        const int n_channels = static_cast<int>(m_peaks.size());
        const f32 ch_total = wave_bottom - wave_top;
        const f32 ch_each  = (ch_total - CHANNEL_GAP * (n_channels - 1)) / std::max(1, n_channels);
        for (int ch = 0; ch < n_channels; ++ch) {

            const f32 y0 = wave_top + ch * (ch_each + CHANNEL_GAP);
            const f32 y1 = y0 + ch_each;
            const ImVec2 cmin(canvas_min.x, y0);
            const ImVec2 cmax(canvas_max.x, y1);
            draw_waveform_channel(draw, m_peaks[ch], cmin, cmax, m_visible_start_sec, m_visible_end_sec);
        }

        if (m_details.duration_sec > 0.0f) {

            const f32 t = (m_playhead_sec - m_visible_start_sec) / visible_duration;
            if (t >= 0.0f && t <= 1.0f) {

                const f32 px = canvas_min.x + t * width;
                draw->AddLine(ImVec2(px, ruler_top), ImVec2(px, wave_bottom), main_color_u32, 1.5f);
                draw->AddTriangleFilled(
                    ImVec2(px - 5.0f, ruler_top),
                    ImVec2(px + 5.0f, ruler_top),
                    ImVec2(px,       ruler_top + 7.0f),
                    main_color_u32);
            }
        }
    }


    void audio_viewer_window::draw_waveform_channel(ImDrawList* draw, const std::vector<audio_peak_pair>& peaks, const ImVec2& ch_min, 
        const ImVec2& ch_max, f32 visible_start_sec, f32 visible_end_sec) {

        if (peaks.empty())
            return;

        const f32 width    = ch_max.x - ch_min.x;
        const f32 height   = ch_max.y - ch_min.y;
        const f32 center_y = ch_min.y + height * 0.5f;
        const f32 half_h   = std::max(1.0f, height * 0.5f - WAVEFORM_PADDING);
        const ImU32 line_color   = IM_COL32(160, 160, 160, 220);
        const ImU32 center_color = IM_COL32(255, 255, 255, 255);
        draw->AddLine(ImVec2(ch_min.x, center_y), ImVec2(ch_max.x, center_y), center_color, 1.0f);

        const f32 visible_duration = visible_end_sec - visible_start_sec;
        if (visible_duration <= 0.0f)
            return;

        const f32 sec_per_peak = 1.0f / static_cast<f32>(PEAKS_PER_SECOND);
        const i64 peak_count   = static_cast<i64>(peaks.size());
        const i32 pixel_count  = static_cast<i32>(width);
        for (i32 px = 0; px < pixel_count; ++px) {

            const f32 t0   = static_cast<f32>(px)     / static_cast<f32>(pixel_count);
            const f32 t1   = static_cast<f32>(px + 1) / static_cast<f32>(pixel_count);
            const f32 sec0 = visible_start_sec + t0 * visible_duration;
            const f32 sec1 = visible_start_sec + t1 * visible_duration;

            i64 p0 = static_cast<i64>(sec0 / sec_per_peak);
            i64 p1 = static_cast<i64>(sec1 / sec_per_peak);
            p0 = std::clamp<i64>(p0, 0, peak_count - 1);
            p1 = std::clamp<i64>(p1, p0 + 1, peak_count);

            f32 mn = 0.0f, mx = 0.0f;
            for (i64 p = p0; p < p1; ++p) {
                mn = std::min(mn, peaks[static_cast<size_t>(p)].min);
                mx = std::max(mx, peaks[static_cast<size_t>(p)].max);
            }

            const f32 y_top = center_y - mx * half_h;
            const f32 y_bot = center_y - mn * half_h;
            const f32 x     = ch_min.x + static_cast<f32>(px);
            draw->AddLine(ImVec2(x, y_top), ImVec2(x, y_bot), line_color, 1.0f);
        }
    }


    void audio_viewer_window::draw_time_ruler(ImDrawList* draw, const ImVec2& min, const ImVec2& max, f32 visible_start_sec, 
        f32 visible_end_sec) {

        draw->AddRectFilled(min, max, IM_COL32(32, 32, 38, 255));

        const f32 visible_duration = visible_end_sec - visible_start_sec;
        if (visible_duration <= 0.0f) return;

        const f32 interval = nice_time_interval(visible_duration, RULER_TARGET_TICKS);
        const f32 width    = max.x - min.x;

        const f32 first = std::ceil(visible_start_sec / interval) * interval;

        const ImU32 tick_color = IM_COL32(140, 140, 150, 200);
        const ImU32 text_color = IM_COL32(200, 200, 210, 255);

        for (f32 t = first; t <= visible_end_sec + interval * 0.001f; t += interval) {

            const f32 u = (t - visible_start_sec) / visible_duration;
            if (u < 0.0f || u > 1.0f) continue;

            const f32 x = min.x + u * width;
            draw->AddLine(ImVec2(x, max.y - 6.0f), ImVec2(x, max.y), tick_color, 1.0f);

            const std::string label = format_time(t);
            const ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
            if (x + text_size.x + 4.0f < max.x)
                draw->AddText(ImVec2(x + 3.0f, min.y + 2.0f), text_color, label.c_str());
        }

        draw->AddLine(ImVec2(min.x, max.y - 0.5f), ImVec2(max.x, max.y - 0.5f),
                      IM_COL32(60, 60, 70, 255), 1.0f);
    }


    void audio_viewer_window::draw_empty_state(ImDrawList* draw, const ImVec2& min, const ImVec2& max) {

        const char* line1 = "No audio loaded";
        const char* line2 = "Drag an audio file from the Content Browser, or double-click one there.";

        const ImVec2 s1 = ImGui::CalcTextSize(line1);
        const ImVec2 s2 = ImGui::CalcTextSize(line2);

        const f32 cx = (min.x + max.x) * 0.5f;
        const f32 cy = (min.y + max.y) * 0.5f;

        draw->AddText(ImVec2(cx - s1.x * 0.5f, cy - s1.y - 4.0f), IM_COL32(200, 200, 200, 255), line1);
        draw->AddText(ImVec2(cx - s2.x * 0.5f, cy + 4.0f),          IM_COL32(140, 140, 140, 255), line2);
    }


    void audio_viewer_window::draw_error_state(ImDrawList* draw, const ImVec2& min, const ImVec2& max) {

        const char* line1 = "Failed to load audio";
        const char* line2 = m_details.path.generic_string().c_str();

        const ImVec2 s1 = ImGui::CalcTextSize(line1);
        const ImVec2 s2 = ImGui::CalcTextSize(line2);

        const f32 cx = (min.x + max.x) * 0.5f;
        const f32 cy = (min.y + max.y) * 0.5f;

        draw->AddText(ImVec2(cx - s1.x * 0.5f, cy - s1.y - 4.0f), IM_COL32(230, 90, 90, 255), line1);
        draw->AddText(ImVec2(cx - s2.x * 0.5f, cy + 4.0f),          IM_COL32(140, 140, 140, 255), line2);
    }


    void audio_viewer_window::handle_drag_drop() {

        const ImVec2 min = ImGui::GetWindowPos();
        const ImVec2 max = ImVec2(min.x + ImGui::GetWindowSize().x, min.y + ImGui::GetWindowSize().y);

        if (!ImGui::BeginDragDropTargetCustom(ImRect(min, max), ImGui::GetID("##av_drop_target")))
            return;

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DRAG_PAYLOAD_ID)) {

            const char* raw = static_cast<const char*>(payload->Data);
            if (raw && raw[0] != '\0') {

                const std::filesystem::path dropped(raw);
                if (is_audio_extension(GLT::util::to_lower(dropped.extension().string())))
                    open(dropped);
            }
        }

        ImGui::EndDragDropTarget();
    }

    // -----------------------------------------------------------------------------------------------------------------

    void audio_viewer_window::update_playhead() {

        auto plugin = m_audio_manager.lock();
        if (!plugin || !m_voice_handle)
            return;

        if (!plugin->is_valid(m_voice_handle)) {

            m_playing = false;
            m_voice_handle = 0;
            m_playhead_sec = 0.0f;
            return;
        }

        m_playhead_sec = plugin->get_playback_position(m_voice_handle);
        m_playing = (plugin->get_state(m_voice_handle) == GLT::asset::audio::state::playing);
    }


    void audio_viewer_window::apply_decode_result(decode_result&& result) {

        m_loading = false;

        if (!result.ok) {
            // If the registry did produce a handle but our post-processing failed,
            // don't leak it.
            if (result.asset_handle != INVALID_HANDLE) {
                if (auto registry = GLT::asset::registry::get_ref())
                    registry->unload(result.asset_handle);
            }
            m_load_failed = true;
            return;
        }

        m_details = std::move(result.details);
        m_peaks = std::move(result.peaks);
        m_peak_count = result.peak_count;

        m_audio_manager = GLT::audio::manager::get_ref();
        m_asset_handle = result.asset_handle;

        m_has_audio   = true;
        m_fit_pending = true;
    }


    void audio_viewer_window::transport_play() {

        auto plugin = m_audio_manager.lock();
        if (!plugin || m_asset_handle == INVALID_HANDLE)
            return;

        if (m_voice_handle && plugin->is_valid(m_voice_handle)) {
            plugin->resume(m_voice_handle);
            m_playing = true;
            return;
        }

        GLT::asset::audio::source_config cfg;
        cfg.volume = m_volume;
        cfg.pan = m_pan;
        cfg.loop = m_looping;
        cfg.play_speed = m_playback_speed;
        cfg.is_3d = false;

        m_voice_handle = plugin->play(m_asset_handle, cfg);
        m_playing = (m_voice_handle != 0);
    }


    void audio_viewer_window::transport_pause() {

        auto plugin = m_audio_manager.lock();
        if (!plugin || !m_voice_handle)
            return;

        plugin->pause(m_voice_handle);
        m_playing = false;
    }


    void audio_viewer_window::transport_stop() {

        auto plugin = m_audio_manager.lock();
        if (!plugin)
            return;

        if (m_voice_handle) {
            plugin->stop(m_voice_handle);
            m_voice_handle = 0;
        }
        m_playing = false;
        m_playhead_sec = 0.0f;
    }


    void audio_viewer_window::transport_seek(f32 seconds) {

        seconds = std::clamp(seconds, 0.0f, m_details.duration_sec);
        m_playhead_sec = seconds;

        auto plugin = m_audio_manager.lock();
        if (plugin && m_voice_handle)
            plugin->seek(m_voice_handle, seconds);
    }


    void audio_viewer_window::transport_toggle() {

        if (m_playing)
            transport_pause();
        else
            transport_play();
    }

}
