#pragma once

#include <plugin_system/i_asset_registry_plugin.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::handler::audio {

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

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    FORCE_INLINE void plugin::on_load() {

        m_registry = GLT::asset::registry::get_ref();
        VALIDATE(m_registry, return, "", "asset_registry not available - handler inactive")
        m_registry->register_handler(this);
        LOG_LOADED
    }


    FORCE_INLINE void plugin::on_unload() {

        if (m_registry)
            m_registry->unregister_handler(this);

        m_registry.reset();
        LOG_UNLOADED
    }


    FORCE_INLINE_R std::span<const GLT::asset::type> plugin::types() const noexcept {

        // Single type for now. If you later split streamed vs. fully-resident
        // audio (music vs. sfx), claim extra types here and branch in
        // deserialize() on info.asset_type.
        static constexpr GLT::asset::type t[] = { GLT::asset::core_types::audio, };
        return t;
    }


    FORCE_INLINE_R std::expected<std::unique_ptr<GLT::asset::i_runtime_asset>, GLT::asset::load_error> plugin::deserialize(
        const GLT::asset::info& info, GLT::asset::chunk_reader& reader) {


        // ---- required chunks ---------------------------------------------------

        const auto fmt_span = reader.get_as<GLT::asset::audio::format>(GLT::asset::audio::CHUNK_FORMAT);
        VALIDATE(fmt_span.size() == 1, return std::unexpected{ GLT::asset::load_error::corrupt_header }, "", 
            "[{}] missing or malformed format chunk (got {})", info.name, fmt_span.size())

        const auto pcm = reader.get_as<f32>(GLT::asset::audio::CHUNK_PCM_DATA);
        VALIDATE(!pcm.empty(), return std::unexpected{ GLT::asset::load_error::corrupt_header }, "",
            "[{}] missing pcm chunk", info.name)

        // Sanity: format says how many frames should be here.
        const GLT::asset::audio::format fmt = fmt_span[0];
        VALIDATE(fmt.channels > 0, return std::unexpected{ GLT::asset::load_error::corrupt_header }, "",
            "[{}] format chunk claims 0 channels", info.name)

        const u64 expected_samples = fmt.frame_count * fmt.channels;
        VALIDATE(expected_samples == pcm.size(), return std::unexpected{ GLT::asset::load_error::corrupt_header }, "",
            "[{}] pcm size mismatch: format says {} samples, chunk has {}",
            info.name, expected_samples, pcm.size())

        // ---- build the runtime asset ------------------------------------------

        auto asset = std::make_unique<GLT::asset::audio::audio_asset>();
        asset->asset_type = info.asset_type;
        asset->format = fmt;
        asset->samples.assign(pcm.begin(), pcm.end());      // Copy - the chunk_reader's backing buffer dies when the registry's load function returns. Do not keep the span.

        // ---- optional loop chunk ----------------------------------------------

        if (const auto lp = reader.get_as<GLT::asset::audio::loop_points>(GLT::asset::audio::CHUNK_LOOP_POINTS); lp.size() == 1) {

            asset->loop = lp[0];

        } else {

            asset->loop.loop_start_frame = 0;
            asset->loop.loop_end_frame = fmt.frame_count;
            asset->loop.has_loop = 0;
        }

        LOG(info, "loaded [{}] - {} frames, {} ch, {} Hz",
            info.name, asset->format.frame_count, asset->format.channels, asset->format.sample_rate);

        return std::unique_ptr<GLT::asset::i_runtime_asset>(std::move(asset));
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
