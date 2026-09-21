#pragma once

#include <soloud.h>
#include <soloud_wav.h>



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

    bool plugin::create() {

        m_soloud = GLT::create_unique_ref<SoLoud::Soloud>();
        VALIDATE(m_soloud->init() == SoLoud::SO_NO_ERROR, m_soloud.reset(); return false,
            "SoLoud audio backend initialized", "SoLoud initialization failed");

        return true;
    }


    void plugin::destroy() {

        if (!m_soloud)
            return;

        m_soloud->stopAll();
        m_cache.clear();          // destroys Wav objects before Soloud::deinit
        m_soloud->deinit();
        m_soloud.reset();
    }

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // ---- playback --------------------------------------------------------------------------------------------------

    handle plugin::play(GLT::asset::handle sound, const GLT::asset::audio::source_config& config) {

        using namespace GLT::asset::audio;

        if (!m_soloud)
            return INVALID_HANDLE;

        SoLoud::Wav* wav = wav_for(sound);
        if (!wav)
            return INVALID_HANDLE;

        SoLoud::handle voice_handle = 0;

        if (config.is_3d) {

            voice_handle = m_soloud->play3d(*wav,
                config.position.x, config.position.y, config.position.z,
                config.volume, config.pan, config.play_speed);

            m_soloud->set3dSourceParameters(voice_handle,
                config.position.x, config.position.y, config.position.z,
                config.velocity.x, config.velocity.y, config.velocity.z);

            m_soloud->set3dSourceMinMaxDistance(voice_handle,
                config.min_distance, config.max_distance);

            m_soloud->set3dSourceAttenuation(voice_handle,
                static_cast<unsigned int>(config.attenuation),
                config.rolloff_factor);

        } else {

            voice_handle = m_soloud->play(*wav,
                config.volume, config.pan, config.play_speed);
        }

        if (config.loop)
            m_soloud->setLooping(voice_handle, true);

        return static_cast<handle>(voice_handle);
    }


    void plugin::stop(handle handle) {

        if (m_soloud && handle != INVALID_HANDLE)
            m_soloud->stop(handle);
    }


    void plugin::stop_all(bool /*include_paused*/) {

        if (m_soloud)
            m_soloud->stopAll();
    }


    void plugin::pause(handle handle) {

        if (m_soloud && handle != INVALID_HANDLE)
            m_soloud->setPause(handle, true);
    }


    void plugin::resume(handle handle) {

        if (m_soloud && handle != INVALID_HANDLE)
            m_soloud->setPause(handle, false);
    }


    GLT::asset::audio::state plugin::get_state(handle handle) const {

        if (!m_soloud || handle == INVALID_HANDLE)
            return GLT::asset::audio::state::stopped;

        if (m_soloud->isValidVoiceHandle(handle))
            return m_soloud->getPause(handle) ? GLT::asset::audio::state::paused : GLT::asset::audio::state::playing;

        return GLT::asset::audio::state::stopped;
    }


    bool plugin::is_valid(handle handle) const {

        return m_soloud && m_soloud->isValidVoiceHandle(handle);
    }

    // ---- per-voice params ------------------------------------------------------------------------------------------

    void plugin::set_volume(handle handle, f32 volume) {

        if (m_soloud && handle != INVALID_HANDLE)
            m_soloud->setVolume(handle, volume);
    }


    void plugin::set_pan(handle handle, f32 pan) {

        if (m_soloud && handle != INVALID_HANDLE)
            m_soloud->setPan(handle, pan);
    }


    void plugin::set_play_speed(handle handle, f32 speed) {

        if (m_soloud && handle != INVALID_HANDLE)
            m_soloud->setRelativePlaySpeed(handle, speed);
    }


    void plugin::set_looping(handle handle, bool loop) {

        if (m_soloud && handle != INVALID_HANDLE)
            m_soloud->setLooping(handle, loop);
    }

    // ---- transport -------------------------------------------------------------------------------------------------

    void plugin::seek(handle handle, f32 seconds) {

        if (m_soloud && handle != INVALID_HANDLE)
            m_soloud->seek(handle, seconds);
    }


    f32 plugin::get_playback_position(handle handle) const {

        if (!m_soloud || handle == INVALID_HANDLE)
            return 0.0f;

        return m_soloud->getStreamPosition(handle);
    }

    // ---- 3D --------------------------------------------------------------------------------------------------------

    void plugin::set_3d_source_position(handle handle, const glm::vec3& position, const glm::vec3& velocity) {

        if (m_soloud && handle != INVALID_HANDLE)
            m_soloud->set3dSourceParameters(handle, position.x, position.y, position.z, velocity.x, velocity.y, velocity.z);
    }


    void plugin::set_3d_source_attenuation(handle handle, f32 min_distance, f32 max_distance, f32 rolloff_factor, 
        GLT::asset::audio::attenuation_model model) {

        if (m_soloud && handle != INVALID_HANDLE) {
            m_soloud->set3dSourceMinMaxDistance(handle, min_distance, max_distance);
            m_soloud->set3dSourceAttenuation(handle, static_cast<unsigned int>(model), rolloff_factor);
        }
    }


    void plugin::set_listener(const GLT::asset::audio::listener_config& config) {

        m_listener = config;
        if (m_soloud) {
            m_soloud->set3dListenerPosition(config.position.x, config.position.y, config.position.z);
            m_soloud->set3dListenerAt(config.forward.x, config.forward.y, config.forward.z);
            m_soloud->set3dListenerUp(config.up.x, config.up.y, config.up.z);
            m_soloud->set3dListenerVelocity(config.velocity.x, config.velocity.y, config.velocity.z);
        }
    }


    void plugin::update_3d_audio() {

        if (m_soloud)
            m_soloud->update3dAudio();
    }

    // ---- global ----------------------------------------------------------------------------------------------------

    void plugin::set_global_volume(f32 volume) {

        if (m_soloud)
            m_soloud->setGlobalVolume(volume);
    }


    f32 plugin::get_global_volume() const { return m_soloud ? m_soloud->getGlobalVolume() : 1.0f; }


    const char* plugin::get_backend_name() const { return "SoLoud"; }


    u32 plugin::get_active_voice_count() const { return m_soloud ? m_soloud->getActiveVoiceCount() : 0; }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    SoLoud::Wav* plugin::wav_for(GLT::asset::handle sound) {

        if (!m_registry || sound == INVALID_HANDLE)
            return nullptr;

        const auto& info = m_registry->info(sound);

        // ---- cache lookup ---------------------------------------------------
        if (auto it = m_cache.find(sound); it != m_cache.end()) {
            if (it->second.hash == info.hash)
                return it->second.wav.get();
            // stale (hot-reload) - fall through and rebuild
        }

        // ---- fetch the runtime asset ---------------------------------------
        auto* runtime = m_registry->data(sound);
        auto* asset = dynamic_cast<GLT::asset::audio::audio_asset*>(runtime);
        if (!asset)
            return nullptr;

        using namespace GLT::asset::audio;

        if (asset->format.kind != sample_kind::f32) {
            LOG(warn, "[audio] asset '{}' is not f32 - unsupported", info.name);
            return nullptr;
        }

        const u32 channels = asset->format.channels;
        const u64 frames = asset->format.frame_count;

        if (channels != 1 && channels != 2) {
            LOG(warn, "[audio] asset '{}' has {} channels - only mono/stereo supported", info.name, channels);
            return nullptr;
        }

        if (frames == 0 || asset->samples.size() != frames * channels) {
            LOG(warn, "[audio] asset '{}' sample count mismatch ({} vs {} * {})", info.name, asset->samples.size(), frames, channels);
            return nullptr;
        }

        // ---- deinterleave ---------------------------------------------------
        //
        // SoLoud::Wav stores audio PLANAR: mData[ch * frame_count + frame_idx]. WavInstance::getAudio reads it the same way. 
        // Our asset is INTERLEAVED (frame-major), so we build a planar buffer for SoLoud.
        //
        // We pass aCopy = false, aTakeOwnership = true, so SoLoud::Wav::loadRawWave takes ownership and will delete[] this buffer in ~Wav().
        const u64 total_floats = frames * channels;
        auto* planar = new float[total_floats];

        if (channels == 1) {

            std::memcpy(planar, asset->samples.data(), total_floats * sizeof(float));

        } else {

            // ch == 2
            const float* src = asset->samples.data();
            float* l = planar;
            float* r = planar + frames;
            for (u64 f = 0; f < frames; ++f) {
                l[f] = src[f * 2 + 0];
                r[f] = src[f * 2 + 1];
            }
        }

        auto wav = GLT::create_unique_ref<SoLoud::Wav>();

        const auto res = wav->loadRawWave(
            planar,
            static_cast<unsigned int>(total_floats),
            static_cast<float>(asset->format.sample_rate),
            static_cast<unsigned int>(channels),
            false,                                  // aCopy
            true);                                  // aTakeOwnership

        if (res != SoLoud::SO_NO_ERROR) {
            delete[] planar;                        // loadRawWave rejected it before taking ownership
            LOG(error, "[audio] SoLoud rejected raw wave for '{}' (err {})", info.name, static_cast<int>(res));
            return nullptr;
        }

        // ---- apply loop points if the asset carries them -------------------
        // SoLoud's setLoopPoint takes a sample index in the source's own planar layout, i.e. it's already per-channel, so no * channels here.
        if (asset->loop.has_loop)
            wav->setLoopPoint(static_cast<double>(asset->loop.loop_start_frame));

        auto [it, _] = m_cache.insert_or_assign(sound, cached_sound{ std::move(wav), info.hash });
        return it->second.wav.get();
    }


    void plugin::invalidate(GLT::asset::handle sound) { m_cache.erase(sound); }

}
