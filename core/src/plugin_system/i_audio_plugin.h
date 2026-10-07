
#pragma once

#include <glm/glm.hpp>

#include "asset/audio.h"
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

        // @brief Fetches the process-wide audio plugin
        // @return Strong reference to the audio plugin, or an empty ref if it isn't loaded
        FORCE_INLINE_R ref<GLT::audio::i_audio_plugin> get_ref() {

            return GLT::plugin_manager::get_plugin_ref<GLT::audio::i_audio_plugin>(plugin_manager::interface::audio);
        }

    }

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Platform plugin: owns the audio backend and every live playback voice
    //
    // The audio plugin plays assets produced by the asset registry. Loading, unloading, and caching are the registry's job
    // this interface only starts, stops, and parameterize voices
    //
    // Handles passed to playback-control methods are voice handles returned by [play()]; they are distinct from asset handles
    // Use [is_valid()] to test whether a voice handle is still live before calling into it
    class i_audio_plugin : public GLT::plugin_manager::i_plugin {
    public:

        virtual ~i_audio_plugin() = default;


        // @brief Initializes the audio backend
        // @return true on success
        virtual bool create()  = 0;


        // @brief Shuts the audio backend down and releases every voice
        virtual void destroy() = 0;

        // --- playback control ---------------------------------------------------

        // @brief Starts playback of an audio asset and returns a voice handle
        //
        // [sound] must be an [asset::handle] returned by the asset registry for an asset whose type is [core_types::audio]
        // Loading / unloading / caching is the registry's job - the audio plugin only plays
        //
        // @param sound   Asset handle of the audio asset to play
        // @param config  Initial per-voice playback configuration (volume, 3D position, …)
        // @return A voice handle for controlling this playback, or an invalid handle on failure
        virtual handle play(GLT::asset::handle sound, const GLT::asset::audio::source_config& config = {}) = 0;


        // @brief Stops a single voice
        // @param handle  Voice handle returned by [play()]
        virtual void stop(handle handle)  = 0;


        // @brief Stops every active voice
        // @param include_paused  When true, also stops paused voices
        virtual void stop_all(bool include_paused = true) = 0;


        // @brief Pauses a single voice
        // @param handle  Voice handle returned by [play()]
        virtual void pause(handle handle) = 0;


        // @brief Resumes a previously-paused voice
        // @param handle  Voice handle returned by [play()]
        virtual void resume(handle handle) = 0;


        // @brief Returns the playback state of a voice
        // @param handle  Voice handle returned by [play()]
        [[nodiscard]] virtual GLT::asset::audio::state get_state(handle handle) const = 0;


        // @brief Reports whether a voice handle refers to a live voice
        // @param handle  Voice handle returned by [play()]
        // @return true if the handle is still valid
        [[nodiscard]] virtual bool is_valid(handle handle) const = 0;

        // --- per-voice params ---------------------------------------------------

        // @brief Sets the linear volume of a voice
        // @param handle  Voice handle returned by [play()]
        // @param volume  Linear volume in [0, 1] (implementations may allow > 1)
        virtual void set_volume(handle handle, f32 volume) = 0;


        // @brief Sets the stereo pan of a non-3D voice
        // @param handle  Voice handle returned by [play()]
        // @param pan     -1 = left, 0 = center, +1 = right
        virtual void set_pan(handle handle, f32 pan) = 0;


        // @brief Sets the playback speed of a voice
        // @param handle  Voice handle returned by [play()]
        // @param speed   Playback speed multiplier (1.0 = native pitch/rate)
        virtual void set_play_speed(handle handle, f32 speed) = 0;


        // @brief Enables or disables looping on a voice
        // @param handle  Voice handle returned by [play()]
        // @param loop    true to loop
        virtual void set_looping(handle handle, bool loop) = 0;

        // --- transport ----------------------------------------------------------

        // @brief Seeks a voice to a time offset
        // @param handle   Voice handle returned by [play()]
        // @param seconds  Offset from the start of the sample, in seconds
        virtual void seek(handle handle, f32 seconds) = 0;


        // @brief Returns the current playback position of a voice
        // @param handle  Voice handle returned by [play()]
        // @return Offset from the start of the sample, in seconds
        [[nodiscard]] virtual f32 get_playback_position(handle handle) const = 0;

        // --- 3D -----------------------------------------------------------------

        // @brief Updates a voice's 3D source position and velocity
        // @param handle    Voice handle returned by [play()]
        // @param position  World-space source position
        // @param velocity  World-space source velocity, for Doppler
        virtual void set_3d_source_position(handle handle, const glm::vec3& position, const glm::vec3& velocity = {}) = 0;


        // @brief Updates a voice's 3D attenuation parameters
        // @param handle          Voice handle returned by [play()]
        // @param min_distance    Distance at which attenuation begins
        // @param max_distance    Distance at which attenuation clamps to its floor
        // @param rolloff_factor  Curve steepness
        // @param model           Attenuation curve family
        virtual void set_3d_source_attenuation(handle handle, f32 min_distance, f32 max_distance, f32 rolloff_factor, 
            GLT::asset::audio::attenuation_model model) = 0;


        // @brief Updates the global listener transform
        // @param config  Listener position, orientation, and velocity
        virtual void set_listener(const GLT::asset::audio::listener_config& config) = 0;


        // @brief Recomputes 3D mixing for every active voice
        //
        // Call once per frame after any source or listener changes; the backend is expected to be cheap when nothing has moved
        virtual void update_3d_audio() = 0;

        // --- global -------------------------------------------------------------

        // @brief Sets the master output volume
        // @param volume  Linear master volume
        virtual void set_global_volume(f32 volume) = 0;


        // @brief Returns the master output volume
        [[nodiscard]] virtual f32 get_global_volume() const = 0;


        // @brief Returns a human-readable name for the active backend (e.g. "miniaudio")
        [[nodiscard]] virtual const char* get_backend_name() const = 0;


        // @brief Returns the number of currently active (non-stopped) voices
        [[nodiscard]] virtual u32 get_active_voice_count() const = 0;

    };

}
