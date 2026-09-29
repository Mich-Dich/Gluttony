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

    void asset_writer_impl::write_chunk(GLT::asset::chunk_id id, const std::span<const std::byte> data, u32 compression) {

        record_chunk chunk{};
        chunk.id = id;
        chunk.compression = compression;
        chunk.bytes.assign(data.begin(), data.end());
        m_chunks.push_back(std::move(chunk));
    }


    void asset_writer_impl::declare_dependency(const UUID id) {

        record_dep dep{};
        dep.id = id;
        dep.by_path = false;
        m_deps.push_back(std::move(dep));
    }


    void asset_writer_impl::declare_dependency(std::string_view virtual_path, GLT::asset::type target_type) {

        record_dep dep{};
        dep.virtual_path = std::string(virtual_path);
        dep.target_type = target_type;
        dep.by_path = true;
        m_deps.push_back(std::move(dep));
    }


    void asset_writer_impl::declare_dependency(const UUID id, std::string_view virtual_path, GLT::asset::type target_type) {

        record_dep dep{};
        dep.id = id;
        dep.virtual_path = std::string(virtual_path);
        dep.target_type = target_type;
        dep.by_path = !virtual_path.empty();
        m_deps.push_back(std::move(dep));
    }


    void asset_writer_impl::set_name(std::string_view name) { m_name.assign(name.begin(), name.end()); }


    [[nodiscard]] const std::string& asset_writer_impl::name() const noexcept { return m_name; }


    [[nodiscard]] const std::vector<asset_writer_impl::record_chunk>& asset_writer_impl::chunks() const noexcept { return m_chunks; }


    [[nodiscard]] const std::vector<asset_writer_impl::record_dep>& asset_writer_impl::deps() const noexcept { return m_deps; }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
