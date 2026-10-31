
#include "util/pch.h"
#include "component_registry.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::component_registry {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct catalog {

        std::vector<component::component_type_meta>                     all;
        std::unordered_map<u64, size_t>                                 by_hash;
    };

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    catalog& get_catalog() noexcept;

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    catalog& get_catalog() noexcept {

        static catalog c;
        return c;
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    void register_meta(const component::component_type_meta& meta) {

        auto& c = get_catalog();
        if (const auto it = c.by_hash.find(meta.hash); it != c.by_hash.end()) {
            c.all[it->second] = meta;                                   // replace, preserve order
            return;
        }
        c.by_hash[meta.hash] = c.all.size();
        c.all.push_back(meta);
    }


    void unregister_meta(u64 hash) {

        auto& c = get_catalog();
        const auto it = c.by_hash.find(hash);
        if (it == c.by_hash.end())
            return;

        const size_t idx = it->second;
        c.all.erase(c.all.begin() + static_cast<ptrdiff_t>(idx));
        c.by_hash.erase(it);

        // Re-index everything after the erased slot
        for (size_t i = idx; i < c.all.size(); ++i)
            c.by_hash[c.all[i].hash] = i;
    }


    const component::component_type_meta* find(u64 hash) {

        const auto& c = get_catalog();
        const auto it = c.by_hash.find(hash);
        return it == c.by_hash.end() ? nullptr : &c.all[it->second];
    }


    const component::component_type_meta* find(std::string_view name) {

        for (const auto& m : get_catalog().all)
            if (m.name == name)
                return &m;
        return nullptr;
    }


    std::span<const component::component_type_meta> all() { return get_catalog().all; }


    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
