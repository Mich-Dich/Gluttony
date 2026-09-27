
#pragma once

#include "asset/type.h"
#include "asset/region.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::world {

    // CONSTANTS =======================================================================================================

    inline constexpr GLT::asset::chunk_id               CHUNK_WORLD_REGIONS = 0x0300;           // region_descriptor[] (required)
    inline constexpr GLT::asset::chunk_id               CHUNK_WORLD_SETTINGS = 0x0301;          // opaque, plugin-defined
    inline constexpr GLT::asset::chunk_id               CHUNK_WORLD_ACTIVE_CAMERA = 0x0302;
    inline constexpr GLT::asset::chunk_id               CHUNK_WORLD_METADATA = 0x0303;          // utf-8 "key\0value\0"

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Runtime representation of a .glt_world asset.
    class world_asset final : public GLT::asset::i_runtime_asset {
    public:

        GLT::asset::type                                asset_type{ GLT::asset::core_types::world };
        std::vector<GLT::asset::region::region>         region_index;
        std::vector<std::byte>                          settings;       // opaque, plugin-defined

        // Chosen entity that drives the renderer's view. 0xFFFFFFFF == "no active camera". Stored as raw index/generation
        // so asset/world.h doesn't have to know about world/entity.h's entity_id layout.
        u32                                             active_camera_index{ 0xFFFFFFFFu };
        u32                                             active_camera_generation{ 0u };


        FORCE_INLINE_R GLT::asset::type type() const noexcept override { return asset_type; }


        FORCE_INLINE_R u64 memory_usage() const noexcept override {
            return sizeof(*this) + region_index.capacity() * sizeof(GLT::asset::region::region) + settings.capacity();
        }
    };

}
