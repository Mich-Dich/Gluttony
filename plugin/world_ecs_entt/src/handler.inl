
#pragma once

#include <plugin_system/i_asset_registry_plugin.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    template<typename T>
    inline void write_chunk_as(GLT::asset::asset_writer& out, GLT::asset::chunk_id id, const T& v);

    template<typename T>
    inline void write_chunk_span(GLT::asset::asset_writer& out, GLT::asset::chunk_id id, std::span<const T> v);

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    inline void write_chunk_as(GLT::asset::asset_writer& out, GLT::asset::chunk_id id, const void* data, size_t bytes);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    template<typename T>
    inline void write_chunk_as(GLT::asset::asset_writer& out, GLT::asset::chunk_id id, const T& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        write_chunk_as(out, id, &v, sizeof(T));
    }

    template<typename T>
    inline void write_chunk_span(GLT::asset::asset_writer& out, GLT::asset::chunk_id id, std::span<const T> v) {
        static_assert(std::is_trivially_copyable_v<T>);
        write_chunk_as(out, id, v.data(), v.size() * sizeof(T));
    }

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    inline void write_chunk_as(GLT::asset::asset_writer& out, GLT::asset::chunk_id id, const void* data, size_t bytes) {

        out.write_chunk(id, { static_cast<const std::byte*>(data), bytes });
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    FORCE_INLINE void world_asset_handler::on_load() {

        m_registry = GLT::asset::registry::get_ref();
        VALIDATE(m_registry, return, "", "asset_registry not available - world handler inactive")
        m_registry->register_handler(this);
        LOG_LOADED
    }


    FORCE_INLINE void world_asset_handler::on_unload() {

        if (m_registry)
            m_registry->unregister_handler(this);

        m_registry.reset();
        LOG_UNLOADED
    }


    FORCE_INLINE_R std::span<const GLT::asset::type> world_asset_handler::types() const noexcept {

        static constexpr GLT::asset::type t[] = {
            GLT::asset::core_types::world,
            GLT::asset::core_types::region,
        };
        return t;
    }


    FORCE_INLINE_R std::expected<GLT::unique_ref<GLT::asset::i_runtime_asset>, GLT::asset::load_error>
    world_asset_handler::deserialize(const GLT::asset::info& info, GLT::asset::chunk_reader& reader) {

        using namespace GLT::asset::core_types;
        if (info.asset_type == world)
            return deserialize_world(info, reader);
        if (info.asset_type == region)
            return deserialize_region(info, reader);

        return std::unexpected{ GLT::asset::load_error::no_handler };
    }


    FORCE_INLINE_R std::expected<void, GLT::asset::load_error>
    world_asset_handler::serialize(const GLT::asset::info& info, const GLT::asset::i_runtime_asset& asset, GLT::asset::asset_writer& out) const {

        using namespace GLT::asset::core_types;

        if (info.asset_type == world) {
            const auto& wa = static_cast<const GLT::asset::world::world_asset&>(asset);
            return serialize_world(info, wa, out);
        }
        if (info.asset_type == region) {
            const auto& ra = static_cast<const GLT::asset::region::region_asset&>(asset);
            return serialize_region(info, ra, out);
        }

        return std::unexpected{ GLT::asset::load_error::no_handler };
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    FORCE_INLINE_R std::expected<GLT::unique_ref<GLT::asset::i_runtime_asset>, GLT::asset::load_error>
    world_asset_handler::deserialize_world(const GLT::asset::info& info, GLT::asset::chunk_reader& reader) {

        // ---- required chunks ----------------------------------------------------------------------------------------

        const auto disks = reader.get_as<region_disk>(GLT::asset::world::CHUNK_WORLD_REGIONS);
        VALIDATE(!disks.empty(), return std::unexpected{ GLT::asset::load_error::corrupt_header }, "",
            "[{}] missing region list chunk", info.name)

        // ---- positional dependency invariant ------------------------------------------------------------------------

        // The writer is required to emit one dependency per region, in region-declaration order.
        // Mismatch means the file was hand-edited or the factory changed; we still load,
        // but trailing regions get INVALID handles and will never stream in.
        if (disks.size() != info.dependencies.size()) {

            LOG(warn, "[{}] {} regions declared but {} dependencies present - trailing regions will be unresolvable",
                info.name, disks.size(), info.dependencies.size());
        }

        // ---- build the runtime asset --------------------------------------------------------------------------------

        auto asset = std::make_unique<GLT::asset::world::world_asset>();
        asset->asset_type = info.asset_type;
        asset->region_index.reserve(disks.size());

        for (size_t i = 0; i < disks.size(); ++i) {

            const region_disk& disk = disks[i];

            GLT::asset::region::region r{};
            r.id = disk.id;
            r.bounds = disk.bounds;
            r.flags = disk.flags;
            r.is_active = false;
            r.asset = (i < info.dependencies.size()) ? info.dependencies[i] : GLT::asset::handle{};

            asset->region_index.push_back(r);
        }

        // ---- optional settings --------------------------------------------------------------------------------------

        if (const auto settings = reader.get(GLT::asset::world::CHUNK_WORLD_SETTINGS); !settings.empty())
            asset->settings.assign(settings.begin(), settings.end());

        LOG(info, "loaded world [{}] - {} region(s)", info.name, asset->region_index.size());

        return GLT::unique_ref<GLT::asset::i_runtime_asset>(std::move(asset));
    }


    FORCE_INLINE_R std::expected<GLT::unique_ref<GLT::asset::i_runtime_asset>, GLT::asset::load_error>
    world_asset_handler::deserialize_region(const GLT::asset::info& info, GLT::asset::chunk_reader& reader) {

        // ---- required chunks ----------------------------------------------------------------------------------------

        const auto bounds_span = reader.get_as<GLT::AABB>(GLT::asset::region::CHUNK_REGION_BOUNDS);
        VALIDATE(bounds_span.size() == 1, return std::unexpected{ GLT::asset::load_error::corrupt_header }, "",
            "[{}] missing or malformed bounds chunk (got {})", info.name, bounds_span.size())

        // ---- build the runtime asset --------------------------------------------------------------------------------

        auto asset = std::make_unique<GLT::asset::region::region_asset>();
        asset->asset_type = info.asset_type;
        asset->bounds = bounds_span[0];

        // ---- optional entity blob -----------------------------------------------------------------------------------

        if (const auto blob = reader.get(GLT::asset::region::CHUNK_REGION_ENTITIES); !blob.empty()) {

            VALIDATE(blob.size() >= sizeof(entity_blob_header),
                return std::unexpected{ GLT::asset::load_error::corrupt_header }, "",
                "[{}] entity chunk too small for header ({} bytes)", info.name, blob.size())

            entity_blob_header hdr;
            std::memcpy(&hdr, blob.data(), sizeof(hdr));

            asset->entity_codec = hdr.codec;
            asset->entity_data.assign(blob.begin() + sizeof(entity_blob_header), blob.end());

            // A codec id of NONE with a non-empty payload is almost certainly a
            // writer bug - the plugin will skip the blob on activate() and the
            // region will look empty. Warn so it's visible.
            if (hdr.codec == CODEC_NONE && !asset->entity_data.empty())
                LOG(warn, "[{}] entity payload present ({} bytes) but codec is NONE - blob will be ignored",
                    info.name, asset->entity_data.size());
        }

        LOG(info, "loaded region [{}] - codec {}, {} entity byte(s)", info.name, asset->entity_codec, asset->entity_data.size());

        return GLT::unique_ref<GLT::asset::i_runtime_asset>(std::move(asset));
    }


    FORCE_INLINE_R std::expected<void, GLT::asset::load_error>
    world_asset_handler::serialize_world(const GLT::asset::info& /*info*/, const GLT::asset::world::world_asset& asset, 
        GLT::asset::asset_writer& out) const {

        // One region_disk per entry in region_index, same order as the deps the
        // registry already re-declared in save(). Positional alignment is what
        // deserialize_world() relies on, so this loop is symmetric with it.
        std::vector<region_disk> disks(asset.region_index.size());

        for (size_t i = 0; i < asset.region_index.size(); ++i) {
            const auto& r = asset.region_index[i];
            disks[i].id     = r.id;
            disks[i].bounds = r.bounds;
            disks[i].flags  = r.flags;
        }

        write_chunk_span(out, GLT::asset::world::CHUNK_WORLD_REGIONS, std::span<const region_disk>(disks));

        if (!asset.settings.empty())
            out.write_chunk(GLT::asset::world::CHUNK_WORLD_SETTINGS, std::span<const std::byte>(asset.settings));

        return {};
    }


    FORCE_INLINE_R std::expected<void, GLT::asset::load_error>
    world_asset_handler::serialize_region(const GLT::asset::info& /*info*/, const GLT::asset::region::region_asset& asset, 
        GLT::asset::asset_writer& out) const {

        write_chunk_as(out, GLT::asset::region::CHUNK_REGION_BOUNDS, asset.bounds);

        if (asset.entity_codec != CODEC_NONE && !asset.entity_data.empty()) {

            // blob = [entity_blob_header][payload]
            std::vector<std::byte> blob(sizeof(entity_blob_header) + asset.entity_data.size());

            entity_blob_header hdr{};
            hdr.codec = asset.entity_codec;
            std::memcpy(blob.data(), &hdr, sizeof(hdr));
            std::memcpy(blob.data() + sizeof(hdr), asset.entity_data.data(), asset.entity_data.size());

            out.write_chunk(GLT::asset::region::CHUNK_REGION_ENTITIES, blob);
        }

        return {};
    }

}
