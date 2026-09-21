
#pragma once

#include <asset/type.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {
    class base_window;
}

namespace GLT::editor::asset_editor_registry {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Factory signature. Return nullptr if the path can't be opened.
    // Receives the asset's virtual path so the window can call into the
    // registry itself if it needs the info/handle.
    using asset_editor_factory = std::function<GLT::unique_ref<base_window>(const std::filesystem::path&)>;

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    void register_editor(GLT::asset::type type, asset_editor_factory factory);


    void unregister_editor(GLT::asset::type type) noexcept;


    [[nodiscard]] bool has_editor(GLT::asset::type type) noexcept;


    // Returns nullptr if no editor is registered for `type`.
    // Otherwise invokes the factory - safe to call every time a
    // user opens an asset; a new window instance is produced.
    [[nodiscard]] GLT::unique_ref<base_window> create(GLT::asset::type type, const std::filesystem::path& path);


    // Snapshot of currently registered types (unordered). Used by
    // tooling - e.g. a "which assets can I open?" debug panel.
    [[nodiscard]] std::vector<GLT::asset::type> registered_types();

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
