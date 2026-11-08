
#include "util/pch.h"
#include "class_registry.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::scripting_cpp {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // ---- class_registry ---------------------------------------------------------------------------------------------

    bool class_registry::add(script::script_class_info info) {

        std::unique_lock lk(m_mutex);

        for (const auto& e : m_entries) {
            if (e.name == info.name) {
                LOG(warn, "scripting_cpp: duplicate class '{}' — keeping registrant '{}', rejecting '{}'",
                    info.name, e.registrant, info.registrant)
                return false;
            }
        }

        m_entries.push_back(std::move(info));
        return true;
    }


    void class_registry::remove(std::string_view name) {

        std::unique_lock lk(m_mutex);
        std::erase_if(m_entries, [name](const script::script_class_info& e) { return e.name == name; });
    }


    void class_registry::remove_by_registrant(std::string_view registrant) {

        std::unique_lock lk(m_mutex);
        std::erase_if(m_entries, [registrant](const script::script_class_info& e) { return e.registrant == registrant; });
    }


    const script::script_class_info* class_registry::find(std::string_view name) const {

        std::shared_lock lk(m_mutex);
        for (const auto& e : m_entries)
            if (e.name == name)
                return &e;
        return nullptr;
    }


    std::span<const script::script_class_info> class_registry::all() const {

        std::shared_lock lk(m_mutex);
        return { m_entries.data(), m_entries.size() };
    }


    bool class_registry::empty() const {

        std::shared_lock lk(m_mutex);
        return m_entries.empty();
    }


    size_t class_registry::size() const {

        std::shared_lock lk(m_mutex);
        return m_entries.size();
    }

    // ---- global_registry --------------------------------------------------------------------------------------------

    void global_registry::set(std::string_view registrant, std::span<const script::default_global> globals) {

        std::unique_lock lk(m_mutex);
        auto& bucket = m_by_registrant[std::string{ registrant }];
        bucket.assign(globals.begin(), globals.end());
        m_flat_dirty = true;
    }


    void global_registry::clear(std::string_view registrant) {

        std::unique_lock lk(m_mutex);
        m_by_registrant.erase(std::string{ registrant });
        m_flat_dirty = true;
    }


    std::span<const script::default_global> global_registry::all() const {

        // all() may rebuild the cache, so it takes a unique lock. Callers hold the returned span only
        // until the next set / clear / all() call. That matches the interface contract on
        // i_script_plugin::default_globals().
        std::unique_lock lk(m_mutex);

        if (m_flat_dirty) {
            m_flat_cache.clear();
            for (const auto& [registrant, bucket] : m_by_registrant)
                m_flat_cache.insert(m_flat_cache.end(), bucket.begin(), bucket.end());
            m_flat_dirty = false;
        }

        return { m_flat_cache.data(), m_flat_cache.size() };
    }

}
