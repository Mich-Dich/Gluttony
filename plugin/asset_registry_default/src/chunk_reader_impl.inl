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

    chunk_reader_impl::chunk_reader_impl(std::span<const std::byte> bytes, std::span<const chunk_entry> table) noexcept
        : m_bytes(bytes), m_chunks(table) {}


    bool chunk_reader_impl::has(chunk_id id) const noexcept { return find(id) != nullptr; }


    std::span<const std::byte> chunk_reader_impl::get(chunk_id id) const {

        const auto* e = find(id);
        if (!e) 
            return {};
        if (e->offset + e->size_on_disk > m_bytes.size()) 
            return {};    // truncation guard

        auto raw = m_bytes.subspan(e->offset, e->size_on_disk);
        // TODO: dispatch on e->compression (0 == raw). Decompression layer
        // would alloc into a scratch vector and return a span into it.
        return raw;
    }


    std::span<const chunk_id> chunk_reader_impl::available() const noexcept {

        m_id_cache.clear();
        m_id_cache.reserve(m_chunks.size());
        for (const auto& c : m_chunks) 
            m_id_cache.push_back(c.id);
        return m_id_cache;
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    const chunk_entry* chunk_reader_impl::find(chunk_id id) const noexcept {

        for (const auto& c : m_chunks) 
            if (c.id == id) 
                return &c;
        return nullptr;
    }

}
