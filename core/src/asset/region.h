
#pragma once

#include "asset/type.h"
#include "world/entity.h"
#include "util/data_structures/AABB.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::region {

    // CONSTANTS =======================================================================================================

    // Chunk IDs (any u32; stable once shipped). Region files reserve 0x02xx.
    inline constexpr GLT::asset::chunk_id               CHUNK_REGION_BOUNDS = 0x0200;       // bounds (required)

    inline constexpr GLT::asset::chunk_id               CHUNK_REGION_ENTITIES = 0x0201;     // opaque blob (optional)

    inline constexpr GLT::asset::chunk_id               CHUNK_REGION_METADATA = 0x0202;     // utf-8 "key\0value\0" (optional)

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Live runtime state for one region. Owned by the world plugin; the registry never sees this.
    // `asset` is the handle to the backing region_asset - it is INVALID_HANDLE while the region is streamed out.
    //
    // Deliberately minimal: no entity list, no dependency list. Those live inside the concrete plugin so the public struct
    // stays cheap to copy and safe to hand out as a span.
    struct region {

        GLT::UUID                                       id{};
        GLT::asset::handle                              asset{};                    // INVALID until streamed in
        GLT::AABB                                       bounds{};
        bool                                            is_active{ false };
        u8                                              flags{ 0 };                 // bit 0 = always_loaded
        u8                                              _pad[6]{};
    };
    static_assert(std::is_trivially_copyable_v<region>);
    static_assert(sizeof(region) == 56);

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Runtime representation of a .glt_region asset. Handlers own this type; the registry only ever sees it as `i_runtime_asset*`.
    //
    // The entity blob is intentionally opaque. The world plugin that consumes it declares `entity_codec` and knows how
    // to interpret the bytes. This is what keeps the asset handler ignorant of ECS vs inheritance-based worlds.
    class region_asset final : public GLT::asset::i_runtime_asset {
    public:

        GLT::asset::type                                asset_type{ GLT::asset::core_types::region };
        AABB                                            bounds{};
        u32                                             entity_codec{ 0 };          // 0 = empty / no entities
        u32                                             _pad0{};
        std::vector<std::byte>                          entity_data;                // opaque, codec-defined


        FORCE_INLINE_R GLT::asset::type type() const noexcept override { return asset_type; }


        FORCE_INLINE_R u64 memory_usage() const noexcept override { return sizeof(*this) + entity_data.capacity(); }
    };

}
