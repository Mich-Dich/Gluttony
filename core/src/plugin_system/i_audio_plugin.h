
#pragma once

#include <glm/glm.hpp>

#include "audio/types.h"
#include "plugin_system/plugin_manager.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::audio {
    class i_audio_plugin;
}

namespace GLT::audio {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    namespace manager {

        FORCE_INLINE_R ref<GLT::audio::i_audio_plugin> get_ref() {

            return GLT::plugin_manager::get_plugin_ref<GLT::audio::i_audio_plugin>(plugin_manager::interface::audio);
        }

    }

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Core audio plugin interface.
    //
    // This interface abstracts the low‑level audio engine, sound source management, 3D spatialization, and listener updates. 
    // Concrete backends (SoLoud, FMOD, etc.) implement this interface so the engine core can drive audio without depending
    // on a specific audio library.
    //
    // Typical lifecycle:
    //   1. create()
    //   2. load_sound() / play() / set_listener() / update() per frame
    //   3. stop_all() / destroy()
    //
    // @see plugin_manager::i_plugin
    class i_audio_plugin : public GLT::plugin_manager::i_plugin {
    public:


        virtual ~i_audio_plugin() = default;

        // @brief Initialises the audio backend.
        //
        // Called after the plugin has been loaded and the engine core is ready.
        // The implementation should:
        // - Initialise the audio hardware / backend (e.g., SoLoud::init()).
        // - Set up any global audio state (e.g., global volume, default listener).
        // - Prepare internal maps for loaded sounds and active voices.
        //
        // @return true on success, false on failure.
        virtual bool create() = 0;


        // @brief Shuts down the audio backend and releases all resources.
        //
        // The implementation must stop all active voices, unload all sound sources,
        // and de‑initialise the audio hardware (e.g., SoLoud::deinit()).
        virtual void destroy() = 0;

        // --- sound management -------------------------------------------------------

        // @brief Loads a sound from a file and returns a handle for later playback.
        //
        // The implementation should support common formats (WAV, OGG, MP3, FLAC)
        // and cache the decoded data internally. The returned handle remains valid
        // until unload_sound() is called.
        //
        // @param name Unique name for the sound (used for retrieval).
        // @param file_path Path to the audio file.
        // @param is_stream True if the sound should be streamed (for large files),
        //                  false for fully‑decoded in‑memory playback.
        // @return Handle to the loaded sound, or INVALID_AUDIO_HANDLE on failure.
        virtual handle load_sound(const std::string& name, const std::string& file_path, bool is_stream = false) = 0;


        // @brief Unloads a previously loaded sound and frees its resources.
        virtual void unload_sound(handle handle) = 0;


        // @brief Retrieves a previously loaded sound handle by name.
        //        Returns INVALID_AUDIO_HANDLE if not found.
        [[nodiscard]] virtual handle get_sound(const std::string& name) const = 0;

        // --- playback control -------------------------------------------------------

        // @brief Plays a loaded sound with the given configuration.
        //
        // For 3D sounds, the position/velocity from the config are applied
        // immediately. The returned handle can be used to control the
        // playing instance (stop, pause, set volume, etc.).
        //
        // @param sound The sound to play.
        // @param config Playback configuration.
        // @return Handle to the playing voice, or INVALID_AUDIO_HANDLE on failure.
        virtual handle play(handle sound, const audio_source_config& config = {}) = 0;


        // @brief Stops a specific playing voice.
        virtual void stop(handle handle) = 0;


        // @brief Stops all playing voices (optionally including paused ones).
        virtual void stop_all(bool include_paused = true) = 0;


        // @brief Pauses a playing voice.
        virtual void pause(handle handle) = 0;


        // @brief Resumes a paused voice.
        virtual void resume(handle handle) = 0;


        // @brief Returns the current state of a voice.
        [[nodiscard]] virtual audio_state get_state(handle handle) const = 0;


        // @brief Checks if a voice is still valid (i.e., not stopped and not expired).
        [[nodiscard]] virtual bool is_valid(handle handle) const = 0;

        // --- per‑voice parameters ---------------------------------------------------

        // @brief Sets the volume of a playing voice (0.0 – 1.0, can exceed for gain).
        virtual void set_volume(handle handle, f32 volume) = 0;


        // @brief Sets the pan of a 2D voice (-1 = left, 1 = right).
        virtual void set_pan(handle handle, f32 pan) = 0;


        // @brief Sets the playback speed of a voice.
        virtual void set_play_speed(handle handle, f32 speed) = 0;


        // @brief Enables or disables looping for a voice.
        virtual void set_looping(handle handle, bool loop) = 0;

        // --- transport --------------------------------------------------------------

        // @brief Seeks a playing (or paused) voice to the given position in
        //        seconds, relative to the start of the source.
        virtual void seek(handle handle, f32 seconds) = 0;


        // @brief Returns the current playback position of a voice, in seconds.
        //        Returns 0.0 for invalid/stopped voices.
        [[nodiscard]] virtual f32 get_playback_position(handle handle) const = 0;

        // --- 3D spatialization ------------------------------------------------------

        // @brief Updates the position and velocity of a 3D voice.
        virtual void set_3d_source_position(handle handle, const glm::vec3& position, const glm::vec3& velocity = {}) = 0;


        // @brief Sets the attenuation parameters for a 3D voice.
        virtual void set_3d_source_attenuation(handle handle, f32 min_distance, f32 max_distance, f32 rolloff_factor, attenuation_model model) = 0;


        // @brief Updates the global listener (camera) configuration.
        //
        // Should be called every frame (or whenever the camera moves) so that
        // SoLoud can recalculate panning and Doppler for all 3D voices.
        virtual void set_listener(const listener_config& config) = 0;


        // @brief Triggers a re‑calculation of 3D panning and Doppler.
        //
        // Must be called after updating listener and/or 3D source positions.
        // Typically called once per frame from the engine's update loop.
        virtual void update_3d_audio() = 0;

        // --- global controls --------------------------------------------------------

        // @brief Sets the global master volume (0.0 – 1.0).
        virtual void set_global_volume(f32 volume) = 0;


        // @brief Returns the current global master volume.
        [[nodiscard]] virtual f32 get_global_volume() const = 0;


        // @brief Returns the backend API identifier for debugging / feature queries.
        [[nodiscard]] virtual const char* get_backend_name() const = 0;


        // @brief Returns the number of currently active (playing) voices.
        [[nodiscard]] virtual uint32_t get_active_voice_count() const = 0;

    };

}
