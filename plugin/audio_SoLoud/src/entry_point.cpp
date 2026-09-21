
#include <event/event_bus.h>
#include <event/application_event.h>
#include <plugin_system/i_plugin.h>
#include <plugin_system/i_audio_plugin.h>
#include <plugin_system/i_asset_registry_plugin.h>



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

        plugin_manager::interface::virtual_file_system,
        plugin_manager::interface::asset_registry,
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

    class plugin final : public GLT::audio::i_audio_plugin {
    public:

        plugin();
        ~plugin() override;


        void on_load()   override;


        void on_unload() override;


        bool create()  override;


        void destroy() override;

        // ---- playback -------------------------------------------------------

        handle play(GLT::asset::handle sound, const GLT::asset::audio::source_config& config = {}) override;


        void stop(handle handle) override;


        void stop_all(bool include_paused = true) override;


        void pause(handle handle) override;


        void resume(handle handle) override;


        [[nodiscard]] GLT::asset::audio::state get_state(handle handle) const override;


        [[nodiscard]] bool is_valid(handle handle) const override;

        // ---- per-voice params -----------------------------------------------

        void set_volume(handle handle, f32 volume) override;


        void set_pan(handle handle, f32 pan) override;


        void set_play_speed(handle handle, f32 speed) override;


        void set_looping(handle handle, bool loop) override;

        // ---- transport ------------------------------------------------------

        void seek(handle handle, f32 seconds) override;


        [[nodiscard]] f32 get_playback_position(handle handle) const override;

        // ---- 3D -------------------------------------------------------------

        void set_3d_source_position(handle handle, const glm::vec3& position, const glm::vec3& velocity = {}) override;


        void set_3d_source_attenuation(handle handle, f32 min_distance, f32 max_distance, f32 rolloff_factor,
            GLT::asset::audio::attenuation_model model) override;


        void set_listener(const GLT::asset::audio::listener_config& config) override;


        void update_3d_audio() override;

        // ---- global ---------------------------------------------------------

        void set_global_volume(f32 volume) override;


        [[nodiscard]] f32 get_global_volume() const override;


        [[nodiscard]] const char* get_backend_name() const override;


        [[nodiscard]] u32 get_active_voice_count() const override;

    private:

        // One cached SoLoud source per asset. Rebuilt when info().hash changes.
        struct cached_sound {

            GLT::unique_ref<SoLoud::Wav>                            wav{};
            GLT::asset::content_hash                                hash{};
        };


        // Lazily build (or fetch) the SoLoud source for a given audio asset.
        // Returns nullptr if the handle is not loaded / not an audio asset.
        [[nodiscard]] SoLoud::Wav* wav_for(GLT::asset::handle sound);


        // Drop the cached source for a single asset (called on hot-reload).
        void invalidate(GLT::asset::handle sound);


        GLT::ref<GLT::asset::i_asset_registry_plugin>               m_registry{};
        GLT::unique_ref<SoLoud::Soloud>                             m_soloud{};
        std::unordered_map<GLT::asset::handle, cached_sound>        m_cache{};
        GLT::asset::audio::listener_config                          m_listener{};
    };

}

#include "plugin.inl"
#include "audio.inl"

EXPORT_PLUGIN_CLASS(GLT::audio::soloud_backend::plugin, GLT::audio::soloud_backend::descriptor)
