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


    [[nodiscard]] bool asset_writer_impl::has_dependency(UUID id) const noexcept {

        for (const auto& d : m_deps)
            if (d.id == id) 
                return true;
        return false;
    }


    void asset_writer_impl::declare_dependency(const UUID id) {

        if (id == 0 || has_dependency(id))
            return;
        record_dep dep{}; dep.id = id; dep.by_path = false;
        m_deps.push_back(std::move(dep));
    }


    void asset_writer_impl::declare_dependency(std::string_view vp, GLT::asset::type t) {

        // path-only deps have no UUID to compare; leave them as-is
        record_dep dep{};
        dep.virtual_path = std::string(vp);
        dep.target_type = t;
        dep.by_path = true;
        m_deps.push_back(std::move(dep));
    }


    void asset_writer_impl::declare_dependency(const UUID id, std::string_view vp, GLT::asset::type t) {

        if (has_dependency(id))
            return;
        record_dep dep{};
        dep.id = id;
        dep.virtual_path = std::string(vp);
        dep.target_type = t;
        dep.by_path = !vp.empty();
        m_deps.push_back(std::move(dep));
    }


    void asset_writer_impl::set_name(std::string_view name) { m_name.assign(name.begin(), name.end()); }


    [[nodiscard]] const std::string& asset_writer_impl::name() const noexcept { return m_name; }


    [[nodiscard]] const std::vector<asset_writer_impl::record_chunk>& asset_writer_impl::chunks() const noexcept { return m_chunks; }


    [[nodiscard]] const std::vector<asset_writer_impl::record_dep>& asset_writer_impl::deps() const noexcept { return m_deps; }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
