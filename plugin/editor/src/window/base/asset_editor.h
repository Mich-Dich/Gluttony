
#pragma once

#include "window/base/base_window.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Implemented by editor windows that are bound to exactly ONE asset on disk (material viewer, texture viewer, audio viewer, …)
    //
    // The editor_layer uses this to enforce "at most one editor per asset":
    // when an asset_open_event arrives it first walks m_windows looking for an i_asset_editor whose edited_asset_path() 
    // matches the request. If one exists, it is focused; otherwise a fresh editor is spawned
    //
    // Windows that display many assets at once (content browser, outliner, log view, stats, …) MUST NOT implement this interface
    // the layer has no meaningful way to deduplicate them
    class asset_editor : public base_window {
    public:

        using base_window::base_window;


        // Project-content-relative path of the asset this editor is bound to
        // Must be stable for the editor's lifetime - the layer compares it against incoming asset_open_event paths to detect duplicates
        [[nodiscard]] virtual const std::filesystem::path& get_asset_path() const noexcept = 0;

    };

}
