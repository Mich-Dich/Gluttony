#pragma once

#include "asset/type.h"

// Procedural construction in the running engine: the construct asset type and the Construct editor.
//
// The application calls install() once the plugins of the application_ready phase are loaded and remove() before
// plugins unload (the editor windows hold results made by pack code, so they close while that code is still loaded).
//
// install() registers:
//   - the asset type "construct" (".glt_construct") with its handler, plus the legacy asset types the loaded packs name
//     (pack_desc::legacy_asset_types, same file format),
//   - through an editor hook (works whether the editor loads before or after): the Tools menu entry
//     "Construct/Construct editor", the Construct editor as the asset editor of those types, and a "Construct" entry in
//     the Create Asset dialog.
// Packs need nothing else: their stage types, roles, guide templates, presets, upgrade and self test functions are read
// from the registry by the generic editor.



namespace GLT::construct {

    // FUNCTION DECLARATION ============================================================================================

    void install_engine_integration();
    void remove_engine_integration();

    // The ".glt_construct" asset type (core_types::invalid before install)
    [[nodiscard]] GLT::asset::type construct_asset_type() noexcept;

    // Opens a new, unsaved Construct editor. False without an editor
    bool open_construct_editor();

}
