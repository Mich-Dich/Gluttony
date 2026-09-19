#pragma once


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::registry_default {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    void asset_writer_impl::write_chunk(GLT::asset::chunk_id id, std::span<const std::byte> data, u32 compression) {

        record_chunk c{};
        c.id          = id;
        c.compression = compression;
        c.bytes.assign(data.begin(), data.end());
        m_chunks.push_back(std::move(c));
    }


    void asset_writer_impl::declare_dependency(const UUID id) {

        record_dep d{};
        d.id       = id;
        d.by_path  = false;
        m_deps.push_back(std::move(d));
    }


    void asset_writer_impl::declare_dependency(std::string_view virtual_path, GLT::asset::type target_type) {

        record_dep d{};
        d.virtual_path = std::string(virtual_path);
        d.target_type  = target_type;
        d.by_path      = true;
        m_deps.push_back(std::move(d));
    }


    void asset_writer_impl::set_name(std::string_view name) { m_name.assign(name.begin(), name.end()); }


    [[nodiscard]] const std::string& asset_writer_impl::name() const noexcept { return m_name; }


    [[nodiscard]] const std::vector<asset_writer_impl::record_chunk>& asset_writer_impl::chunks() const noexcept { return m_chunks; }


    [[nodiscard]] const std::vector<asset_writer_impl::record_dep>& asset_writer_impl::deps() const noexcept { return m_deps; }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
