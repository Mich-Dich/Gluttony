#pragma once

#include <plugin_system/i_asset_registry_plugin.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::handler::texture {

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

        // Only texture2D for now. Add texture3D / cube_map here when the
        // factory grows support and the chunk layout can express them.
        static constexpr GLT::asset::type t[] = {
            GLT::asset::core_types::texture2D,
        };
        return t;
    }


    FORCE_INLINE_R std::expected<GLT::unique_ref<GLT::asset::i_runtime_asset>, GLT::asset::load_error> plugin::deserialize(
        const GLT::asset::info& info, GLT::asset::chunk_reader& reader) {

        // required chunks ---------------------------------------------------------------------------------------------

        const auto fmt_span = reader.get_as<GLT::asset::texture::texture_format>(GLT::asset::texture::CHUNK_TEXTURE_FORMAT);
        VALIDATE(fmt_span.size() == 1, return std::unexpected{ GLT::asset::load_error::corrupt_header }, "",
            "[{}] missing or malformed texture_format chunk (got {})", info.name, fmt_span.size())

        const auto pixel_bytes = reader.get(GLT::asset::texture::CHUNK_PIXEL_DATA);
        VALIDATE(!pixel_bytes.empty(), return std::unexpected{ GLT::asset::load_error::corrupt_header }, "",
            "[{}] missing pixel data chunk", info.name)

        const GLT::asset::texture::texture_format fmt = fmt_span[0];

        // Sanity checks -----------------------------------------------------------------------------------------------

        VALIDATE(fmt.width > 0 && fmt.height > 0, return std::unexpected{ GLT::asset::load_error::corrupt_header }, "",
            "[{}] texture_format claims {}x{}", info.name, fmt.width, fmt.height)

        VALIDATE(fmt.format != GLT::asset::texture::pixel_format::unknown, return std::unexpected{ GLT::asset::load_error::corrupt_header }, "",
            "[{}] texture_format has unknown pixel format", info.name)

        const u64 expected_base = [&]() -> u64 {                            // Compute expected base-level byte size.

            u64 bytes_per_pixel = 0;
            switch (fmt.format) {
                case GLT::asset::texture::pixel_format::u8_r:           bytes_per_pixel = 1; break;
                case GLT::asset::texture::pixel_format::u8_rg:          bytes_per_pixel = 2; break;
                case GLT::asset::texture::pixel_format::u8_rgb:         bytes_per_pixel = 3; break;
                case GLT::asset::texture::pixel_format::u8_rgba:        bytes_per_pixel = 4; break;
                case GLT::asset::texture::pixel_format::u16_r:          bytes_per_pixel = 2; break;
                case GLT::asset::texture::pixel_format::u16_rg:         bytes_per_pixel = 4; break;
                case GLT::asset::texture::pixel_format::u16_rgb:        bytes_per_pixel = 6; break;
                case GLT::asset::texture::pixel_format::u16_rgba:       bytes_per_pixel = 8; break;
                case GLT::asset::texture::pixel_format::f32_r:          bytes_per_pixel = 4; break;
                case GLT::asset::texture::pixel_format::f32_rg:         bytes_per_pixel = 8; break;
                case GLT::asset::texture::pixel_format::f32_rgb:        bytes_per_pixel = 12; break;
                case GLT::asset::texture::pixel_format::f32_rgba:       bytes_per_pixel = 16; break;
                default: return 0;                                          // compressed - we don't handle yet
            }
            return static_cast<u64>(fmt.width) * fmt.height * fmt.depth * bytes_per_pixel;
        }();

        VALIDATE(expected_base > 0, return std::unexpected{ GLT::asset::load_error::corrupt_header }, "",
            "[{}] unsupported pixel format {} in texture_format", info.name, static_cast<int>(fmt.format))

        if (fmt.mip_levels <= 1) {                                          // If there's no mip chunk, the pixel chunk must be exactly base level.

            VALIDATE(pixel_bytes.size() == expected_base, return std::unexpected{ GLT::asset::load_error::corrupt_header }, "",
                "[{}] pixel data size mismatch: expected {}, got {}", info.name, expected_base, pixel_bytes.size())
        }

        // build the runtime asset -------------------------------------------------------------------------------------

        auto asset = std::make_unique<GLT::asset::texture::texture_asset>();
        asset->asset_type = info.asset_type;
        asset->format = fmt;
        asset->pixels.assign(pixel_bytes.begin(), pixel_bytes.end());       // Copy - the chunk_reader's backing buffer dies.

        // optional mip ranges -----------------------------------------------------------------------------------------

        const auto mips = reader.get_as<GLT::asset::texture::mip_range>(GLT::asset::texture::CHUNK_MIP_RANGES);
        if (!mips.empty()) {

            asset->mips.assign(mips.begin(), mips.end());
            if (asset->mips.size() != fmt.mip_levels) {                     // Sanity: mip count must match format.

                LOG(warn, "[{}] mip_range count {} != format.mip_levels {} - clamping", info.name, asset->mips.size(), fmt.mip_levels);
                asset->format.mip_levels = static_cast<u16>(asset->mips.size());
            }
        }

        LOG(info, "loaded [{}] - {}x{} ({}), {} mip(s), {} bytes", info.name, asset->format.width, asset->format.height, 
            static_cast<int>(asset->format.format), asset->format.mip_levels, asset->pixels.size());

        return GLT::unique_ref<GLT::asset::i_runtime_asset>(std::move(asset));
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
