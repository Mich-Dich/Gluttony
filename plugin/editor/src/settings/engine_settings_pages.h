#pragma once

#include "settings/settings_registry.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // Pages of the Engine Settings window, made from what is registered with GLT::engine_settings: per block one page
    // for each category of its members ("Shadows", "Post Process", ...), under the block's group ("Rendering").
    // A category "Page|Section" puts the member into that section of the page.
    //
    // The rows show and change the values of one file: the project's (engine.config) or the changes on this computer
    // (engine.user.config). Which one is chosen in the sidebar. "Reset" removes the value from that file, so the row
    // falls back to what is below it (project: the engine's default, this computer: the project's value).
    //
    // Clears [registry] first. Call again when engine_settings_signature() changed
    void build_engine_settings_pages(settings_registry& registry);

    // Changes when a block was registered or removed (a plugin was loaded / unloaded)
    [[nodiscard]] u64 engine_settings_signature();

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
