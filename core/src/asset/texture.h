
#pragma once

#include "asset/type.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::texture {

    // CONSTANTS =======================================================================================================

    // Chunk IDs for the texture_asset format (any u32, stable once shipped)
    inline constexpr GLT::asset::chunk_id                   CHUNK_TEXTURE_FORMAT = 0x0200;   // texture::texture_format (required)

    inline constexpr GLT::asset::chunk_id                   CHUNK_PIXEL_DATA = 0x0201;   // raw pixel bytes (required)

    inline constexpr GLT::asset::chunk_id                   CHUNK_MIP_RANGES = 0x0202;   // texture::mip_range[] (optional)

    inline constexpr GLT::asset::chunk_id                   CHUNK_METADATA = 0x0203;   // utf-8 "key\0value\0" pairs (optional)

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // @brief On-disk pixel layout
    //
    // The factory normalises whatever the source codec decoded into one of these canonical forms so the runtime handler
    // never has to guess. Block-compressed formats are reserved for a future compressor pass
    enum class pixel_format : u16 {

        unknown = 0,

        // Uncompressed, 8-bit per channel
        u8_r,
        u8_rg,
        u8_rgb,
        u8_rgba,

        // Uncompressed, 16-bit per channel (normalized)
        u16_r,
        u16_rg,
        u16_rgb,
        u16_rgba,

        // Uncompressed, 32-bit float per channel (HDR)
        f32_r,
        f32_rg,
        f32_rgb,
        f32_rgba,

        // Block-compressed
        bc1,
        bc3,
        bc4,
        bc5,
        bc6h,
        bc7,
        etc1,
        etc2,
        astc_4x4,
        astc_6x6,
        astc_8x8,
    };


    // @brief Color space the samples are encoded in
    //
    // Renderers need this to pick the right sRGB -> linear conversion at sample time, and the factory uses it
    // to decide defaults per usage tag
    enum class color_space : u8 {

        linear = 0,
        srgb,
    };


    // @brief Topological kind of a texture
    //
    // The handler claims texture2d only for now, but the chunk format is forward-compatible with the other kinds
    enum class texture_kind : u8 {

        texture_2d = 0,
        texture_3d,
        cube_map,
        texture_2d_array,
        cube_map_array,
    };


    // @brief Semantic role a texture plays
    //
    // Drives sensible defaults (linear vs sRGB) and, once block compression lands, the codec choice
    // (BC5 for normals, BC4 for masks, BC7 for albedo/UI, BC6H for emission)
    enum class texture_usage : u8 {

        default_ = 0,                                       // "default": generic color data
        normal_map,                                         // tangent-space normal map - linear, BC5
        mask,                                               // single-channel data (roughness, AO, …) - linear, BC4
        ui,                                                 // UI / cursor sprites - sRGB, no minification
        albedo,                                             // explicit albedo (sRGB, BC7)
        emission,                                           // emissive - linear HDR, BC6H
    };


    // @brief Fixed descriptor of the pixel data that follows
    //
    // The handler trusts this blindly, so the factory is expected to have written a layout consistent with
    // CHUNK_PIXEL_DATA and (optionally) CHUNK_MIP_RANGES
    struct texture_format {

        u32                         width;                  // base level
        u32                         height;                 // base level (1 for 1D)
        u32                         depth;                  // 1 for 2D
        u16                         mip_levels;             // 1 = no mipmaps
        u16                         array_layers;           // 1 for non-array
        pixel_format                format;
        color_space                 space;
        texture_kind                kind;
        texture_usage               usage;
        u8                          compression_quality;    // 0..100, codec hint
        u8                          _pad[6]{};              // grown from _pad[5] to keep 28 bytes
    };
    static_assert(sizeof(texture_format) == 28);
    static_assert(std::is_trivially_copyable_v<texture_format>);


    // @brief One entry per mip level, in order
    //
    // [offset] is relative to the start of the concatenated pixel blob stored in CHUNK_PIXEL_DATA;
    // [size] is the byte count of that level
    struct mip_range {

        u64                         offset{};
        u64                         size{};
        u32                         width{};
        u32                         height{};
        u32                         depth{};
        u8                          _pad[4]{};
    };
    static_assert(sizeof(mip_range) == 32);
    static_assert(std::is_trivially_copyable_v<mip_range>);

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Runtime representation of a loaded texture asset
    //
    // Canonical form is whatever the factory wrote - the handler just copies it verbatim
    // [pixels] holds every mip level concatenated end-to-end; the per-level offsets and sizes live in [mips]
    //
    // IMPORTANT: this struct OWNS the decoded pixel data. The chunk_reader hands out spans into a buffer that dies when
    // the registry's load function returns, so we copy what we want to keep
    class texture_asset final : public GLT::asset::i_runtime_asset {
    public:

        GLT::asset::type                                asset_type{ GLT::asset::core_types::texture2d };
        GLT::asset::texture::texture_format             format{};
        std::vector<std::byte>                          pixels;         // concatenated mip levels
        std::vector<GLT::asset::texture::mip_range>     mips;           // one per mip level (empty = single level)


        // @brief Returns the asset-type tag used by the registry
        FORCE_INLINE_R GLT::asset::type type() const noexcept override { return asset_type; }


        // @brief Approximate resident bytes, used by the profiler
        FORCE_INLINE_R u64 memory_usage() const noexcept override {

            return sizeof(*this)
                + pixels.capacity()
                + mips.capacity() * sizeof(GLT::asset::texture::mip_range);
        }


        // Convenience for the renderer: base-level dimensions
        FORCE_INLINE_R u32 width()  const noexcept { return format.width; }
        FORCE_INLINE_R u32 height() const noexcept { return format.height; }
        FORCE_INLINE_R bool has_mips() const noexcept { return format.mip_levels > 1; }

    };


}
