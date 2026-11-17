
#include "util/pch.h"
#include "extension_host.h"

#include <asset/i_asset_registry.h>

#include "util/asset_editor_registry.h"
#include "window/extension_window.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor::extension_host {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct asset_editor_entry {

        std::string                                     owner;
        GLT::asset::type                                type{};
    };

    // STATIC VARIABLES ================================================================================================

    static std::vector<menu_entry>                      s_menu_items{};
    static std::vector<creator_entry>                   s_creators{};
    static std::vector<asset_editor_entry>              s_asset_editors{};
    static std::vector<pending_window>                  s_pending_windows{};
    static std::function<void(std::string_view)>        s_close_windows{};

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    void add_asset_editor(std::string_view owner, GLT::asset::type type, GLT::editor::window_factory factory) {

        VALIDATE(factory, return, "", "register_asset_editor: [{}] passed an empty factory", (owner))
        // Core types keep their built-in editors: unregistering a plugin override would leave the type without any
        VALIDATE(type.value >= GLT::asset::CUSTOM_TYPE_BEGIN, return, "", "register_asset_editor: [{}] cannot replace the editor of core type [{}]", (owner, type.value))

        // The core asset editors keep their registry; plugin editors go there too, wrapped into an editor window
        GLT::editor::asset_editor_registry::register_editor(type,
            [owner = std::string(owner), factory = std::move(factory)](const std::filesystem::path& path) -> GLT::unique_ref<base_window> {
                auto content = factory(path);
                if (!content)
                    return nullptr;
                return GLT::create_unique_ref<extension_window>(owner, std::move(content));
            });

        std::erase_if(s_asset_editors, [type](const asset_editor_entry& e) { return e.type == type; });
        s_asset_editors.push_back({ std::string(owner), type });
    }


    void add_asset_creator(std::string_view owner, GLT::editor::asset_creator creator) {

        VALIDATE(creator.create, return, "", "register_asset_creator: [{}] passed a creator without create()", (owner))

        std::erase_if(s_creators, [&creator](const creator_entry& e) { return e.creator.type == creator.type; });
        s_creators.push_back({ std::string(owner), std::move(creator) });
    }


    void add_menu_item(std::string_view owner, std::string_view path, std::function<void()> action) {

        VALIDATE(action && !path.empty(), return, "", "register_menu_item: [{}] passed an empty path or action", (owner))

        std::erase_if(s_menu_items, [path](const menu_entry& e) { return e.path == path; });
        s_menu_items.push_back({ std::string(owner), std::string(path), std::move(action) });
        std::stable_sort(s_menu_items.begin(), s_menu_items.end(), [](const menu_entry& a, const menu_entry& b) { return a.path < b.path; });
    }


    void queue_window(std::string_view owner, GLT::unique_ref<GLT::editor::i_editor_window> window) {

        if (!window)
            return;
        s_pending_windows.push_back({ std::string(owner), std::move(window) });
    }


    void remove_owner(std::string_view owner_view) {

        // Copy: the view may point into an entry or window that is erased below.
        // Must not run while the editor iterates its windows (unload plugins between frames)
        const std::string owner(owner_view);
        std::erase_if(s_menu_items, [owner](const menu_entry& e) { return e.owner == owner; });
        std::erase_if(s_creators, [owner](const creator_entry& e) { return e.owner == owner; });
        std::erase_if(s_pending_windows, [owner](const pending_window& w) { return w.owner == owner; });

        for (const auto& entry : s_asset_editors)
            if (entry.owner == owner)
                GLT::editor::asset_editor_registry::unregister_editor(entry.type);
        std::erase_if(s_asset_editors, [owner](const asset_editor_entry& e) { return e.owner == owner; });

        // Windows already in the editor hold code and data of the plugin: they must be gone before it unloads
        if (s_close_windows)
            s_close_windows(owner);
    }


    const std::vector<menu_entry>& menu_items() { return s_menu_items; }


    const std::vector<creator_entry>& asset_creators() { return s_creators; }


    const GLT::editor::asset_creator* find_creator(GLT::asset::type type) {

        for (const auto& entry : s_creators)
            if (entry.creator.type == type)
                return &entry.creator;
        return nullptr;
    }


    std::vector<GLT::unique_ref<base_window>> take_pending_windows() {

        std::vector<GLT::unique_ref<base_window>> out;
        out.reserve(s_pending_windows.size());
        for (auto& pending : s_pending_windows)
            out.push_back(GLT::create_unique_ref<extension_window>(std::move(pending.owner), std::move(pending.content)));
        s_pending_windows.clear();
        return out;
    }


    void set_close_windows_callback(std::function<void(std::string_view owner)> callback) { s_close_windows = std::move(callback); }


    void clear() {

        for (const auto& entry : s_asset_editors)
            GLT::editor::asset_editor_registry::unregister_editor(entry.type);
        s_asset_editors.clear();
        s_menu_items.clear();
        s_creators.clear();
        s_pending_windows.clear();
        s_close_windows = {};
    }


    std::string extension_of(GLT::asset::type type) {

        if (type.value < GLT::asset::CUSTOM_TYPE_BEGIN)
            return GLT::asset::type_to_extension(type);

        if (auto* registry = GLT::asset::registry::get_ptr())
            if (const std::string_view name = registry->name_from_type(type); !name.empty())
                return std::string("glt_") + std::string(name);
        return "glt_unknown";
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
