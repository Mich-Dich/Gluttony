#pragma once

#if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-function"
#endif

// Single-header decoder. Definitions only in this TU.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DEVICE_IO
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#include <miniaudio.h>

#if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic pop
#endif

#include <asset/audio.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::factory::audio_miniaudio {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Everything the factory needs to emit chunks. Always interleaved f32.
    struct decoded_audio {

        std::vector<f32>            samples;        // interleaved, frame-major
        u32                         sample_rate{};
        u16                         channels{};
        u64                         frame_count{};

        bool                        has_loop{ false };
        u64                         loop_start{ 0 };
        u64                         loop_end{ 0 };
    };

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // Read whole file through the VFS. Import-only - factories run in the
    // editor / build tools, so a synchronous read here is fine.
    [[nodiscard]] std::expected<std::vector<std::byte>, GLT::asset::import_error> read_source(const std::filesystem::path& path);


    // FNV-1a 64. Same placeholder for xxh3 that mesh_assimp uses.
    [[nodiscard]] constexpr u64 hash_bytes(std::span<const std::byte> data) noexcept;


    [[nodiscard]] std::expected<decoded_audio, GLT::asset::import_error> decode(std::span<const std::byte> bytes);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

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


    // Decodes ANY miniaudio-supported source into interleaved f32.
    // We ask miniaudio for f32 output; it does the resampling/format
    // conversion for us using the source's native rate/channels.
    std::expected<decoded_audio, GLT::asset::import_error> decode(std::span<const std::byte> bytes) {

        ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);   // 0 = native
        ma_decoder decoder{};
        if (ma_decoder_init_memory(bytes.data(), bytes.size(), &cfg, &decoder) != MA_SUCCESS)
            return std::unexpected{ GLT::asset::import_error::unsupported_format };

        decoded_audio out{};
        out.sample_rate = decoder.outputSampleRate;
        out.channels    = static_cast<u16>(decoder.outputChannels);

        // Fast path: file length is known (WAV, FLAC, …).
        ma_uint64 frame_count = 0;
        if (ma_decoder_get_length_in_pcm_frames(&decoder, &frame_count) == MA_SUCCESS && frame_count > 0) {

            out.samples.resize(frame_count * out.channels);
            ma_uint64 frames_read = 0;
            ma_decoder_read_pcm_frames(&decoder, out.samples.data(), frame_count, &frames_read);

            out.frame_count = frames_read;
            out.samples.resize(frames_read * out.channels);

        } else {

            // Streaming path (some MP3/Ogg streams).
            constexpr ma_uint64 CHUNK = 4096;
            std::vector<f32> scratch(CHUNK * out.channels);

            for (;;) {

                ma_uint64 frames_read = 0;
                ma_decoder_read_pcm_frames(&decoder, scratch.data(), CHUNK, &frames_read);
                if (frames_read == 0)
                    break;

                out.samples.insert(out.samples.end(), scratch.begin(), scratch.begin() + frames_read * out.channels);
            }
            out.frame_count = out.samples.size() / (out.channels ? out.channels : 1);
        }

        ma_decoder_uninit(&decoder);

        if (out.samples.empty() || out.channels == 0)
            return std::unexpected{ GLT::asset::import_error::invalid_source };

        return out;
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
            { "wav",    GLT::asset::core_types::audio },
            { "ogg",    GLT::asset::core_types::audio },
            { "mp3",    GLT::asset::core_types::audio },
            { "flac",   GLT::asset::core_types::audio },
        };
        return b;
    }


    [[nodiscard]] std::expected<GLT::asset::factory::import_result, GLT::asset::import_error> plugin::import(
        const std::filesystem::path& source, GLT::asset::type target_type, const GLT::asset::import_options& /*opts*/,
        GLT::asset::asset_writer& out) {


        if (target_type != GLT::asset::core_types::audio)
            return std::unexpected{ GLT::asset::import_error::not_supported };

        // read source through the VFS ----
        auto bytes_res = read_source(source);
        if (!bytes_res)
            return std::unexpected{ bytes_res.error() };
        const std::vector<std::byte>& bytes = *bytes_res;

        // decode into interleaved f32 ----
        auto decoded_res = decode(std::span<const std::byte>(bytes.data(), bytes.size()));
        if (!decoded_res)
            return std::unexpected{ decoded_res.error() };
        const decoded_audio& decoded = *decoded_res;

        // build the format descriptor ----
        GLT::asset::audio::format fmt{};
        fmt.sample_rate = decoded.sample_rate;
        fmt.channels = decoded.channels;
        fmt.bit_depth = 32;
        fmt.kind = GLT::asset::audio::sample_kind::f32;
        fmt.frame_count = decoded.frame_count;

        // write the chunks. Registry owns header / string table / deps.
        out.set_name(source.stem().string());
        out.write_chunk(GLT::asset::audio::CHUNK_FORMAT,   std::as_bytes(std::span{ &fmt, 1 }));
        out.write_chunk(GLT::asset::audio::CHUNK_PCM_DATA, std::as_bytes(std::span{ decoded.samples }));

        if (decoded.has_loop) {

            GLT::asset::audio::loop_points lp{};
            lp.loop_start_frame = decoded.loop_start;
            lp.loop_end_frame   = decoded.loop_end;
            lp.has_loop         = 1;
            out.write_chunk(GLT::asset::audio::CHUNK_LOOP_POINTS, std::as_bytes(std::span{ &lp, 1 }));
        }

        // Payload hash: format + samples. Cheap, but stable.
        u64 hash = hash_bytes(std::as_bytes(std::span{ &fmt, 1 }));
        hash ^= hash_bytes(std::as_bytes(std::span{ decoded.samples })) * 0x9E3779B97F4A7C15ull;

        GLT::asset::factory::import_result result{

            .output_path = {},                                  // let the registry pick
            .id          = UUID(),
            .source_hash = hash_bytes(bytes),
            .payload_hash = hash,
        };

        LOG(info, "imported [{}] -> {} frames, {} ch, {} Hz",
            source.generic_string(), decoded.frame_count, decoded.channels, decoded.sample_rate);

        return result;
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
