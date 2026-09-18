#pragma once

#include <event/event_bus.h>

#include <soloud.h>
#include <soloud_wav.h>
#include <soloud_wavstream.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::audio::soloud_backend {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    bool audio::create() {

        m_soloud = GLT::create_unique_ref<SoLoud::Soloud>();
        VALIDATE(m_soloud->init() == SoLoud::SO_NO_ERROR, m_soloud.reset(); return false, 
            "SoLoud audio backend initialized", "SoLoud initialization failed");
        return true;
    }


    void audio::destroy() {

        if (m_soloud) {
            m_soloud->deinit();
            m_soloud.reset();
        }
        m_sounds.clear();
        m_sound_name_map.clear();
    }

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    handle audio::load_sound(const std::string& name, const std::string& file_path, bool is_stream) {

        if (!m_soloud)
            return INVALID_HANDLE;

        handle handle = m_next_sound_handle++;
        sound_entry entry;

        if (is_stream) {

            auto stream = GLT::create_unique_ref<SoLoud::WavStream>();
            VALIDATE(stream->load(file_path.c_str()) == SoLoud::SO_NO_ERROR, return INVALID_HANDLE, 
                "", "Failed to load streamed sound: {}", file_path);
            entry.stream = std::move(stream);
            entry.is_stream = true;

        } else {

            auto wav = GLT::create_unique_ref<SoLoud::Wav>();
            VALIDATE(wav->load(file_path.c_str()) == SoLoud::SO_NO_ERROR, return INVALID_HANDLE, 
                "", "Failed to load sound: {}", file_path);
            entry.wav = std::move(wav);
            entry.is_stream = false;

        }

        m_sounds[handle] = std::move(entry);
        m_sound_name_map[name] = handle;
        return handle;
    }


    void audio::unload_sound(handle handle) {

        auto it = m_sounds.find(handle);
        if (it != m_sounds.end()) {
            // Remove from name map
            for (auto name_it = m_sound_name_map.begin(); name_it != m_sound_name_map.end(); ) {
                if (name_it->second == handle)
                    name_it = m_sound_name_map.erase(name_it);
                else
                    ++name_it;
            }
            m_sounds.erase(it);
        }
    }


    handle audio::get_sound(const std::string& name) const {

        auto it = m_sound_name_map.find(name);
        return (it != m_sound_name_map.end()) ? it->second : INVALID_HANDLE;
    }

    // --- playback control -------------------------------------------------------------

    handle audio::play(handle sound, const audio_source_config& config) {

        if (!m_soloud) 
            return INVALID_HANDLE;

        auto it = m_sounds.find(sound);
        if (it == m_sounds.end()) return INVALID_HANDLE;

        SoLoud::AudioSource* source = it->second.is_stream
            ? static_cast<SoLoud::AudioSource*>(it->second.stream.get())
            : static_cast<SoLoud::AudioSource*>(it->second.wav.get());

        SoLoud::handle voice_handle = 0;
        if (config.is_3d) {
            voice_handle = m_soloud->play3d(*source,
                config.position.x,
                config.position.y,
                config.position.z,
                config.volume,
                config.pan,
                config.play_speed);
            // Apply 3D parameters
            m_soloud->set3dSourceParameters(voice_handle,
                config.position.x, config.position.y, config.position.z,
                config.velocity.x, config.velocity.y, config.velocity.z);
            m_soloud->set3dSourceMinMaxDistance(voice_handle,
                config.min_distance,
                config.max_distance);
            m_soloud->set3dSourceAttenuation(voice_handle,
                static_cast<unsigned int>(config.attenuation),
                config.rolloff_factor);
        
        } else {

            voice_handle = m_soloud->play(*source,
                config.volume,
                config.pan,
                config.play_speed);
        }

        if (config.loop)
            m_soloud->setLooping(voice_handle, true);

        return static_cast<handle>(voice_handle);
    }


    void audio::stop(handle handle) {

        if (m_soloud && handle != INVALID_HANDLE)
            m_soloud->stop(handle);
    }


    void audio::stop_all(bool /*include_paused*/) {

        if (m_soloud)
            m_soloud->stopAll();
    }


    void audio::pause(handle handle) {

        if (m_soloud && handle != INVALID_HANDLE)
            m_soloud->setPause(handle, true);
    }


    void audio::resume(handle handle) {

        if (m_soloud && handle != INVALID_HANDLE)
            m_soloud->setPause(handle, false);
    }


    audio_state audio::get_state(handle handle) const {

        if (!m_soloud || handle == INVALID_HANDLE)
            return audio_state::stopped;

        if (m_soloud->isValidVoiceHandle(handle)) {
            return m_soloud->getPause(handle) ? audio_state::paused : audio_state::playing;
        }
        return audio_state::stopped;
    }


    bool audio::is_valid(handle handle) const { return m_soloud && m_soloud->isValidVoiceHandle(handle); }

    // --- per‑voice parameters --------------------------------------------------------

    void audio::set_volume(handle handle, f32 volume) {

        if (m_soloud && handle != INVALID_HANDLE)
            m_soloud->setVolume(handle, volume);
    }


    void audio::set_pan(handle handle, f32 pan) {

        if (m_soloud && handle != INVALID_HANDLE)
            m_soloud->setPan(handle, pan);
    }


    void audio::set_play_speed(handle handle, f32 speed) {

        if (m_soloud && handle != INVALID_HANDLE)
            m_soloud->setRelativePlaySpeed(handle, speed);
    }


    void audio::set_looping(handle handle, bool loop) {

        if (m_soloud && handle != INVALID_HANDLE)
            m_soloud->setLooping(handle, loop);
    }

    // --- transport --------------------------------------------------------------

    void audio::seek(handle handle, f32 seconds) {

        if (m_soloud && handle != INVALID_HANDLE)
            m_soloud->seek(handle, seconds);
    }


    f32 audio::get_playback_position(handle handle) const {

        if (!m_soloud || handle == INVALID_HANDLE)
            return 0.0f;
        return m_soloud->getStreamPosition(handle);
    }

    // --- 3D specialization ------------------------------------------------------------

    void audio::set_3d_source_position(handle handle, const glm::vec3& position, const glm::vec3& velocity) {

        if (m_soloud && handle != INVALID_HANDLE) {
            m_soloud->set3dSourceParameters(handle,
                position.x, position.y, position.z,
                velocity.x, velocity.y, velocity.z);
        }
    }

    
    void audio::set_3d_source_attenuation(handle handle, f32 min_distance, f32 max_distance, f32 rolloff_factor, attenuation_model model) {

        if (m_soloud && handle != INVALID_HANDLE) {
            m_soloud->set3dSourceMinMaxDistance(handle, min_distance, max_distance);
            m_soloud->set3dSourceAttenuation(handle,
                static_cast<unsigned int>(model),
                rolloff_factor);
        }
    }


    void audio::set_listener(const listener_config& config) {

        m_listener = config;
        if (m_soloud) {
            m_soloud->set3dListenerPosition(config.position.x,
                config.position.y,
                config.position.z);
            m_soloud->set3dListenerAt(config.forward.x,
                config.forward.y,
                config.forward.z);
            m_soloud->set3dListenerUp(config.up.x,
                config.up.y,
                config.up.z);
            m_soloud->set3dListenerVelocity(config.velocity.x,
                config.velocity.y,
                config.velocity.z);
        }
    }

    
    void audio::update_3d_audio() {

        if (m_soloud)
            m_soloud->update3dAudio();
    }

    // --- global controls --------------------------------------------------------------

    void audio::set_global_volume(f32 volume) {

        if (m_soloud)
            m_soloud->setGlobalVolume(volume);
    }


    f32 audio::get_global_volume() const { return m_soloud ? m_soloud->getGlobalVolume() : 1.0f; }


    const char* audio::get_backend_name() const { return "SoLoud"; }


    u32 audio::get_active_voice_count() const { return m_soloud ? m_soloud->getActiveVoiceCount() : 0; }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
