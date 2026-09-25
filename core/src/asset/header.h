
#pragma once

#include <type_traits>
#include "asset/type.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Fixed-layout, memcpy-able, versioned. This is what the registry reads first.
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

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
