#include <plugin_system/i_plugin.h>
#include <plugin_system/i_audio_plugin.h>
#include <util/io/logger.h>
#include <event/event_bus.h>
#include <event/application_event.h>



// FORWARD DECLARATIONS ================================================================================================

namespace SoLoud {

    class Soloud;
    class Wav;
    class WavStream;
}

namespace GLT::audio::soloud_backend {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    static constexpr const char*                                dependencies_names[] = {

        nullptr
    };

    static constexpr plugin_manager::interface                  dependencies_interfaces[] = {

        plugin_manager::interface::window
    };

    static constexpr GLT::plugin_manager::plugin_descriptor     descriptor = {

        .name                                                   = GLT_MODULE_NAME,
        .load_phase                                             = GLT::plugin_manager::phase::pre_application,
        .unload_phase                                           = GLT::plugin_manager::phase::post_application_shutdown,
        .target                                                 = plugin_manager::interface::audio,
        .dependency_names_count                                 = ARRAY_SIZE(dependencies_names),
        .dependency_names                                       = dependencies_names,
        .dependency_interface_count                             = ARRAY_SIZE(dependencies_interfaces),
        .dependency_interfaces                                  = dependencies_interfaces,
    };

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================


    class audio : public GLT::audio::i_audio_plugin {
    public:

        audio();
        ~audio() override;



        void on_load() override;


        void on_unload() override;


        bool create() override;


        void destroy() override;


        handle load_sound(const std::string& name, const std::string& file_path, bool is_stream = false) override;


        void unload_sound(handle handle) override;


        [[nodiscard]] handle get_sound(const std::string& name) const override;


        handle play(handle sound, const audio_source_config& config = {}) override;


        void stop(handle handle) override;


        void stop_all(bool include_paused = true) override;


        void pause(handle handle) override;


        void resume(handle handle) override;


        [[nodiscard]] audio_state get_state(handle handle) const override;


        [[nodiscard]] bool is_valid(handle handle) const override;


        void set_volume(handle handle, f32 volume) override;


        void set_pan(handle handle, f32 pan) override;


        void set_play_speed(handle handle, f32 speed) override;


        void set_looping(handle handle, bool loop) override;


        void seek(handle handle, f32 seconds) override;


        [[nodiscard]] f32 get_playback_position(handle handle) const override;


        void set_3d_source_position(handle handle, const glm::vec3& position, const glm::vec3& velocity = {}) override;


        void set_3d_source_attenuation(handle handle, f32 min_distance, f32 max_distance, f32 rolloff_factor, attenuation_model model) override;


        void set_listener(const listener_config& config) override;


        void update_3d_audio() override;


        void set_global_volume(f32 volume) override;


        [[nodiscard]] f32 get_global_volume() const override;


        [[nodiscard]] const char* get_backend_name() const override;


        [[nodiscard]] u32 get_active_voice_count() const override;

    private:

        struct sound_entry {

            GLT::unique_ref<SoLoud::Wav>                        wav{};
            GLT::unique_ref<SoLoud::WavStream>                  stream{};
            bool                                                is_stream = false;
        };

        GLT::unique_ref<SoLoud::Soloud>                         m_soloud{};
        std::unordered_map<handle, sound_entry>                 m_sounds{};
        std::unordered_map<std::string, handle>                 m_sound_name_map{};
        handle                                                  m_next_sound_handle = 1;
        listener_config                                         m_listener{};
    };

}

#include "plugin.inl"
#include "audio.inl"

EXPORT_PLUGIN_CLASS(GLT::audio::soloud_backend::audio, GLT::audio::soloud_backend::descriptor)


/*

// In the engine's application class, during application_ready phase:
auto audio_plugin = GLT::audio::manager::get_ref();

if (audio_plugin) {
    audio_plugin->create();

    // Load a sound
    auto snd = audio_plugin->load_sound("explosion", "assets/audio/explosion.wav");

    // Play a 3D sound
    GLT::audio::audio_source_config cfg;
    cfg.is_3d = true;
    cfg.position = { 10.0f, 0.0f, 5.0f };
    cfg.min_distance = 2.0f;
    cfg.max_distance = 50.0f;
    auto voice = audio_plugin->play(snd, cfg);

    // In the update loop (each frame):
    GLT::audio::listener_config listener;
    listener.position = camera.get_position();
    listener.forward  = camera.get_forward();
    listener.up       = camera.get_up();
    audio_plugin->set_listener(listener);

    // Update 3D audio (recalculate panning/Doppler)
    audio_plugin->update_3d_audio();
}
    
*/
