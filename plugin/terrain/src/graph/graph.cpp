
#include "util/pch.h"
#include "graph.h"



namespace GLT::terrain {

    // CONSTANTS =======================================================================================================

    static constexpr u32                                GRAPH_MAGIC = 0x46524754;      // "TGRF"
    static constexpr u32                                GRAPH_VERSION = 1;

    // TYPES ===========================================================================================================

    namespace {

        class byte_writer {
        public:

            template<typename T> requires std::is_trivially_copyable_v<T>
            void put(const T& value) {
                const auto* p = reinterpret_cast<const std::byte*>(&value);
                m_bytes.insert(m_bytes.end(), p, p + sizeof(T));
            }

            void put_string(std::string_view text) {
                put(static_cast<u32>(text.size()));
                const auto* p = reinterpret_cast<const std::byte*>(text.data());
                m_bytes.insert(m_bytes.end(), p, p + text.size());
            }

            [[nodiscard]] std::vector<std::byte> take() { return std::move(m_bytes); }

        private:

            std::vector<std::byte>                      m_bytes{};
        };


        class byte_reader {
        public:

            explicit byte_reader(std::span<const std::byte> bytes) : m_bytes(bytes) {}

            template<typename T> requires std::is_trivially_copyable_v<T>
            [[nodiscard]] bool get(T& out) {
                if (m_offset + sizeof(T) > m_bytes.size())
                    return false;
                std::memcpy(&out, m_bytes.data() + m_offset, sizeof(T));
                m_offset += sizeof(T);
                return true;
            }

            [[nodiscard]] bool get_string(std::string& out) {
                u32 size = 0;
                if (!get(size) || m_offset + size > m_bytes.size())
                    return false;
                out.assign(reinterpret_cast<const char*>(m_bytes.data() + m_offset), size);
                m_offset += size;
                return true;
            }

            [[nodiscard]] size_t remaining() const noexcept { return m_bytes.size() - m_offset; }

        private:

            std::span<const std::byte>                  m_bytes;
            size_t                                      m_offset = 0;
        };


        void put_param(byte_writer& w, const param_value& value) {

            w.put(static_cast<u8>(value.index()));
            std::visit([&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::string>)
                    w.put_string(v);
                else if constexpr (std::is_same_v<T, bool>)
                    w.put(static_cast<u8>(v ? 1 : 0));
                else if constexpr (std::is_same_v<T, glm::vec2>) {
                    w.put(v.x);
                    w.put(v.y);
                } else
                    w.put(v);
            }, value);
        }


        [[nodiscard]] bool get_param(byte_reader& r, param_value& out) {

            u8 index = 0;
            if (!r.get(index))
                return false;
            switch (index) {
                case 0: { f32 v{}; if (!r.get(v)) return false; out = v; return true; }
                case 1: { i32 v{}; if (!r.get(v)) return false; out = v; return true; }
                case 2: { u8 v{}; if (!r.get(v)) return false; out = (v != 0); return true; }
                case 3: { glm::vec2 v{}; if (!r.get(v.x) || !r.get(v.y)) return false; out = v; return true; }
                case 4: { std::string v; if (!r.get_string(v)) return false; out = std::move(v); return true; }
                default: return false;
            }
        }

    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    u64 hash_param(const param_value& value) {

        u64 h = hash_mix(value.index());
        std::visit([&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::string>)
                h = hash_combine(h, hash_string(v));
            else if constexpr (std::is_same_v<T, glm::vec2>) {
                h = hash_combine(h, std::bit_cast<u32>(v.x));
                h = hash_combine(h, std::bit_cast<u32>(v.y));
            } else if constexpr (std::is_same_v<T, f32>)
                h = hash_combine(h, std::bit_cast<u32>(v));
            else
                h = hash_combine(h, static_cast<u64>(v));
        }, value);
        return h;
    }


    std::vector<std::byte> serialize_graph(const terrain_graph& graph) {

        byte_writer w;
        w.put(GRAPH_MAGIC);
        w.put(GRAPH_VERSION);
        w.put(graph.next_id);

        w.put(static_cast<u32>(graph.nodes.size()));
        for (const auto& node : graph.nodes) {
            w.put(node.id);
            w.put_string(node.type);
            w.put_string(node.label);
            w.put(static_cast<u32>(node.params.size()));
            for (const auto& [name, value] : node.params) {
                w.put_string(name);
                put_param(w, value);
            }
        }

        w.put(static_cast<u32>(graph.links.size()));
        for (const auto& link : graph.links) {
            w.put(link.from_node);
            w.put_string(link.from_pin);
            w.put(link.to_node);
            w.put_string(link.to_pin);
        }

        w.put(static_cast<u32>(graph.exposed.size()));
        for (const auto& param : graph.exposed) {
            w.put_string(param.name);
            w.put(param.default_value);
            w.put(param.min);
            w.put(param.max);
        }
        return w.take();
    }


    std::expected<terrain_graph, std::string> deserialize_graph(std::span<const std::byte> bytes) {

        byte_reader r(bytes);
        u32 magic = 0, version = 0;
        if (!r.get(magic) || magic != GRAPH_MAGIC)
            return std::unexpected(std::string("not a terrain graph (bad magic)"));
        if (!r.get(version) || version == 0 || version > GRAPH_VERSION)
            return std::unexpected(std::format("unsupported terrain graph version {}", version));

        terrain_graph graph{};
        const auto truncated = [] { return std::unexpected(std::string("terrain graph data is truncated")); };
        if (!r.get(graph.next_id))
            return truncated();

        // Counts come from the file: never allocate more records than the remaining bytes could hold (every record type
        // is at least 16 bytes), so a corrupt file fails cleanly instead of throwing bad_alloc
        constexpr size_t MIN_RECORD = 16;
        u32 count = 0;
        if (!r.get(count) || count > r.remaining() / MIN_RECORD)
            return truncated();
        graph.nodes.resize(count);
        std::unordered_set<u32> ids;
        for (auto& node : graph.nodes) {
            u32 params = 0;
            if (!r.get(node.id) || !r.get_string(node.type) || !r.get_string(node.label) || !r.get(params))
                return truncated();
            if (node.id == INVALID_NODE_ID || node.id == std::numeric_limits<u32>::max() || !ids.insert(node.id).second)
                return std::unexpected(std::format("terrain graph has an invalid or duplicate node id {}", node.id));
            for (u32 i = 0; i < params; ++i) {
                std::string name;
                param_value value;
                if (!r.get_string(name) || !get_param(r, value))
                    return truncated();
                node.params[std::move(name)] = std::move(value);
            }
            graph.next_id = std::max(graph.next_id, node.id + 1);
        }

        if (!r.get(count) || count > r.remaining() / MIN_RECORD)
            return truncated();
        graph.links.resize(count);
        for (auto& link : graph.links)
            if (!r.get(link.from_node) || !r.get_string(link.from_pin) || !r.get(link.to_node) || !r.get_string(link.to_pin))
                return truncated();

        if (!r.get(count) || count > r.remaining() / MIN_RECORD)
            return truncated();
        graph.exposed.resize(count);
        for (auto& param : graph.exposed)
            if (!r.get_string(param.name) || !r.get(param.default_value) || !r.get(param.min) || !r.get(param.max))
                return truncated();

        return graph;
    }

    // CLASS IMPLEMENTATION ============================================================================================

    graph_node* terrain_graph::find(u32 id) noexcept {
        for (auto& node : nodes)
            if (node.id == id)
                return &node;
        return nullptr;
    }


    const graph_node* terrain_graph::find(u32 id) const noexcept {
        for (const auto& node : nodes)
            if (node.id == id)
                return &node;
        return nullptr;
    }


    u32 terrain_graph::add_node(std::string type, const node_type* desc) {

        graph_node node{};
        node.id = next_id++;
        node.type = std::move(type);
        if (desc)
            for (const auto& param : desc->params)
                node.params[param.name] = param.default_value;
        nodes.push_back(std::move(node));
        return nodes.back().id;
    }


    void terrain_graph::remove_node(u32 id) {

        std::erase_if(nodes, [id](const graph_node& n) { return n.id == id; });
        std::erase_if(links, [id](const graph_link& l) { return l.from_node == id || l.to_node == id; });
    }


    void terrain_graph::connect(u32 from_node, std::string from_pin, u32 to_node, std::string to_pin) {

        std::erase_if(links, [&](const graph_link& l) { return l.to_node == to_node && l.to_pin == to_pin; });
        links.push_back({ from_node, std::move(from_pin), to_node, std::move(to_pin) });
    }


    const graph_link* terrain_graph::source_of(u32 to_node, std::string_view to_pin) const noexcept {
        for (const auto& link : links)
            if (link.to_node == to_node && link.to_pin == to_pin)
                return &link;
        return nullptr;
    }


    u64 terrain_graph::node_hash(const graph_node& node) const {

        u64 h = hash_string(node.type);
        for (const auto& [name, value] : node.params)
            h = hash_combine(hash_combine(h, hash_string(name)), hash_param(value));
        return h;
    }

}
