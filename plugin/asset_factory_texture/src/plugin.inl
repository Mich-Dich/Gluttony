#pragma once

#if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-but-set-variable"
    #pragma GCC diagnostic ignored "-Wunused-function"
    #pragma GCC diagnostic ignored "-Wsign-compare"
#endif

// STB image is a single-header library. We only need it in this TU.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG

// NOTE: do NOT define STBI_NO_LINEAR                   need stbi_loadf_from_memory() for HDR.
// NOTE: do NOT define STBI_NO_HDR                      want Radiance .hdr support.
// NOTE: do NOT define STBI_NO_PIC / STBI_NO_PNM        the factory binds .pic/.pnm.
// NOTE: STB does NOT support EXR. Drop that extension from bindings/wants_hdr().

#include <stb_image.h>

#if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic pop
#endif

#include <asset/texture.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::factory::texture_stb {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct decoded_image {

        std::vector<std::byte>                      pixels{};
        u32                                         width{};
        u32                                         height{};
        GLT::asset::texture::pixel_format           format{ GLT::asset::texture::pixel_format::unknown };
        GLT::asset::texture::color_space            space{ GLT::asset::texture::color_space::srgb };
        bool                                        is_hdr{ false };

        [[nodiscard]] u32 bytes_per_pixel() const noexcept {

            return static_cast<u32>(pixels.size() / (static_cast<size_t>(width) * height));
        }
    };


    // Mirror of the values carried in opts.type_specific. Populated by
    // parse_type_specific() using the SAME order as option_schema().
    struct parsed_options {

        bool                                        flip_vertical = false;
        bool                                        flip_horizontal = false;
        bool                                        generate_mips = true;
        u32                                         max_size = 0;           // 0 = no limit
        // 0 = auto (keep what decode() produced), 1 = force sRGB, 2 = force linear.
        u8                                          color_space = 0;
        GLT::asset::texture::texture_usage          usage{ GLT::asset::texture::texture_usage::default_ };
        u8                                          compression_quality = 85;
        bool                                        premultiply_alpha = false;
    };

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // Downsample an RGBA image by 2x using a box filter. T is u8 or f32.
    template<typename T>
    [[nodiscard]] std::vector<std::byte> box_downsample_rgba(std::span<const std::byte> src, u32 sw, u32 sh, u32& dw, u32& dh);


    // Bilinear resample of an RGBA image. T is u8 or f32.
    template<typename T>
    [[nodiscard]] std::vector<std::byte> resize_rgba_bilinear(std::span<const std::byte> src, u32 sw, u32 sh, u32 dw, u32 dh);

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    [[nodiscard]] std::expected<std::vector<std::byte>, GLT::asset::import_error> read_source(const std::filesystem::path& path);


    [[nodiscard]] constexpr u64 hash_bytes(std::span<const std::byte> data) noexcept;


    [[nodiscard]] bool wants_hdr(const std::filesystem::path& source) noexcept;


    [[nodiscard]] std::expected<decoded_image, GLT::asset::import_error> decode(std::span<const std::byte> bytes, bool allow_hdr);


    [[nodiscard]] parsed_options parse_type_specific(std::span<const std::byte> blob) noexcept;


    void flip_vertical_inplace  (std::vector<std::byte>& pixels, u32 w, u32 h, u32 bpp);


    void flip_horizontal_inplace(std::vector<std::byte>& pixels, u32 w, u32 h, u32 bpp);


    void premultiply_alpha_rgba8(std::vector<std::byte>& pixels) noexcept;


    // Renormalize each pixel's xyz so normal maps stay unit-length after filtering. Operates on the canonical RGBA8 layout only.
    void renormalize_normal_map_rgba8(std::vector<std::byte>& pixels) noexcept;


    // Map "auto|srgb|linear" → enum value. Empty / unknown → auto.
    [[nodiscard]] GLT::asset::texture::color_space resolve_color_space(u8 user_choice, GLT::asset::texture::texture_usage usage,
        GLT::asset::texture::color_space decoded) noexcept;

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    template<typename T>
    std::vector<std::byte> box_downsample_rgba(std::span<const std::byte> src, u32 sw, u32 sh, u32& dw, u32& dh) {

        static_assert(std::is_same_v<T, u8> || std::is_same_v<T, f32>, "only u8 / f32 supported");

        dw = std::max<u32>(1u, sw / 2u);
        dh = std::max<u32>(1u, sh / 2u);

        std::vector<std::byte> dst(static_cast<size_t>(dw) * dh * 4u * sizeof(T));

        const T* s = reinterpret_cast<const T*>(src.data());
        T*       d = reinterpret_cast<T*>(dst.data());

        for (u32 y = 0; y < dh; ++y) {
            for (u32 x = 0; x < dw; ++x) {

                f32 acc[4] = { 0, 0, 0, 0 };
                int n = 0;

                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {

                        const u32 sx = std::min(2u * x + static_cast<u32>(dx), sw - 1u);
                        const u32 sy = std::min(2u * y + static_cast<u32>(dy), sh - 1u);
                        const size_t off = (static_cast<size_t>(sy) * sw + sx) * 4u;

                        for (int c = 0; c < 4; ++c)
                            acc[c] += static_cast<f32>(s[off + c]);
                        ++n;
                    }
                }

                const size_t doff = (static_cast<size_t>(y) * dw + x) * 4u;
                for (int c = 0; c < 4; ++c) {

                    const f32 avg = acc[c] / static_cast<f32>(n);
                    if constexpr (std::is_same_v<T, u8>)
                        d[doff + c] = static_cast<u8>(std::clamp(avg + 0.5f, 0.0f, 255.0f));
                    else
                        d[doff + c] = avg;
                }
            }
        }
        return dst;
    }


    template<typename T>
    std::vector<std::byte> resize_rgba_bilinear(std::span<const std::byte> src, u32 sw, u32 sh, u32 dw, u32 dh) {

        static_assert(std::is_same_v<T, u8> || std::is_same_v<T, f32>, "only u8 / f32 supported");

        std::vector<std::byte> dst(static_cast<size_t>(dw) * dh * 4u * sizeof(T));
        const T* s = reinterpret_cast<const T*>(src.data());
        T* d = reinterpret_cast<T*>(dst.data());

        const f32 x_ratio = static_cast<f32>(sw) / static_cast<f32>(dw);
        const f32 y_ratio = static_cast<f32>(sh) / static_cast<f32>(dh);

        for (u32 y = 0; y < dh; ++y) {

            const f32 sy_raw = (static_cast<f32>(y) + 0.5f) * y_ratio - 0.5f;
            const f32 sy = std::clamp(sy_raw, 0.0f, static_cast<f32>(sh - 1));
            const u32 y0 = static_cast<u32>(sy);
            const u32 y1 = std::min(y0 + 1u, sh - 1u);
            const f32 fy = sy - static_cast<f32>(y0);

            for (u32 x = 0; x < dw; ++x) {

                const f32 sx_raw = (static_cast<f32>(x) + 0.5f) * x_ratio - 0.5f;
                const f32 sx = std::clamp(sx_raw, 0.0f, static_cast<f32>(sw - 1));
                const u32 x0 = static_cast<u32>(sx);
                const u32 x1 = std::min(x0 + 1u, sw - 1u);
                const f32 fx = sx - static_cast<f32>(x0);

                const size_t i00 = (static_cast<size_t>(y0) * sw + x0) * 4u;
                const size_t i01 = (static_cast<size_t>(y0) * sw + x1) * 4u;
                const size_t i10 = (static_cast<size_t>(y1) * sw + x0) * 4u;
                const size_t i11 = (static_cast<size_t>(y1) * sw + x1) * 4u;

                const size_t doff = (static_cast<size_t>(y) * dw + x) * 4u;

                for (int c = 0; c < 4; ++c) {

                    const f32 v00 = static_cast<f32>(s[i00 + c]);
                    const f32 v01 = static_cast<f32>(s[i01 + c]);
                    const f32 v10 = static_cast<f32>(s[i10 + c]);
                    const f32 v11 = static_cast<f32>(s[i11 + c]);

                    const f32 top = v00 + (v01 - v00) * fx;
                    const f32 bot = v10 + (v11 - v10) * fx;
                    const f32 v   = top + (bot - top) * fy;

                    if constexpr (std::is_same_v<T, u8>)
                        d[doff + c] = static_cast<u8>(std::clamp(v + 0.5f, 0.0f, 255.0f));
                    else
                        d[doff + c] = v;
                }
            }
        }
        return dst;
    }

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    std::expected<std::vector<std::byte>, GLT::asset::import_error> read_source(const std::filesystem::path& path) {

        std::error_code error{};
        const u64 size = GLT::vfs::file_size(path, error);
        if (error || size == 0)
            return std::unexpected{ GLT::asset::import_error::io_failure };

        const auto handle = GLT::vfs::open_file(path, GLT::vfs::file_open_mode::read, error);
        if (handle == ::INVALID_HANDLE || error)
            return std::unexpected{ GLT::asset::import_error::io_failure };

        std::vector<std::byte> bytes(size);
        const size_t got = GLT::vfs::read_file(handle, bytes.data(), size);
        GLT::vfs::close_file(handle);

        if (got != size)
            return std::unexpected{ GLT::asset::import_error::io_failure };

        return bytes;
    }


    constexpr u64 hash_bytes(std::span<const std::byte> data) noexcept {

        u64 h = 0xcbf29ce484222325ull;                 // FNV offset basis
        for (auto b : data) {

            h ^= static_cast<u64>(std::to_integer<u8>(b));
            h *= 0x100000001b3ull;                     // FNV prime
        }
        return h;
    }


    bool wants_hdr(const std::filesystem::path& source) noexcept {

        std::string ext = source.extension().string();
        for (char& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return ext == ".hdr";                          // STB has no EXR support.
    }


    std::expected<decoded_image, GLT::asset::import_error> decode(std::span<const std::byte> bytes, bool allow_hdr) {

        int w = 0, h = 0, comp = 0;
        if (allow_hdr) {

            f32* data = stbi_loadf_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), static_cast<int>(bytes.size()),
                &w, &h, &comp, 4);
            if (!data)
                return std::unexpected{ GLT::asset::import_error::unsupported_format };

            decoded_image out{};
            out.width = static_cast<u32>(w);
            out.height = static_cast<u32>(h);
            out.format = GLT::asset::texture::pixel_format::f32_rgba;
            out.space = GLT::asset::texture::color_space::linear;
            out.is_hdr = true;

            const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
            out.pixels.resize(n * sizeof(f32));
            std::memcpy(out.pixels.data(), data, out.pixels.size());

            stbi_image_free(data);
            return out;
        }

        stbi_uc* data = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), static_cast<int>(bytes.size()), &w, &h, &comp, 4);
        if (!data)
            return std::unexpected{ GLT::asset::import_error::unsupported_format };

        decoded_image out{};
        out.width = static_cast<u32>(w);
        out.height = static_cast<u32>(h);
        out.format = GLT::asset::texture::pixel_format::u8_rgba;
        out.space = GLT::asset::texture::color_space::srgb;
        out.is_hdr = false;

        const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
        out.pixels.resize(n);
        std::memcpy(out.pixels.data(), data, n);

        stbi_image_free(data);
        return out;
    }


    parsed_options parse_type_specific(std::span<const std::byte> blob) noexcept {

        // Defaults mirror option_schema() exactly. An empty / truncated blob is NOT an error
        // headless import passes {} and gets the schema defaults.
        parsed_options out{};

        size_t cursor = 0;

        auto take = [&](void* dst, size_t n) -> bool {
            if (cursor + n > blob.size())
                return false;

            std::memcpy(dst, blob.data() + cursor, n);
            cursor += n;
            return true;
        };
        auto take_string = [&](std::string& s) -> bool {
            u32 len = 0;
            if (!take(&len, sizeof(len)))
                return false;

            if (cursor + len > blob.size())
                return false;

            s.assign(reinterpret_cast<const char*>(blob.data() + cursor), len);
            cursor += len;
            return true;
        };

        u8 b = 0;

        // field 1: flip_vertical
        if (take(&b, 1)) out.flip_vertical = (b != 0);

        // field 2: flip_horizontal
        if (take(&b, 1)) out.flip_horizontal = (b != 0);

        // field 3: generate_mips
        if (take(&b, 1)) out.generate_mips = (b != 0);

        // field 4: max_size (i32, clamp to non-negative)
        i32 max_size = 0;
        if (take(&max_size, sizeof(max_size)))
            out.max_size = (max_size > 0) ? static_cast<u32>(max_size) : 0u;

        // field 5: color_space
        std::string cs;
        if (take_string(cs)) {

            if      (cs == "srgb")              out.color_space = 1;
            else if (cs == "linear")            out.color_space = 2;
            else                                out.color_space = 0;      // "auto"
        }

        // field 6: usage
        std::string usage;
        if (take_string(usage)) {

            using GLT::asset::texture::texture_usage;
            if      (usage == "normal_map")     out.usage = texture_usage::normal_map;
            else if (usage == "mask")           out.usage = texture_usage::mask;
            else if (usage == "ui")             out.usage = texture_usage::ui;
            else if (usage == "albedo")         out.usage = texture_usage::albedo;
            else if (usage == "emission")       out.usage = texture_usage::emission;
            else                                out.usage = texture_usage::default_;
        }

        // field 7: compression_quality
        i32 quality = 85;
        if (take(&quality, sizeof(quality)))
            out.compression_quality = static_cast<u8>(std::clamp(quality, 0, 100));

        // field 8: premultiply_alpha
        if (take(&b, 1)) out.premultiply_alpha = (b != 0);

        return out;
    }


    void flip_vertical_inplace(std::vector<std::byte>& pixels, u32 w, u32 h, u32 bpp) {

        if (h < 2 || bpp == 0)
            return;
        const size_t row_stride = static_cast<size_t>(w) * bpp;

        for (u32 y = 0; y < h / 2; ++y) {

            auto* a = pixels.data() + static_cast<size_t>(y) * row_stride;
            auto* b = pixels.data() + static_cast<size_t>(h - 1 - y) * row_stride;
            std::swap_ranges(a, a + row_stride, b);
        }
    }


    void flip_horizontal_inplace(std::vector<std::byte>& pixels, u32 w, u32 h, u32 bpp) {

        if (w < 2 || bpp == 0)
            return;
        const size_t row_stride = static_cast<size_t>(w) * bpp;

        for (u32 y = 0; y < h; ++y) {

            auto* row = pixels.data() + static_cast<size_t>(y) * row_stride;
            for (u32 x = 0; x < w / 2; ++x) {

                auto* a = row + static_cast<size_t>(x) * bpp;
                auto* b = row + static_cast<size_t>(w - 1 - x) * bpp;
                for (u32 c = 0; c < bpp; ++c)
                    std::swap(a[c], b[c]);
            }
        }
    }


    void premultiply_alpha_rgba8(std::vector<std::byte>& pixels) noexcept {

        for (size_t i = 0; i + 3 < pixels.size(); i += 4) {

            const u32 a = std::to_integer<u8>(pixels[i + 3]);
            for (int c = 0; c < 3; ++c) {

                const u32 v = std::to_integer<u8>(pixels[i + c]);
                pixels[i + c] = static_cast<std::byte>((v * a + 127u) / 255u);
            }
        }
    }


    void renormalize_normal_map_rgba8(std::vector<std::byte>& pixels) noexcept {

        // Snapshot R and B (the "x" and "y" in RGBA), then recompute G.
        // Standard trick: z' = sqrt(1 - x² - y²), then re-encode to [0,255].
        for (size_t i = 0; i + 3 < pixels.size(); i += 4) {

            const f32 x = (static_cast<f32>(std::to_integer<u8>(pixels[i + 0])) / 255.0f) * 2.0f - 1.0f;
            const f32 y = (static_cast<f32>(std::to_integer<u8>(pixels[i + 1])) / 255.0f) * 2.0f - 1.0f;
            const f32 d2 = x * x + y * y;
            const f32 z  = (d2 >= 1.0f) ? 0.0f : std::sqrt(1.0f - d2);
            const f32 z_enc = z * 0.5f + 0.5f;
            pixels[i + 2] = static_cast<std::byte>(std::clamp(z_enc * 255.0f + 0.5f, 0.0f, 255.0f));
        }
    }


    GLT::asset::texture::color_space resolve_color_space(u8 user_choice, GLT::asset::texture::texture_usage usage, 
        GLT::asset::texture::color_space decoded) noexcept {

        using cs = GLT::asset::texture::color_space;
        using tu = GLT::asset::texture::texture_usage;

        if (user_choice == 1)
            return cs::srgb;             // explicit override

        if (user_choice == 2)
            return cs::linear;

        // "auto": let usage refine the decoder's decision.
        switch (usage) {
            case tu::normal_map:
            case tu::mask:
            case tu::emission:  return cs::linear;
            case tu::ui:
            case tu::albedo:    return cs::srgb;
            default:            return decoded;
        }
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    void plugin::on_load() {

        m_registry = GLT::asset::registry::get_ref();
        VALIDATE(m_registry, return, "", "Asset registry not available")
        m_registry->register_factory(this);
        LOG_LOADED
    }


    void plugin::on_unload() {

        if (m_registry)
            m_registry->unregister_factory(this);
        m_registry.reset();
        LOG_UNLOADED
    }


    [[nodiscard]] std::span<const GLT::asset::factory::binding> plugin::bindings() const noexcept {

        static constexpr GLT::asset::factory::binding b[] = {
            { "png",    GLT::asset::core_types::texture2D },
            { "jpg",    GLT::asset::core_types::texture2D },
            { "jpeg",   GLT::asset::core_types::texture2D },
            { "bmp",    GLT::asset::core_types::texture2D },
            { "tga",    GLT::asset::core_types::texture2D },
            { "gif",    GLT::asset::core_types::texture2D },
            { "psd",    GLT::asset::core_types::texture2D },
            { "hdr",    GLT::asset::core_types::texture2D },
            { "pic",    GLT::asset::core_types::texture2D },
            { "pnm",    GLT::asset::core_types::texture2D },
        };
        return b;
    }


    // Schema order here MUST stay in lockstep with parse_type_specific().
    [[nodiscard]] std::span<const GLT::asset::factory::i_asset_factory_plugin::option_descriptor>
        plugin::option_schema(const GLT::asset::type& target_type) const noexcept {

        if (target_type != GLT::asset::core_types::texture2D)
            return {};

        using desc = GLT::asset::factory::i_asset_factory_plugin::option_descriptor;

        static constexpr desc schema[] = {
            {   // 1
                .key                = "flip_vertical",
                .label              = "Flip vertical",
                .type               = desc::kind::boolean,
                .default_value      = "false",
                .enumeration_values = "",
                .tooltip            = "Mirror the image top-to-bottom. Use for sources authored with a bottom-left origin (OpenGL convention).",
            },
            {   // 2
                .key                = "flip_horizontal",
                .label              = "Flip horizontal",
                .type               = desc::kind::boolean,
                .default_value      = "false",
                .enumeration_values = "",
                .tooltip            = "Mirror the image left-to-right. Tick together with Flip vertical for a 180° rotation.",
            },
            {   // 3
                .key                = "generate_mips",
                .label              = "Generate mipmaps",
                .type               = desc::kind::boolean,
                .default_value      = "true",
                .enumeration_values = "",
                .tooltip            = "Emit a full mip chain down to 1x1 using a box filter. Turn off for UI / cursor textures that are never minified.",
            },
            {   // 4
                .key                = "max_size",
                .label              = "Max size (px)",
                .type               = desc::kind::integer,
                .default_value      = "0",
                .enumeration_values = "",
                .tooltip            = "Downscale the image so its longest side is at most N pixels. Aspect ratio preserved. 0 = no limit. Common values: 512, 1024, 2048, 4096.",
            },
            {   // 5
                .key                = "color_space",
                .label              = "Color space",
                .type               = desc::kind::enumeration,
                .default_value      = "auto",
                .enumeration_values = "auto|srgb|linear",
                .tooltip            = "'auto' trusts the decoder and the usage tag. Pick 'srgb' for color/UI, 'linear' for data (roughness, metallic, masks).",
            },
            {   // 6
                .key                = "usage",
                .label              = "Usage",
                .type               = desc::kind::enumeration,
                .default_value      = "default",
                .enumeration_values = "default|normal_map|mask|ui|albedo|emission",
                .tooltip            = "Semantic tag. Drives the auto color-space default and, once block compression is wired up, the codec choice (BC5 for normal_map, BC4 for mask, BC7 for albedo/ui, BC6H for emission).",
            },
            {   // 7
                .key                = "compression_quality",
                .label              = "Compression quality",
                .type               = desc::kind::integer,
                .default_value      = "85",
                .enumeration_values = "",
                .tooltip            = "0 = smallest (most lossy), 100 = best fidelity. Stored as a hint; consumed once BCn/ASTC encoding is enabled.",
            },
            {   // 8
                .key                = "premultiply_alpha",
                .label              = "Premultiply alpha",
                .type               = desc::kind::boolean,
                .default_value      = "false",
                .enumeration_values = "",
                .tooltip            = "Multiply RGB by A. Use for sprite / particle textures composited with one/one-minus-src-alpha blending.",
            },
        };
        return schema;
    }


    [[nodiscard]] std::expected<GLT::asset::factory::import_result, GLT::asset::import_error> plugin::import(
        const std::filesystem::path& source, GLT::asset::type target_type, const GLT::asset::import_options& opts,
        GLT::asset::asset_writer& out) {


        if (target_type != GLT::asset::core_types::texture2D)
            return std::unexpected{ GLT::asset::import_error::not_supported };

        const parsed_options option = parse_type_specific(opts.type_specific);

        // read source through the VFS ----
        auto bytes_res = read_source(source);
        if (!bytes_res)
            return std::unexpected{ bytes_res.error() };
        const std::vector<std::byte>& bytes = *bytes_res;

        // decode into canonical form ----
        const bool hdr = wants_hdr(source);
        auto decoded_res = decode(std::span<const std::byte>(bytes.data(), bytes.size()), hdr);
        if (!decoded_res)
            return std::unexpected{ decoded_res.error() };
        decoded_image img = std::move(*decoded_res);

        // ---- transforms, in the following well-defined order --------------------------------------
        //   1. flips  (orientation)
        //   2. premultiply alpha  (must precede resize/mip generation)
        //   3. resize to max_size
        //   4. mip generation (with per-level fixups for normal maps)

        // option: flip_vertical -----------------------------------------------------------------------
        if (option.flip_vertical)
            flip_vertical_inplace(img.pixels, img.width, img.height, img.bytes_per_pixel());

        // option: flip_horizontal ---------------------------------------------------------------------
        if (option.flip_horizontal)
            flip_horizontal_inplace(img.pixels, img.width, img.height, img.bytes_per_pixel());

        // option: premultiply_alpha -------------------------------------------------------------------
        if (option.premultiply_alpha) {

            if (img.format == GLT::asset::texture::pixel_format::u8_rgba)
                premultiply_alpha_rgba8(img.pixels);
            else
                LOG(warn, "texture_stb: premultiply_alpha requested for non-RGBA8 [{}] - ignored", source.generic_string());
        }

        // option: max_size ----------------------------------------------------------------------------
        // Downscale the longest side to <= max_size, preserving aspect ratio.
        // Never upscales: if the source already fits, this is a no-op.
        if (option.max_size > 0) {

            const u32 longest = std::max(img.width, img.height);
            if (longest > option.max_size) {

                const f32 scale = static_cast<f32>(option.max_size) / static_cast<f32>(longest);
                const u32 nw = std::max<u32>(1u, static_cast<u32>(img.width  * scale + 0.5f));
                const u32 nh = std::max<u32>(1u, static_cast<u32>(img.height * scale + 0.5f));

                std::vector<std::byte> resized;
                switch (img.format) {
                    case GLT::asset::texture::pixel_format::u8_rgba:
                        resized = resize_rgba_bilinear<u8>(std::span<const std::byte>{ img.pixels }, img.width, img.height, nw, nh);
                        break;
                    case GLT::asset::texture::pixel_format::f32_rgba:
                        resized = resize_rgba_bilinear<f32>(std::span<const std::byte>{ img.pixels }, img.width, img.height, nw, nh);
                        break;
                    default:
                        LOG(warn, "texture_stb: max_size resize not implemented for format {} [{}] - skipped", 
                            static_cast<int>(img.format), source.generic_string());
                        break;
                }

                if (!resized.empty()) {

                    img.pixels = std::move(resized);
                    img.width  = nw;
                    img.height = nh;
                }
            }
        }

        // option: color_space + usage -----------------------------------------------------------------
        // Usage acts as an "auto" refinement; explicit color_space always wins.
        img.space = resolve_color_space(option.color_space, option.usage, img.space);

        // ---- build the concatenated pixel blob + mip table ------------------------------------------
        std::vector<std::byte> pixel_blob{};
        std::vector<GLT::asset::texture::mip_range> mip_ranges{};

        pixel_blob.reserve(img.pixels.size());
        pixel_blob.insert(pixel_blob.end(), img.pixels.begin(), img.pixels.end());
        mip_ranges.push_back({
            .offset = 0,
            .size = img.pixels.size(),
            .width = img.width,
            .height = img.height,
            .depth = 1,
        });

        // option: generate_mips -----------------------------------------------------------------------
        if (option.generate_mips) {

            const bool is_normal = (option.usage == GLT::asset::texture::texture_usage::normal_map);

            u32 cw = img.width;
            u32 ch = img.height;
            std::vector<std::byte> cur = img.pixels;

            while (cw > 1 || ch > 1) {

                std::span<const std::byte> cur_span(cur.data(), cur.size());
                u32 dw = 0, dh = 0;
                std::vector<std::byte> down;

                switch (img.format) {
                    case GLT::asset::texture::pixel_format::u8_rgba:  down = box_downsample_rgba<u8>(cur_span, cw, ch, dw, dh);  break;
                    case GLT::asset::texture::pixel_format::f32_rgba: down = box_downsample_rgba<f32>(cur_span, cw, ch, dw, dh); break;
                    default:
                        LOG(warn, "texture_stb: mip generation not implemented for format {} - stopping at {}x{}",
                            static_cast<int>(img.format), cw, ch);
                        dw = 0;
                        break;
                }

                if (dw == 0)
                    break;

                // Normal maps need renormalization after each filter step, otherwise
                // the box average of several unit vectors has length < 1 and lighting
                // develops visible bias.
                if (is_normal && img.format == GLT::asset::texture::pixel_format::u8_rgba)
                    renormalize_normal_map_rgba8(down);

                mip_ranges.push_back({
                    .offset = static_cast<u64>(pixel_blob.size()),
                    .size = down.size(),
                    .width = dw,
                    .height = dh,
                    .depth = 1,
                });
                pixel_blob.insert(pixel_blob.end(), down.begin(), down.end());

                cur = std::move(down);
                cw = dw;
                ch = dh;
            }
        }

        // format descriptor ---------------------------------------------------------------------------
        GLT::asset::texture::texture_format fmt{};
        fmt.width = img.width;
        fmt.height = img.height;
        fmt.depth = 1;
        fmt.mip_levels = static_cast<u16>(mip_ranges.size());
        fmt.array_layers = 1;
        fmt.format = img.format;
        fmt.space = img.space;
        fmt.kind = GLT::asset::texture::texture_kind::texture_2d;
        fmt.usage = option.usage;
        fmt.compression_quality = option.compression_quality;

        // write the chunks. Registry owns header / string table / deps -------------------------------
        out.set_name(source.stem().string());
        out.write_chunk(GLT::asset::texture::CHUNK_TEXTURE_FORMAT, std::as_bytes(std::span{ &fmt, 1 }));
        out.write_chunk(GLT::asset::texture::CHUNK_PIXEL_DATA, std::span<const std::byte>{ pixel_blob });

        if (mip_ranges.size() > 1)
            out.write_chunk(GLT::asset::texture::CHUNK_MIP_RANGES, std::as_bytes(std::span{ mip_ranges }));

        // Payload hash: format + pixel blob + mip table.
        u64 hash = hash_bytes(std::as_bytes(std::span{ &fmt, 1 }));
        hash ^= hash_bytes(std::span<const std::byte>{ pixel_blob }) * 0x9E3779B97F4A7C15ull;
        hash ^= hash_bytes(std::as_bytes(std::span{ mip_ranges }))   * 0xC2B2AE3D27D4EB4Full;

        GLT::asset::factory::import_result result{
            .output_path = {},
            .id = UUID(),
            .source_hash = hash_bytes(bytes),
            .payload_hash = hash,
        };

        LOG(info, "texture_stb: [{}] {}x{} ({}) {} mip(s) usage={} q={} cs={}{}{}{}",
            source.generic_string(), img.width, img.height, static_cast<int>(img.format),
            mip_ranges.size(), static_cast<int>(option.usage), option.compression_quality,
            (img.space == GLT::asset::texture::color_space::srgb ? "srgb" : "linear"),
            option.flip_vertical ? " flipV"  : "",
            option.flip_horizontal ? " flipH"  : "",
            option.premultiply_alpha ? " premul" : "");

        return result;
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
