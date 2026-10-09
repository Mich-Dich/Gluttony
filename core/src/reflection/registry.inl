#pragma once

#include "reflection/registry.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::reflect {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    namespace detail {

        // TYPES =======================================================================================================

        struct migration_entry {
            u16                 from_version;
            migrate_fn          fn;
        };

        // STATIC VARIABLES ============================================================================================

        // INTERNAL TEMPLATE DECLARATION ===============================================================================

        // INTERNAL FUNCTION DECLARATION ===============================================================================

        inline std::unordered_map<u32, serialization_override>& ser_overrides();
        
        
        inline std::unordered_map<u32, editor_draw_override>& edt_overrides();

        // INTERNAL TEMPLATE IMPLEMENTATION ============================================================================

        // INTERNAL FUNCTION IMPLEMENTATION ============================================================================
        
        inline std::unordered_map<u32, serialization_override>& ser_overrides() {
        
            static std::unordered_map<u32, serialization_override> m;
            return m;
        }
        
        
        inline std::unordered_map<u32, editor_draw_override>& edt_overrides() {
        
            static std::unordered_map<u32, editor_draw_override> m;
            return m;
        }

        // migration table -----------------------------------------------------------------------------

        inline std::unordered_map<u32, std::vector<migration_entry>>& migration_table() {

            static std::unordered_map<u32, std::vector<migration_entry>> t;
            return t;
        }

    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    constexpr u32 fnv1a_32(std::string_view s) noexcept {

        u32 h = 2166136261u;
        for (unsigned char c : s) { 
            h ^= c; 
            h *= 16777619u;
        }
        return h;
    }

    // ---- registry singleton -----------------------------------------------------

    inline reflection_registry& registry() {

        static reflection_registry r;
        return r;
    }


    inline void finalize() { registry().finalize(); }


    inline const type_descriptor* type_of(std::string_view name) noexcept { return registry().find(name); }


    inline const type_descriptor* type_of(u32 hash) noexcept { return registry().find(hash); }

    // visitor ---------------------------------------------------------------------------------------------------------

    inline void for_each_member(const type_descriptor& td, void* obj, const member_callback& callback) {

        if (!td.is_struct())
            return;
        auto* base = static_cast<std::byte*>(obj);
        for (const auto& m : td.members) {
            void* member_ptr = m.access ? m.access(obj) : static_cast<void*>(base + m.offset);
            if (!callback(m, member_ptr))
                return;
        }
    }


    inline void register_migration(u32 h, u16 from_v, migrate_fn fn) {
        detail::migration_table()[h].push_back({ from_v, fn });
    }


    inline bool run_migrations(u32 h, u16 from_v, void* obj) {

        auto it = detail::migration_table().find(h);
        if (it == detail::migration_table().end())
            return from_v == 1;
        // Apply in ascending order from `from_v` up to the current version.
        auto& steps = it->second;
        std::sort(steps.begin(), steps.end(), [](auto& a, auto& b){ return a.from_version < b.from_version; });
        for (auto& s : steps) {
            if (s.from_version >= from_v) 
                s.fn(obj, s.from_version);
        }
        return true;
    }


    inline void register_serializer(u32 h, serialization_override o) { detail::ser_overrides()[h] = o; }


    inline void register_editor_draw(u32 h, editor_draw_override o)       { detail::edt_overrides()[h] = o; }


    inline const serialization_override* serializer_override(u32 h) noexcept {

        auto& m = detail::ser_overrides();
        auto it = m.find(h);
        return it == m.end() ? nullptr : &it->second;
    }


    inline const editor_draw_override* editor_draw_override_for(u32 h) noexcept {

        auto& m = detail::edt_overrides();
        auto it = m.find(h);
        return it == m.end() ? nullptr : &it->second;
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // ---- registration API -------------------------------------------------------

    template <typename T>
    void register_type() { registry().add(&type_descriptor_of<T>); }


    template <typename E> requires std::is_enum_v<E>
    void register_enum() { registry().add(&type_descriptor_of<E>); }

    // ---- visitor ----------------------------------------------------------------

    template <typename T>
    void for_each_member(T* obj, const member_callback& callback) {
        if (auto* td = type_of<T>())
            for_each_member(*td, obj, callback);
    }

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    inline void reflection_registry::add(const type_descriptor* td) {

        if (!td || td->hash == INVALID_TYPE_HASH)
            return;
        std::unique_lock lk(m_mutex);
        auto [it, inserted] = m_by_hash.try_emplace(td->hash, td);
        if (inserted) {
            m_by_name.emplace(td->name, td);
            m_all.push_back(td);
        }
    }


    inline void reflection_registry::remove(u32 hash) noexcept {

        std::unique_lock lk(m_mutex);
        auto it = m_by_hash.find(hash);
        if (it == m_by_hash.end())
            return;
        m_by_name.erase(it->second->name);
        m_all.erase(std::remove(m_all.begin(), m_all.end(), it->second), m_all.end());
        m_by_hash.erase(it);
    }


    inline void reflection_registry::finalize() {

        std::unique_lock lk(m_mutex);
        std::sort(m_all.begin(), m_all.end(), [](const type_descriptor* a, const type_descriptor* b){ return a->hash < b->hash; });
        m_finalized = true;
    }


    inline const type_descriptor* reflection_registry::find(u32 hash) const noexcept {

        std::shared_lock lk(m_mutex);
        auto it = m_by_hash.find(hash);
        return it == m_by_hash.end() ? nullptr : it->second;
    }


    inline const type_descriptor* reflection_registry::find(std::string_view name) const noexcept {

        std::shared_lock lk(m_mutex);
        auto it = m_by_name.find(name);
        return it == m_by_name.end() ? nullptr : it->second;
    }


    inline std::span<const type_descriptor* const> reflection_registry::all() const noexcept {

        // Deliberately no lock here - the span is only safe while no registration is happening
        // Consumers call finalize() and then freeze the registry
        return { m_all.data(), m_all.size() };
    }


    inline bool reflection_registry::empty() const noexcept {

        std::shared_lock lk(m_mutex);
        return m_all.empty();
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
