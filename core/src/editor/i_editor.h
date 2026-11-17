
#pragma once

#include "util/pch.h"           // asset/type.h relies on the pch for <expected>, <span>, ...

#include "asset/type.h"
#include "plugin_system/i_plugin.h"
#include "plugin_system/plugin_manager.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {
    class i_world;
}

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // @brief A window a plugin adds to the editor
    //
    // The editor owns the ImGui window around it (Begin / End, docking, the close button, focus tracking); the plugin only
    // draws the content. draw_content() runs inside the editor's ImGui frame, update() once per frame outside of it (also
    // while the window is hidden behind another tab; the first draw_content() can come before the first update())
    class i_editor_window {
    public:

        virtual ~i_editor_window() = default;

        // Shown in the tab / title bar. Read once when the window is opened
        [[nodiscard]] virtual std::string title() const = 0;

        // ImGuiWindowFlags for the editor's Begin() (e.g. ImGuiWindowFlags_MenuBar). Plain int: no ImGui in this header
        [[nodiscard]] virtual int window_flags() const { return 0; }

        // The window's content, between the editor's ImGui::Begin() and End()
        virtual void draw_content(f32 delta_time) = 0;

        virtual void update(f32 /*delta_time*/) {}

        // Return true to close the window (it is destroyed during the editor's next update)
        [[nodiscard]] virtual bool wants_close() const { return false; }

        // Asset editors: the asset this window edits (content-dir relative). Opening the same asset again focuses this
        // window instead of creating a second one
        [[nodiscard]] virtual std::filesystem::path asset_path() const { return {}; }
    };


    // @brief Factory of an asset editor: receives the asset's content-dir relative path. Null = cannot open it
    using window_factory = std::function<GLT::unique_ref<i_editor_window>(const std::filesystem::path& asset_path)>;


    // @brief An asset type the user can create from scratch ("Create Asset" in the content browser)
    struct asset_creator {

        GLT::asset::type                                type{};
        std::string                                     label{};            // tile caption: "Terrain Graph"
        std::string                                     tooltip{};
        std::string                                     default_name{};     // "New Terrain"

        // Builds the asset, registers it at [content_path] (content-dir relative, extension included) and saves it.
        // Returns an error text for the user on failure
        std::function<std::expected<void, std::string>(const std::filesystem::path& content_path)> create{};
    };


    // @brief What plugins can add to the editor. Implemented by the editor plugin (interface::editor_core)
    //
    // Everything a plugin registers is tagged with an [owner] (use GLT_MODULE_NAME). Call unregister_all(owner) in
    // on_unload(): it removes the registrations AND closes the windows the plugin opened, so no editor state points into
    // an unloaded library. All calls: main thread only
    class i_editor {
    public:

        virtual ~i_editor() = default;

        // Opening an asset of [type] (content browser double click, asset pickers, ...) creates a window through [factory]
        virtual void register_asset_editor(std::string_view owner, GLT::asset::type type, window_factory factory) = 0;

        // Adds [creator] to the "Create Asset" dialog
        virtual void register_asset_creator(std::string_view owner, asset_creator creator) = 0;

        // Adds an entry to the editor's "Tools" menu. [menu_path] uses '/' for sub menus: "Terrain/Open Node Canvas"
        virtual void register_menu_item(std::string_view owner, std::string_view menu_path, std::function<void()> action) = 0;

        // Opens [window] in the editor (docked into the main dock space). It is closed by its own close button,
        // by wants_close(), or by unregister_all(owner)
        virtual void open_window(std::string_view owner, GLT::unique_ref<i_editor_window> window) = 0;

        // Opens the editor registered for [type] on [asset_path] (or focuses it if already open)
        virtual void open_asset(GLT::asset::type type, const std::filesystem::path& asset_path) = 0;

        // The world the editor currently edits (the primary session's world). Null when none is loaded
        [[nodiscard]] virtual GLT::world::i_world* edited_world() = 0;

        // Removes everything [owner] registered and closes its windows (destroyed immediately). Call it between frames
        // (on_unload()), never from inside an editor window's update() / draw_content()
        virtual void unregister_all(std::string_view owner) = 0;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // @brief The running editor, or null (shipped game, editor plugin disabled). Cheap: one atomic load + one cast. Do not
    // cache across plugin loads / unloads (plugin_manager::get_generation())
    [[nodiscard]] FORCE_INLINE_R i_editor* get() {

        auto* plugin = GLT::plugin_manager::get_plugin_raw<GLT::plugin_manager::i_plugin>(GLT::plugin_manager::interface::editor_core);
        return plugin ? plugin->as<i_editor>() : nullptr;
    }


    // @brief Load-order independent editor registration
    //
    // Runs [callback] right away if the editor is loaded, and again every time the editor plugin (re)loads. Plugins put
    // their register_*() calls in it instead of calling get() in on_load(), which would miss an editor loaded later.
    // One hook per owner (a second call replaces the first). Call remove_editor_hook(owner) in on_unload()
    void add_editor_hook(std::string_view owner, std::function<void(i_editor&)> callback);

    void remove_editor_hook(std::string_view owner);

    // Editor plugin only: runs every hook (end of its on_load())
    void run_editor_hooks(i_editor& editor);

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
