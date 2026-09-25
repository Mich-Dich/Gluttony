
#pragma once

#include <asset/world.h>
#include <asset/region.h>
#include <plugin_system/i_asset_handler_plugin.h>
#include <plugin_system/i_asset_registry_plugin.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // One row in CHUNK_WORLD_REGIONS. Runtime-only fields (asset handle,
    // is_active) are deliberately absent — they're reconstructed by the
    // handler from `info.dependencies` in region-declaration order.
    //
    // NOTE: these disk structs ultimately belong in asset/world.h and
    // asset/region.h so the eventual writer can see them too. Kept here
    // for now because the writer isn't written yet.
    struct region_disk {

        GLT::UUID           id{};           //  16 - must match the region asset's own UUID
        GLT::AABB           bounds{};       //  32
        u8                  flags{ 0 };     //   1 - bit 0 = always_loaded
        u8                  _pad[7]{};      //   7
    };
    static_assert(sizeof(region_disk) == 48);
    static_assert(std::is_trivially_copyable_v<region_disk>);


    // Prepended to CHUNK_REGION_ENTITIES. The blob that follows is opaque
    // to the handler; only the plugin's entity_codec knows what's in it.
    struct entity_blob_header {

        u32                 codec{ 0 };     // CODEC_NONE / CODEC_ENTT_V1 / ...
        u32                 _pad{ 0 };      // reserve for codec-internal versioning
    };
    static_assert(sizeof(entity_blob_header) == 8);
    static_assert(std::is_trivially_copyable_v<entity_blob_header>);

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Handler for both .glt_world and .glt_region. Two types, one class -
    // the branch is a single switch on info.asset_type at the top of
    // deserialize(). Keeping them together makes the invariant between
    // them (region UUID == region asset UUID) trivially auditable.
    class world_asset_handler final : public GLT::asset::i_asset_handler {
    public:

        void on_load()   override;


        void on_unload() override;

        // ---- i_asset_handler ----

        [[nodiscard]] std::span<const GLT::asset::type> types() const noexcept override;


        [[nodiscard]] std::expected<GLT::unique_ref<GLT::asset::i_runtime_asset>, GLT::asset::load_error>
            deserialize(const GLT::asset::info& info, GLT::asset::chunk_reader& reader) override;


        [[nodiscard]] std::expected<void, GLT::asset::load_error> serialize(const GLT::asset::info& info, 
            const GLT::asset::i_runtime_asset& asset, GLT::asset::asset_writer& out) const override;

    private:

        [[nodiscard]] std::expected<GLT::unique_ref<GLT::asset::i_runtime_asset>, GLT::asset::load_error>
            deserialize_world(const GLT::asset::info& info, GLT::asset::chunk_reader& reader);


        [[nodiscard]] std::expected<GLT::unique_ref<GLT::asset::i_runtime_asset>, GLT::asset::load_error>
            deserialize_region(const GLT::asset::info& info, GLT::asset::chunk_reader& reader);


        [[nodiscard]] std::expected<void, GLT::asset::load_error> serialize_world(const GLT::asset::info& info,
            const GLT::asset::world::world_asset& asset, GLT::asset::asset_writer& out) const;


        [[nodiscard]] std::expected<void, GLT::asset::load_error> serialize_region(const GLT::asset::info& info,
            const GLT::asset::region::region_asset& asset, GLT::asset::asset_writer& out) const;


        GLT::ref<GLT::asset::i_asset_registry_plugin>       m_registry{};
    };

}

#include "world_asset_handler.inl"
