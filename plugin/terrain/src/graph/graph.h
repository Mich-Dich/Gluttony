
#pragma once

#include <terrain/node.h>



namespace GLT::terrain {

    // CONSTANTS =======================================================================================================

    inline constexpr u32                                INVALID_NODE_ID = 0;

    // TYPES ===========================================================================================================

    // @brief One node of a terrain graph: which type, which parameter values. Parameters are stored by name, so a node
    // class that gains or renames parameters still loads (unknown ones are kept, missing ones take the default)
    struct graph_node {

        u32                                             id = INVALID_NODE_ID;
        std::string                                     type;           // node_type::type_name
        std::string                                     label;          // user title, empty = type's display name
        std::map<std::string, param_value>              params{};       // ordered: stable hashing and files
    };


    // @brief from_node.from_pin (an output) -> to_node.to_pin (an input). Pins by name, for the same reason as params
    struct graph_link {

        u32                                             from_node = INVALID_NODE_ID;
        std::string                                     from_pin;
        u32                                             to_node = INVALID_NODE_ID;
        std::string                                     to_pin;

        [[nodiscard]] bool operator==(const graph_link&) const = default;
    };


    // @brief A graph parameter the world can override (terrain component) without editing the graph
    struct exposed_param {

        std::string                                     name;
        f32                                             default_value = 0.0f;
        f32                                             min = 0.0f;
        f32                                             max = 0.0f;
    };


    // @brief The runtime truth of a terrain graph (CHUNK_GRAPH): flat, no editor data
    struct terrain_graph {

        std::vector<graph_node>                         nodes{};
        std::vector<graph_link>                         links{};
        std::vector<exposed_param>                      exposed{};
        u32                                             next_id = 1;

        [[nodiscard]] graph_node* find(u32 id) noexcept;
        [[nodiscard]] const graph_node* find(u32 id) const noexcept;

        // Adds a node of [type] with the type's default parameters (if the type is known). Returns its id
        u32 add_node(std::string type, const node_type* desc = nullptr);

        void remove_node(u32 id);

        // Replaces any link already feeding [to_node].[to_pin] (an input has at most one source)
        void connect(u32 from_node, std::string from_pin, u32 to_node, std::string to_pin);

        // The link feeding an input, or null
        [[nodiscard]] const graph_link* source_of(u32 to_node, std::string_view to_pin) const noexcept;

        // Content hash of one node (type + parameters), not of its inputs
        [[nodiscard]] u64 node_hash(const graph_node& node) const;
    };

    // FUNCTION DECLARATION ============================================================================================

    // CHUNK_GRAPH serialization (little-endian, versioned). serialize never fails; deserialize reports what is wrong
    [[nodiscard]] std::vector<std::byte> serialize_graph(const terrain_graph& graph);

    [[nodiscard]] std::expected<terrain_graph, std::string> deserialize_graph(std::span<const std::byte> bytes);

    [[nodiscard]] u64 hash_param(const param_value& value);

}
