
#pragma once

#include <editor/i_editor.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {
    class base_window;
}

namespace GLT::editor::extension_host {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Everything plugins registered through GLT::editor::i_editor (core/src/editor/i_editor.h). The editor plugin implements
    // i_editor by forwarding here; editor_layer reads the menu entries and creators and drains the opened windows.
    // Main thread only, like the rest of the editor

    struct menu_entry {

        std::string                                     owner;
        std::string                                     path;           // "Terrain/Open Node Canvas"
        std::function<void()>                           action;
    };


    struct creator_entry {

        std::string                                     owner;
        GLT::editor::asset_creator                      creator;
    };


    struct pending_window {

        std::string                                     owner;
        GLT::unique_ref<GLT::editor::i_editor_window>   content;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // registration (i_editor) -----------------------------------------------------------------------------------------

    void add_asset_editor(std::string_view owner, GLT::asset::type type, GLT::editor::window_factory factory);

    void add_asset_creator(std::string_view owner, GLT::editor::asset_creator creator);

    void add_menu_item(std::string_view owner, std::string_view path, std::function<void()> action);

    void queue_window(std::string_view owner, GLT::unique_ref<GLT::editor::i_editor_window> window);

    // Removes the owner's registrations and pending windows, then asks the editor layer to close its open windows (see
    // set_close_windows_callback)
    void remove_owner(std::string_view owner);

    // queries (editor) ------------------------------------------------------------------------------------------------

    [[nodiscard]] const std::vector<menu_entry>& menu_items();

    [[nodiscard]] const std::vector<creator_entry>& asset_creators();

    [[nodiscard]] const GLT::editor::asset_creator* find_creator(GLT::asset::type type);

    // Windows opened since the last call, wrapped into editor windows (extension_window)
    [[nodiscard]] std::vector<GLT::unique_ref<base_window>> take_pending_windows();

    // editor_layer installs this: destroys every open extension_window of [owner] immediately
    void set_close_windows_callback(std::function<void(std::string_view owner)> callback);

    // Drops everything (editor shutdown)
    void clear();

    // File extension of an asset type, with "glt_" prefix and without the dot. Works for core and plugin types
    [[nodiscard]] std::string extension_of(GLT::asset::type type);

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
