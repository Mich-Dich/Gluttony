#include "util/pch.h"

#include "procedural/construct/engine/construct_asset.h"



namespace GLT::construct {

    // TYPES ===========================================================================================================

    namespace {

        // CHUNK_CONSTRUCT_PREVIEW layout (version 1)
        struct preview_disk {
            u32                                         version = 1;
            u32                                         show_guides = 1;
            u64                                         seed = 1;
            f32                                         target[3]{};
            f32                                         distance = 0.0f;
            f32                                         yaw = 0.7f;
            f32                                         pitch = -0.45f;
        };
        static_assert(std::is_trivially_copyable_v<preview_disk>);

    }

    // CLASS IMPLEMENTATION ============================================================================================

    u64 construct_asset::memory_usage() const noexcept {

        std::lock_guard lock(m_mutex);
        u64 bytes = sizeof(*this);
        if (m_data)
            for (const guide& g : m_data->guides)
                bytes += sizeof(guide) + g.edits.size() * sizeof(edit_step) + g.attributes.entries().size() * 64;
        return bytes;
    }


    std::shared_ptr<const construct_data> construct_asset::data() const {

        std::lock_guard lock(m_mutex);
        return m_data;
    }


    void construct_asset::set_data(construct_data data) {

        auto next = std::make_shared<const construct_data>(std::move(data));
        {
            std::lock_guard lock(m_mutex);
            m_data = std::move(next);
        }
        m_revision.fetch_add(1, std::memory_order_acq_rel);
    }

    // handler ---------------------------------------------------------------------------------------------------------

    std::expected<GLT::unique_ref<GLT::asset::i_runtime_asset>, GLT::asset::load_error> construct_asset_handler::deserialize(
        const GLT::asset::info& info, GLT::asset::chunk_reader& reader) {

        auto asset = GLT::create_unique_ref<construct_asset>(info.asset_type);

        if (reader.has(CHUNK_CONSTRUCT)) {
            auto data = construct::deserialize(reader.get(CHUNK_CONSTRUCT));
            if (!data) {
                LOG(error, "construct [{}]: {}", info.name, data.error())
                return std::unexpected(GLT::asset::load_error::corrupt_header);
            }
            asset->set_data(std::move(*data));
        } else {
            LOG(warn, "construct [{}] has no construct chunk: loaded empty", info.name)
        }

        if (const auto preview = reader.get(CHUNK_CONSTRUCT_PREVIEW); preview.size() >= sizeof(preview_disk)) {
            preview_disk disk{};
            std::memcpy(&disk, preview.data(), sizeof(disk));
            if (disk.version == 1) {
                asset->preview.seed = disk.seed;
                asset->preview.show_guides = disk.show_guides != 0;
                asset->preview.camera_target = glm::vec3(disk.target[0], disk.target[1], disk.target[2]);
                asset->preview.camera_distance = disk.distance;
                asset->preview.camera_yaw = disk.yaw;
                asset->preview.camera_pitch = disk.pitch;
            }
        }

        if (const auto exported = reader.get(CHUNK_CONSTRUCT_EXPORT); !exported.empty())
            asset->preview.export_path = std::filesystem::path(std::string(reinterpret_cast<const char*>(exported.data()), exported.size()));

        LOG(trace, "loaded construct [{}]: {} guide(s)", info.name, asset->data()->guides.size())
        return asset;
    }


    std::expected<void, GLT::asset::load_error> construct_asset_handler::serialize(const GLT::asset::info& /*info*/,
        const GLT::asset::i_runtime_asset& asset, GLT::asset::asset_writer& out) const {

        if (!handles(asset.type()))
            return std::unexpected(GLT::asset::load_error::no_handler);
        const auto& construct = static_cast<const construct_asset&>(asset);

        const std::vector<std::byte> bytes = construct::serialize(*construct.data());
        out.write_chunk(CHUNK_CONSTRUCT, bytes);

        preview_disk disk{};
        disk.seed = construct.preview.seed;
        disk.show_guides = construct.preview.show_guides ? 1u : 0u;
        disk.target[0] = construct.preview.camera_target.x;
        disk.target[1] = construct.preview.camera_target.y;
        disk.target[2] = construct.preview.camera_target.z;
        disk.distance = construct.preview.camera_distance;
        disk.yaw = construct.preview.camera_yaw;
        disk.pitch = construct.preview.camera_pitch;
        out.write_chunk(CHUNK_CONSTRUCT_PREVIEW, std::as_bytes(std::span(&disk, 1)));
        if (!construct.preview.export_path.empty()) {
            const std::string path = construct.preview.export_path.generic_string();
            out.write_chunk(CHUNK_CONSTRUCT_EXPORT, std::as_bytes(std::span(path.data(), path.size())));
        }
        return {};
    }

}
