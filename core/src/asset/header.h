
#pragma once

#include <type_traits>
#include "asset/type.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset {

    // CONSTANTS =======================================================================================================

    // editor-only preview ---------------------------------------------------------------------------------------------

    // Chunk id reserved for an editor-only RGBA8 preview embedded in an asset file
    //
    // The factory writes a small (max side <= THUMBNAIL_MAX_SIDE) RGBA8 image into this chunk on import/save
    // The runtime handler never reads it, and it never enters the texture_asset The editor's icon_manager fetches
    // it directly through i_asset_registry_plugin::read_chunk(path, CHUNK_THUMBNAIL) and owns the GPU image
    //
    // Chunk payload layout:
    //   [thumbnail_header]                 (8 bytes, trivially copyable)
    //   [width * height * 4 bytes RGBA8]   row-major, tightly packed
    inline constexpr GLT::asset::chunk_id       CHUNK_THUMBNAIL = 0x0210;

    // Longest-edge cap the factory applies when it bakes a CHUNK_THUMBNAIL
    inline constexpr u32                        THUMBNAIL_MAX_SIDE = 256;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // @brief Fixed-layout file header
    //
    // Every asset file starts with this struct. It identifies the asset, points at the string / dependency / chunk tables,
    // and stores the version numbers the registry needs before it does anything else. The struct is trivially copyable so the
    // registry can `memcpy` it out of a raw byte buffer without any per-field parsing
    //
    // On-disk section order (offsets are stored above):
    // @brief  - [asset_header]        (this struct)
    // @brief  - [string table]        names, source paths, dependency paths
    // @brief  - [dependency table]    dependency_disk[]
    // @brief  - [chunk table]         chunk_entry[]
    // @brief  - [chunk data]          8-byte aligned, one blob per chunk
    struct header {

        static constexpr u32                    MAGIC = 0x544C4741; // "AGLT"
        static constexpr u16                    CURRENT_VERSION  = 1;

        u32                                     magic{};
        u16                                     format_version{};
        u16                                     min_engine_version{};
        u32                                     flags{};                    // GLT::asset::flags
        UUID                                    id{};
        GLT::asset::type                        asset_type{};               // strong u32 (see §4)
        content_hash                            hash{};

        // offsets into the file
        u64                                     name_offset{};
        u64                                     source_path_offset{};
        u64                                     chunk_table_offset{};
        u64                                     dependency_table_offset{};
        u64                                     string_table_offset{};

        u32                                     chunk_count{};
        u32                                     dependency_count{};
        u64                                     total_size{};
    };
    static_assert(std::is_trivially_copyable_v<header>);

    // A file dump should look like this:
    // ┌────────────────────────────────────────────────┐
    // │ asset_header         (fixed)                   │
    // ├────────────────────────────────────────────────┤
    // │ string table         (name, source path, ...)  │
    // ├────────────────────────────────────────────────┤
    // │ dependency table     (asset_id[])              │
    // ├────────────────────────────────────────────────┤
    // │ chunk table          (chunk_entry[])           │
    // ├────────────────────────────────────────────────┤
    // │ chunk data         ← what handlers actually    │
    // │                       care about               │
    // └────────────────────────────────────────────────┘

    // editor-only preview ---------------------------------------------------------------------------------------------

    // @brief Small descriptor that prefixes a CHUNK_THUMBNAIL payload
    //
    // Tells the editor's icon_manager how to interpret the RGBA8 bytes that follow. [format] is a texture::pixel_format value;
    // only [u8_rgba] is emitted today, but the field exists so the chunk can grow later
    struct thumbnail_header {
        u16                                     width{};
        u16                                     height{};
        u16                                     format{};       // pixel_format — always u8_rgba today
        u16                                     _pad{};
    };
    static_assert(sizeof(thumbnail_header) == 8);
    static_assert(std::is_trivially_copyable_v<thumbnail_header>);

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
