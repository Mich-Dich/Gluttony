#pragma once

#include "reflection/archive_binary.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::reflect {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // Private helper for variant dispatch
    template <typename Variant, std::size_t... Is>
    bool read_variant_index(Variant& out, u8 index, std::index_sequence<Is...>) {

        bool matched = false;
        // Fold expression calls read_variant_one for each index; the matching one stores its value into out
        (read_variant_one<Variant, Is>(out, index, matched), ...);
        if (!matched)
            m_error = true;
        return matched;
    }


    template <typename Variant, std::size_t I>
    void read_variant_one(Variant& out, u8 target_index, bool& matched) {

        if (matched || target_index != I)
            return;
        using Alt = std::variant_alternative_t<I, Variant>;
        Alt alt{};
        if (read(alt)) {
            out = std::move(alt);
            matched = true;
        }
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // writer ----------------------------------------------------------------------------------------------------------

    inline void binary_writer::write_raw(const void* data, std::size_t size) {

        if (size == 0)
            return;
        const auto* p = static_cast<const std::byte*>(data);
        m_buffer.insert(m_buffer.end(), p, p + size);
    }


    inline void binary_writer::write_blob(std::span<const std::byte> blob) {

        const auto count = static_cast<u32>(blob.size());
        write_raw(&count, sizeof(u32));
        write_raw(blob.data(), blob.size());
    }


    template <typename T>
    void binary_writer::write(const T& value) {

        using U = std::remove_cvref_t<T>;

        if constexpr (std::is_arithmetic_v<U> || std::is_enum_v<U>) {

            write_raw(&value, sizeof(U));

        } else if constexpr (std::is_same_v<U, std::string>) {

            const auto len = static_cast<u32>(value.size());
            write_raw(&len, sizeof(u32));
            write_raw(value.data(), value.size());

        } else if constexpr (std::is_same_v<U, std::filesystem::path>) {

            const auto s = value.string();
            write(std::string_view{ s });
            // NOTE: the above calls into the std::string branch on purpose. We inline it here explicitly to avoid a second overload:

        } else if constexpr (is_leaf_serializable<U>::value) {

            write_raw(&value, sizeof(U));

        } else if constexpr (is_byte_vector<U>::value) {

            write_blob(std::span<const std::byte>(value.data(), value.size()));

        } else if constexpr (is_std_vector<U>::value) {

            const auto count = static_cast<u32>(value.size());
            write_raw(&count, sizeof(u32));
            for (const auto& item : value)
                write(item);

        } else if constexpr (is_std_array<U>::value) {

            for (const auto& item : value)
                write(item);

        } else if constexpr (is_std_map<U>::value || is_std_unordered_map<U>::value) {

            const auto count = static_cast<u32>(value.size());
            write_raw(&count, sizeof(u32));
            for (const auto& [k, v] : value) {
                write(k);
                write(v);
            }

        } else if constexpr (is_std_variant<U>::value) {

            const auto index = static_cast<u8>(value.index());
            write_raw(&index, sizeof(u8));
            std::visit([this](const auto& alt) { write(alt); }, value);

        } else if constexpr (std::is_class_v<U>) {

            if (auto* ov = serializer_override(hash_of<U>())) {
                ov->write(&value, this);
                return;
            }
            write_aggregate(value);

        } else
            static_assert(sizeof(U) == 0, "binary_writer: type is not serializable");
    }


    template <typename T>
    bool binary_writer::write_or_skip(const T& value) {

        const auto start = m_buffer.size();
        try {
            write(value);
            return true;
        } catch (...) {
            m_buffer.resize(start);
            write_blob({});                 // zero-length blob = "source default"
            return false;
        }
    }


    template <typename T>
    void binary_writer::write_aggregate(const T& value) {

        template for (constexpr auto member : std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()))) {

            write(value.[:member:]);
        }
    }


    // reader ----------------------------------------------------------------------------------------------------------

    inline bool binary_reader::read_raw(void* out, std::size_t size) {

        if (m_error)
            return false;

        if (m_cursor + size > m_data.size()) {
            m_error = true;
            return false;
        }

        if (size > 0)
            std::memcpy(out, m_data.data() + m_cursor, size);
        m_cursor += size;
        return true;
    }


    inline bool binary_reader::read_blob(std::span<const std::byte>& out) {

        u32 len = 0;
        if (!read_raw(&len, sizeof(u32)))
            return false;

        if (m_cursor + len > m_data.size()) {
            m_error = true;
            return false;
        }

        out = m_data.subspan(m_cursor, len);
        m_cursor += len;
        return true;
    }


    template <typename T>
    bool binary_reader::read(T& out) {

        using U = std::remove_cvref_t<T>;

        if constexpr (std::is_arithmetic_v<U> || std::is_enum_v<U>) {

            return read_raw(&out, sizeof(U));

        } else if constexpr (std::is_same_v<U, std::string>) {

            u32 len = 0;
            if (!read_raw(&len, sizeof(u32)))
                return false;
            if (m_cursor + len > m_data.size()) { m_error = true; return false; }
            out.assign(reinterpret_cast<const char*>(m_data.data() + m_cursor), len);
            m_cursor += len;
            return true;

        } else if constexpr (std::is_same_v<U, std::filesystem::path>) {

            std::string s;
            if (!read(s))
                return false;
            out = std::filesystem::path{ s };
            return true;

        } else if constexpr (is_leaf_serializable<U>::value) {

            return read_raw(&out, sizeof(U));

        } else if constexpr (is_byte_vector<U>::value) {

            std::span<const std::byte> blob;
            if (!read_blob(blob))
                return false;
            out.assign(blob.begin(), blob.end());
            return true;

        } else if constexpr (is_std_vector<U>::value) {

            u32 count = 0;
            if (!read_raw(&count, sizeof(u32)))
                return false;
            out.clear();
            out.reserve(count);
            for (u32 i = 0; i < count; ++i) {
                typename U::value_type item{};
                if (!read(item)) return false;
                out.push_back(std::move(item));
            }
            return true;

        } else if constexpr (is_std_array<U>::value) {

            for (auto& item : out)
                if (!read(item)) return false;
            return true;

        } else if constexpr (is_std_map<U>::value || is_std_unordered_map<U>::value) {

            u32 count = 0;
            if (!read_raw(&count, sizeof(u32)))
                return false;
            out.clear();
            for (u32 i = 0; i < count; ++i) {
                typename U::key_type   k{};
                typename U::mapped_type v{};
                if (!read(k) || !read(v)) return false;
                out.emplace(std::move(k), std::move(v));
            }
            return true;

        } else if constexpr (is_std_variant<U>::value) {

            u8 index = 0;
            if (!read_raw(&index, sizeof(u8)))
                return false;
            if (index >= std::variant_size_v<U>) { m_error = true; return false; }
            return read_variant_index(out, index, std::make_index_sequence<std::variant_size_v<U>>{});

        } else if constexpr (std::is_class_v<U>) {

            if (auto* ov = serializer_override(hash_of<U>())) {
                ov->read(&out, this);
                return ok();
            }
            return read_aggregate(out);

        } else
            static_assert(sizeof(U) == 0, "binary_reader: type is not serializable");
    }


    template <typename T>
    bool binary_reader::read_aggregate(T& out) {

        template for (constexpr auto member : std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()))) {

            if (!read(out.[:member:]))
                return false;
        }
        return true;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
