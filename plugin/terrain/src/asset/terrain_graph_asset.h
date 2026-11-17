
#pragma once

#include <asset/i_asset_handler.h>

#include "graph/graph.h"



namespace GLT::terrain {

    // CONSTANTS =======================================================================================================

    // Asset type names (reserved at load; the file extension is "glt_" + name)
    inline constexpr std::string_view                   TERRAIN_GRAPH_TYPE_NAME = "terrain_graph";

    inline constexpr GLT::asset::chunk_id               CHUNK_GRAPH = 0x48505247;      // "GRPH": terrain_graph, binary
    inline constexpr GLT::asset::chunk_id               CHUNK_EDITOR = 0x54444547;     // "GEDT": VisualNodeSystem JSON (T2)
    inline constexpr GLT::asset::chunk_id               CHUNK_PREVIEW = 0x56525047;    // "GPRV": preview settings

    // TYPES ===========================================================================================================

    // @brief Preview / map settings stored with the graph (editor state that belongs to the asset, not to a world)
    struct preview_settings {

        u64                                             seed = 1234;
        glm::dvec2                                      domain_min{ -4096.0 };       // world xz of the explored area
        glm::dvec2                                      domain_max{ 4096.0 };
        f64                                             spacing = 4.0;              // preview sample spacing
    };

    // CLASS DECLARATION ===============================================================================================

    // @brief The runtime form of a .glt_terrain_graph file
    //
    // [graph] is shared and immutable once published: an edit builds a new graph and swaps the pointer (revision++), so
    // evaluations in flight keep a consistent snapshot
    class terrain_graph_asset final : public GLT::asset::i_runtime_asset {
    public:

        explicit terrain_graph_asset(GLT::asset::type type) : m_type(type) {}

        [[nodiscard]] GLT::asset::type type() const noexcept override { return m_type; }
        [[nodiscard]] u64 memory_usage() const noexcept override;

        [[nodiscard]] std::shared_ptr<const terrain_graph> graph() const;
        void set_graph(terrain_graph graph);
        [[nodiscard]] u64 revision() const noexcept { return m_revision.load(std::memory_order_acquire); }

        std::string                                     editor_json{};      // CHUNK_EDITOR, opaque to the runtime
        preview_settings                                preview{};

    private:

        GLT::asset::type                                m_type;
        mutable std::mutex                              m_mutex{};
        std::shared_ptr<const terrain_graph>            m_graph = std::make_shared<const terrain_graph>();
        std::atomic<u64>                                m_revision{ 1 };
    };


    // @brief Asset handler of terrain graphs. Not a plugin of its own: the terrain plugin owns and registers it
    class terrain_graph_handler final : public GLT::asset::i_asset_handler {
    public:

        void set_type(GLT::asset::type type) noexcept { m_types[0] = type; }
        [[nodiscard]] GLT::asset::type type() const noexcept { return m_types[0]; }

        void on_load() override {}
        void on_unload() override {}

        [[nodiscard]] std::span<const GLT::asset::type> types() const noexcept override { return m_types; }

        [[nodiscard]] std::expected<GLT::unique_ref<GLT::asset::i_runtime_asset>, GLT::asset::load_error> deserialize(
            const GLT::asset::info& info, GLT::asset::chunk_reader& reader) override;

        [[nodiscard]] std::expected<void, GLT::asset::load_error> serialize(const GLT::asset::info& info,
            const GLT::asset::i_runtime_asset& asset, GLT::asset::asset_writer& out) const override;

    private:

        GLT::asset::type                                m_types[1]{};
    };

}
