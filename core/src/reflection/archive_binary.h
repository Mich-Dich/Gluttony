#pragma once

#include <glm/glm.hpp>

#include "util/pch.h"
#include "util/data_structures/UUID.h"
#include "world/entity.h"
#include "asset/type.h"
#include "reflection/registry.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::reflect {

    // CONSTANTS =======================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Binary serializer used by script snapshots and world save files
    //
    // Layout (stable across compiler runs for a given architecture):
    //   - arithmetic / bool / enum          raw bytes (sizeof)
    //   - std::string                       u32 length + bytes (no terminator)
    //   - std::filesystem::path             as std::string (UTF-8)
    //   - GLT::UUID                         16 raw bytes (memcpy)
    //   - GLT::asset::handle                raw bytes (memcpy); register a
    //                                       serialization_override if the layout
    //                                       changes across engine versions
    //   - GLT::world::entity_id             8 raw bytes (index, generation)
    //   - glm::vec2/3/4, quat, mat3/4       raw bytes (memcpy)
    //   - std::vector<std::byte>            u32 length + raw bytes (special:
    //                                       treated as an opaque blob)
    //   - std::vector<T>, std::array<T,N>   u32 count + elements
    //   - std::map<K,V>, unordered_map      u32 count + (key, value) pairs
    //   - std::variant<...>                 u8 index + active alternative
    //   - aggregate structs                 members in declaration order
    //
    // [write_or_skip] serializes a value into a rollback buffer and writes a
    // zero-length blob on failure. Scripts use this to store a "source default"
    // marker for fields whose serializer threw, so restore_state can fall back
    class binary_writer {
    public:

        binary_writer() = default;

        explicit binary_writer(std::vector<std::byte>&& initial)
            : m_buffer(std::move(initial)) {}

        template <typename T>
        void write(const T& value);

        template <typename T>
        bool write_or_skip(const T& value);

        void write_raw(const void* data, std::size_t size);

        void write_blob(std::span<const std::byte> blob);

        [[nodiscard]] const std::vector<std::byte>& data() const noexcept { return m_buffer; }

        [[nodiscard]] std::vector<std::byte>&& take() && noexcept { return std::move(m_buffer); }

        [[nodiscard]] std::size_t size() const noexcept { return m_buffer.size(); }

    private:

        template <typename T>
        void write_aggregate(const T& value);

        std::vector<std::byte> m_buffer;
    };


    // @brief Reader for [binary_writer] output. All reads are bounds-checked;
    // once [ok()] returns false, further reads are no-ops. [read_blob] returns
    // a span into the original input buffer, valid until the reader's backing
    // span expires
    class binary_reader {
    public:

        explicit binary_reader(std::span<const std::byte> data) noexcept
            : m_data(data) {}

        template <typename T>
        bool read(T& out);

        bool read_raw(void* out, std::size_t size);

        bool read_blob(std::span<const std::byte>& out);

        [[nodiscard]] bool ok() const noexcept { return !m_error; }

        [[nodiscard]] bool at_end() const noexcept { return m_cursor >= m_data.size(); }

        [[nodiscard]] std::size_t remaining() const noexcept { return m_data.size() - m_cursor; }

    private:

        template <typename T>
        bool read_aggregate(T& out);

        std::span<const std::byte>  m_data;
        std::size_t                 m_cursor = 0;
        bool                        m_error = false;
    };

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    template <typename T>
    struct is_std_vector : std::false_type {};

    template <typename T>
    struct is_std_array : std::false_type {};

    template <typename T>
    struct is_std_map : std::false_type {};

    template <typename T>
    struct is_std_unordered_map : std::false_type {};

    template <typename T>
    struct is_std_variant : std::false_type {};

    template <typename T>
    struct is_byte_vector : std::false_type {};

    template <typename T, typename A>
    struct is_std_vector<std::vector<T, A>> : std::true_type {};

    template <typename T, std::size_t N>
    struct is_std_array<std::array<T, N>> : std::true_type {};

    template <typename K, typename V, typename C, typename A>
    struct is_std_map<std::map<K, V, C, A>> : std::true_type {};

    template <typename K, typename V, typename H, typename E, typename A>
    struct is_std_unordered_map<std::unordered_map<K, V, H, E, A>> : std::true_type {};

    template <typename... Ts>
    struct is_std_variant<std::variant<Ts...>> : std::true_type {};

    template <typename A>
    struct is_byte_vector<std::vector<std::byte, A>> : std::true_type {};

    // Internal-only — kept in the header so template specializations are
    // reachable from [write] / [read] bodies

    template <typename T>
    struct is_leaf_serializable : std::false_type {};

    // MACROS ==========================================================================================================

    #define GLT_REFLECT_LEAF(TYPE) template <> struct is_leaf_serializable<TYPE> : std::true_type {};
    GLT_REFLECT_LEAF(bool)
    GLT_REFLECT_LEAF(u8)  GLT_REFLECT_LEAF(u16) GLT_REFLECT_LEAF(u32) GLT_REFLECT_LEAF(u64)
    GLT_REFLECT_LEAF(i8)  GLT_REFLECT_LEAF(i16) GLT_REFLECT_LEAF(i32) GLT_REFLECT_LEAF(i64)
    GLT_REFLECT_LEAF(f32) GLT_REFLECT_LEAF(f64)
    GLT_REFLECT_LEAF(std::string)
    GLT_REFLECT_LEAF(std::filesystem::path)
    GLT_REFLECT_LEAF(GLT::UUID)
    GLT_REFLECT_LEAF(GLT::world::entity_id)
    GLT_REFLECT_LEAF(GLT::asset::handle)
    GLT_REFLECT_LEAF(glm::vec2)
    GLT_REFLECT_LEAF(glm::vec3)
    GLT_REFLECT_LEAF(glm::vec4)
    GLT_REFLECT_LEAF(glm::quat)
    GLT_REFLECT_LEAF(glm::mat3)
    GLT_REFLECT_LEAF(glm::mat4)
    #undef GLT_REFLECT_LEAF

}

#include "reflection/archive_binary.inl"
