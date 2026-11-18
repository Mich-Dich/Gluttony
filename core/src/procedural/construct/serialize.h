#pragma once

#include "procedural/construct/evaluate.h"

// Construct data <-> bytes (the payload of construct assets). Engine-independent, versioned, little-endian.
// Names (ids, roles, kinds, attributes) are stored as text, so files do not depend on hashes or registration order.



namespace GLT::construct {

    // CONSTANTS =======================================================================================================

    inline constexpr u32                                CONSTRUCT_DATA_VERSION = 2;          // 2: stages (generators)

    // FUNCTION DECLARATION ============================================================================================

    [[nodiscard]] std::vector<std::byte> serialize(const construct_data& data);
    [[nodiscard]] std::expected<construct_data, std::string> deserialize(std::span<const std::byte> bytes);

    // Value hash of construct data (same content -> same hash). Editors use it to detect real changes
    [[nodiscard]] u64 content_hash(const construct_data& data);

}
