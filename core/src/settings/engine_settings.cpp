
#include "util/pch.h"
#include "engine_settings.h"

#include "util/io/vfs.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::engine_settings {

    // CONSTANTS =======================================================================================================

    constexpr const char*                       FILE_NAMES[] = { "engine.config", "engine.user.config" };

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    namespace {

        std::filesystem::path                   s_project_dir{};
    }

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    store& get_store() {

        static store s_store{};                 // function-local: plugins may register before any other core state exists
        return s_store;
    }


    std::filesystem::path file_path(const scope target) {

        if (s_project_dir.empty() || target >= scope::count)
            return {};
        return s_project_dir / GLT::config::CONFIG_DIR / FILE_NAMES[static_cast<size_t>(target)];
    }


    void load(const std::filesystem::path& project_dir) {

        s_project_dir = project_dir;
        store& settings = get_store();

        for (const scope target : { scope::project, scope::user }) {

            std::string text{};
            const auto path = file_path(target);
            std::error_code error{};
            if (!path.empty() && GLT::vfs::exists(path, error) && !error) {
                text = GLT::vfs::read_text_file(path, error);
                if (error) {
                    LOG(warn, "engine settings: could not read [{}] ({}), using the values below it", path.generic_string(), error.message())
                    text.clear();
                }
            }
            settings.parse(target, text);       // also for a missing file: the layer is empty then
        }
    }


    bool save() {

        store& settings = get_store();
        bool ok = true;

        for (const scope target : { scope::project, scope::user }) {

            if (!settings.dirty(target))
                continue;

            const auto path = file_path(target);
            if (path.empty()) {
                ok = false;                     // no project loaded: keep the changes in memory
                continue;
            }

            std::error_code error{};
            std::filesystem::create_directories(path.parent_path(), error);
            if (GLT::vfs::write_text_file(path, settings.emit(target)))
                settings.clear_dirty(target);
            else {
                LOG(error, "engine settings: could not write [{}]", path.generic_string())
                ok = false;
            }
        }
        return ok;
    }


    bool register_block(block_desc desc) {

        const std::string name = desc.name;
        const bool added = get_store().add(std::move(desc));
        if (!added)
            LOG(error, "engine settings: block [{}] could not be registered (already registered, or no type / instance)", name)
        return added;
    }


    void unregister_block(const std::string_view name) { get_store().remove(name); }


    void adopt_live(const std::string_view name) {

        store& settings = get_store();
        if (const store::block* owner = settings.find(name))
            if (settings.adopt_live(*owner, scope::user))
                save();
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
