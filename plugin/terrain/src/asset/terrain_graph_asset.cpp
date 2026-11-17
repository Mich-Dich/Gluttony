
#include "util/pch.h"
#include "terrain_graph_asset.h"



namespace GLT::terrain {

    // TYPES ===========================================================================================================

    namespace {

        // CHUNK_PREVIEW layout (version 1)
        struct preview_disk {
            u32                                         version = 1;
            u32                                         _pad = 0;
            u64                                         seed = 0;
            f64                                         domain[4]{};        // min x, min z, max x, max z
            f64                                         spacing = 4.0;
        };
        static_assert(std::is_trivially_copyable_v<preview_disk>);

    }

    // CLASS IMPLEMENTATION ============================================================================================

    u64 terrain_graph_asset::memory_usage() const noexcept {

        std::lock_guard lock(m_mutex);
        u64 bytes = sizeof(*this) + editor_json.size();
        if (m_graph)
            bytes += m_graph->nodes.size() * (sizeof(graph_node) + 64) + m_graph->links.size() * (sizeof(graph_link) + 32);
        return bytes;
    }


    std::shared_ptr<const terrain_graph> terrain_graph_asset::graph() const {

        std::lock_guard lock(m_mutex);
        return m_graph;
    }


    void terrain_graph_asset::set_graph(terrain_graph graph) {

        auto next = std::make_shared<const terrain_graph>(std::move(graph));
        {
            std::lock_guard lock(m_mutex);
            m_graph = std::move(next);
        }
        m_revision.fetch_add(1, std::memory_order_acq_rel);
    }

    // handler ---------------------------------------------------------------------------------------------------------

    std::expected<GLT::unique_ref<GLT::asset::i_runtime_asset>, GLT::asset::load_error> terrain_graph_handler::deserialize(
        const GLT::asset::info& info, GLT::asset::chunk_reader& reader) {

        auto asset = GLT::create_unique_ref<terrain_graph_asset>(m_types[0]);

        if (reader.has(CHUNK_GRAPH)) {
            auto graph = deserialize_graph(reader.get(CHUNK_GRAPH));
            if (!graph) {
                LOG(error, "terrain graph [{}]: {}", info.name, graph.error())
                return std::unexpected(GLT::asset::load_error::corrupt_header);
            }
            asset->set_graph(std::move(*graph));
        } else {
            LOG(warn, "terrain graph [{}] has no graph chunk: loaded empty", info.name)
        }

        if (const auto json = reader.get(CHUNK_EDITOR); !json.empty())
            asset->editor_json.assign(reinterpret_cast<const char*>(json.data()), json.size());

        if (const auto preview = reader.get(CHUNK_PREVIEW); preview.size() >= sizeof(preview_disk)) {
            preview_disk disk{};
            std::memcpy(&disk, preview.data(), sizeof(disk));
            if (disk.version == 1) {
                asset->preview.seed = disk.seed;
                asset->preview.domain_min = glm::dvec2(disk.domain[0], disk.domain[1]);
                asset->preview.domain_max = glm::dvec2(disk.domain[2], disk.domain[3]);
                asset->preview.spacing = disk.spacing > 0.0 ? disk.spacing : 4.0;
            }
        }

        LOG(trace, "loaded terrain graph [{}]: {} node(s), {} link(s)", info.name, asset->graph()->nodes.size(), asset->graph()->links.size())
        return asset;
    }


    std::expected<void, GLT::asset::load_error> terrain_graph_handler::serialize(const GLT::asset::info& /*info*/,
        const GLT::asset::i_runtime_asset& asset, GLT::asset::asset_writer& out) const {

        if (asset.type() != m_types[0])
            return std::unexpected(GLT::asset::load_error::no_handler);
        const auto& terrain = static_cast<const terrain_graph_asset&>(asset);

        const std::vector<std::byte> graph_bytes = serialize_graph(*terrain.graph());
        out.write_chunk(CHUNK_GRAPH, graph_bytes);

        if (!terrain.editor_json.empty())
            out.write_chunk(CHUNK_EDITOR, std::as_bytes(std::span(terrain.editor_json.data(), terrain.editor_json.size())));

        preview_disk disk{};
        disk.seed = terrain.preview.seed;
        disk.domain[0] = terrain.preview.domain_min.x;
        disk.domain[1] = terrain.preview.domain_min.y;
        disk.domain[2] = terrain.preview.domain_max.x;
        disk.domain[3] = terrain.preview.domain_max.y;
        disk.spacing = terrain.preview.spacing;
        out.write_chunk(CHUNK_PREVIEW, std::as_bytes(std::span(&disk, 1)));
        return {};
    }

}
