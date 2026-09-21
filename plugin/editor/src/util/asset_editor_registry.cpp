
#include "util/pch.h"
#include "asset_editor_registry.h"

#include "window/base_window.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor::asset_editor_registry {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    std::shared_mutex                                               s_mutex{};

    std::unordered_map<GLT::asset::type, asset_editor_factory>      s_factories{};

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    void register_editor(GLT::asset::type type, asset_editor_factory factory) {

        std::unique_lock lock(s_mutex);
        s_factories[type] = std::move(factory);
    }


    void unregister_editor(GLT::asset::type type) noexcept {

        std::unique_lock lock(s_mutex);
        s_factories.erase(type);
    }


    bool has_editor(GLT::asset::type type) noexcept {

        std::shared_lock lock(s_mutex);
        return s_factories.contains(type);
    }


    GLT::unique_ref<base_window> create(GLT::asset::type type, const std::filesystem::path& path) {

        // Copy the factory out under the lock, then invoke it unlocked.
        // This avoids re-entrancy issues if the factory (or the window's constructor) happens to touch the registry.
        asset_editor_factory factory;
        {
            std::shared_lock lock(s_mutex);
            const auto it = s_factories.find(type);
            if (it == s_factories.end())
                return nullptr;
            factory = it->second;
        }

        return factory(path);
    }


    std::vector<GLT::asset::type> registered_types() {

        std::shared_lock lock(s_mutex);
        std::vector<GLT::asset::type> out;
        out.reserve(s_factories.size());
        for (const auto& [t, _] : s_factories)
            out.push_back(t);
        return out;
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
