
#pragma once

#include "asset/type.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::audio {

    // CONSTANTS =======================================================================================================

    // Chunk IDs (any u32, stable once shipped).
    inline constexpr GLT::asset::chunk_id               CHUNK_FORMAT = 0x0100;   // audio::format (required)

    inline constexpr GLT::asset::chunk_id               CHUNK_PCM_DATA = 0x0101;   // interleaved f32 samples (required)

    inline constexpr GLT::asset::chunk_id               CHUNK_LOOP_POINTS = 0x0102;   // audio::loop_points (optional)

    inline constexpr GLT::asset::chunk_id               CHUNK_METADATA = 0x0103;   // utf-8 "key\0value\0" pairs (optional)

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Canonical on-disk sample representation. The factory is expected to
    // normalise whatever it decoded from the source into this.
    enum class sample_kind : u16 {

        unknown = 0,
        u8,
        s16,
        s24,
        s32,
        f32,        // what the current miniaudio factory always emits
        f64,
    };


    // Fixed-layout descriptor of the PCM that follows. Never reinterpret
    // samples without consulting this - the handler trusts it blindly.
    struct format {

        u32             sample_rate;            // frames / second
        u16             channels;               // interleaved
        u16             bit_depth;              // valid bits per sample (32 for f32)
        sample_kind     kind;                   // how to read CHUNK_PCM_DATA
        u16             _pad0{};
        u64             frame_count;            // total frames (not bytes)
    };
    static_assert(sizeof(format) == 24);
    static_assert(std::is_trivially_copyable_v<format>);


    struct loop_points {

        u64             loop_start_frame;
        u64             loop_end_frame;
        u8              has_loop;               // 0/1
        u8              _pad[7]{};
    };
    static_assert(sizeof(loop_points) == 24);
    static_assert(std::is_trivially_copyable_v<loop_points>);


    enum class attenuation_model : u8 {

        none = 0,
        inverse_distance,
        linear_distance,
        exponential_distance,
    };


    enum class state : u8 {

        stopped = 0,
        playing,
        paused,
    };


    struct source_config {

        f32                                     volume = 1.0f;
        f32                                     pan = 0.0f;         // -1 = left, 1 = right (2D)
        bool                                    loop = false;
        f32                                     play_speed = 1.0f;
        bool                                    is_3d = false;

        // 3D parameters (used when is_3d is true)
        glm::vec3                               position = { 0.0f, 0.0f, 0.0f };
        glm::vec3                               velocity = { 0.0f, 0.0f, 0.0f };
        f32                                     min_distance = 1.0f;
        f32                                     max_distance = 100.0f;
        f32                                     rolloff_factor = 1.0f;
        attenuation_model                       attenuation = attenuation_model::inverse_distance;
    };


    struct listener_config {

        glm::vec3                               position = { 0.0f, 0.0f, 0.0f };
        glm::vec3                               forward = { 0.0f, 0.0f, -1.0f };
        glm::vec3                               up = { 0.0f, 1.0f, 0.0f };
        glm::vec3                               velocity = { 0.0f, 0.0f, 0.0f };
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // The runtime representation of a loaded audio asset. Canonical form is interleaved f32
    // every backend (SoLoud, FMOD, miniaudio) accepts it without a conversion step.
    //
    // IMPORTANT: this struct OWNS the decoded PCM. The chunk_reader hands out spans into a buffer that dies when the 
    // registry's load function returns, so we copy what we want to keep.
    class audio_asset final : public GLT::asset::i_runtime_asset {
    public:

        GLT::asset::type                        asset_type{ GLT::asset::core_types::audio };
        GLT::asset::audio::format               format{};
        std::vector<f32>                        samples;        // interleaved, frame-major
        GLT::asset::audio::loop_points          loop{};


        FORCE_INLINE_R GLT::asset::type type() const noexcept override { return asset_type; }


        FORCE_INLINE_R u64 memory_usage() const noexcept override { return sizeof(*this) + samples.capacity() * sizeof(f32); }

    };

}
