
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

        FORCE_INLINE_R ref<GLT::audio::i_audio_plugin> get_ref() {

            return GLT::plugin_manager::get_plugin_ref<GLT::audio::i_audio_plugin>(plugin_manager::interface::audio);
        }

    }

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class i_audio_plugin : public GLT::plugin_manager::i_plugin {
    public:

        virtual ~i_audio_plugin() = default;

        virtual bool create()  = 0;

        virtual void destroy() = 0;

        // --- playback control ---------------------------------------------------

        // `sound` must be an `asset::handle` returned by the asset registry for
        // an asset whose type is `core_types::audio`. Loading / unloading /
        // caching is the registry's job - the audio plugin only plays.
        virtual handle play(GLT::asset::handle sound, const GLT::asset::audio::source_config& config = {}) = 0;

        virtual void stop(handle handle)  = 0;

        virtual void stop_all(bool include_paused = true) = 0;

        virtual void pause(handle handle) = 0;

        virtual void resume(handle handle) = 0;

        [[nodiscard]] virtual GLT::asset::audio::state get_state(handle handle) const = 0;

        [[nodiscard]] virtual bool is_valid(handle handle) const = 0;

        // --- per-voice params ---------------------------------------------------

        virtual void set_volume(handle handle, f32 volume) = 0;

        virtual void set_pan(handle handle, f32 pan) = 0;

        virtual void set_play_speed(handle handle, f32 speed) = 0;

        virtual void set_looping(handle handle, bool loop) = 0;

        // --- transport ----------------------------------------------------------

        virtual void seek(handle handle, f32 seconds) = 0;

        [[nodiscard]] virtual f32 get_playback_position(handle handle) const = 0;

        // --- 3D -----------------------------------------------------------------

        virtual void set_3d_source_position(handle handle, const glm::vec3& position, const glm::vec3& velocity = {}) = 0;

        virtual void set_3d_source_attenuation(handle handle, f32 min_distance, f32 max_distance, f32 rolloff_factor, 
            GLT::asset::audio::attenuation_model model) = 0;

        virtual void set_listener(const GLT::asset::audio::listener_config& config) = 0;

        virtual void update_3d_audio() = 0;

        // --- global -------------------------------------------------------------

        virtual void set_global_volume(f32 volume) = 0;

        [[nodiscard]] virtual f32 get_global_volume() const = 0;

        [[nodiscard]] virtual const char* get_backend_name() const = 0;

        [[nodiscard]] virtual u32 get_active_voice_count() const = 0;

    };

}
