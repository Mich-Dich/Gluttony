#pragma once

#include "settings/engine_settings_store.h"



// FORWARD DECLARATIONS ================================================================================================

// Engine settings: values of the core and of plugins that a project (and later a player) can change, e.g. shadow
// distance or the number of shadow casting lights.
//
// The owner of a group of settings keeps them in a reflected struct and registers it:
//
//      GLT::engine_settings::register_block({ "renderer.vk_rasterizer.visual", "Rendering",
//          GLT::reflect::type_of<settings::visual>(), &m_visual_settings, [this] { on_settings_changed(); } });
//
// From then on the struct holds: the default in the code, overwritten by <project>/config/engine.config (the project's
// values, shipped with the game), overwritten by <project>/config/engine.user.config (changes on this computer). No
// editor is needed for any of that. The editor's Engine Settings window edits both files through store().
namespace GLT::engine_settings {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // Reads both files of the project in [project_dir] (missing files: nothing is set) and updates every registered
    // block. Called by the application once the project is known; calling it again switches to another project
    void load(const std::filesystem::path& project_dir);

    // Writes the layers that changed since they were loaded / saved. Returns false if a file could not be written
    bool save();

    // The file of a layer for the loaded project (empty before load())
    [[nodiscard]] std::filesystem::path file_path(scope target);

    // Registers a group of settings and fills it with the stored values (see store::add)
    bool register_block(block_desc desc);

    // Must be called before the registered instance goes away (plugin unload)
    void unregister_block(std::string_view name);

    // Takes over values that were written into a registered instance directly, as changes on this computer, and saves
    void adopt_live(std::string_view name);

    // Everything else: values per layer, the registered blocks (for a settings UI)
    [[nodiscard]] store& get_store();

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
