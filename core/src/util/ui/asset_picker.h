
#pragma once

#include <filesystem>
#include <string_view>

#include "asset/type.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::UI {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct asset_picker_options {

        std::string_view        label;                          // "Mesh", "Material", ...
        GLT::asset::type        filter;                         // selects the on-disk extension
        bool                    allow_clear{ true };
        std::string_view        placeholder{ "(none)" };
        std::filesystem::path   root{};                         // empty -> get_asset_root()
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // Root directory the picker walks. Must be an absolute path - typically PROJECT_CONTENT_DIR. Every file the picker
    // returns is converted to a project-relative path via GLT::project::to_content_relative() before being handed to the registry.
    //
    // Set once at startup by whichever subsystem knows the project layout (editor bootstrap, project manager)
    // Until it's called, the picker falls back to the process CWD, which is almost certainly wrong - call it before any picker is drawn
    void set_asset_root(std::filesystem::path root);


    [[nodiscard]] const std::filesystem::path& get_asset_root();


    // Drop the cached scan for every (root, extension) pair. Call after an
    // import or a manual file drop that added files under the asset root.
    void invalidate_asset_cache();


    void invalidate_asset_cache(const std::filesystem::path& root, std::string_view extension);


    // Draws just the field + button + popup, without a label. Assumes the
    // caller has pushed a unique ID and set the cursor. `available_width`
    // is the horizontal space the widget may occupy (typically
    // ImGui::GetContentRegionAvail().x inside a table cell).
    //
    // Used by both entry points below. If you're rolling your own row layout,
    // this is the piece to call.
    [[nodiscard]] bool asset_picker_widget(const asset_picker_options& opts, GLT::asset::handle& in_out, f32 available_width);


    // Draws a picker bound to `in_out`. Returns true if the selection changed this frame (so callers can snapshot for undo).
    //
    // The picker never asks the registry to enumerate anything. It walks the asset root for files matching extension_for_type(filter),
    // shows them in a popup, and only calls registry->load(path) when the user confirms a pick. 
    // Loading can fail - on failure the handle is left untouched.
    //
    // Scan results are cached per (root, extension) and refreshed on a background thread via thread_pool. 
    // The first frame after a cold cache shows "scanning..." and fills in on the next frame.
    bool asset_picker(const asset_picker_options& opts, GLT::asset::handle& in_out);


    // Table row: emits the label into column 0 and the widget into column 1.
    // Matches the calling convention of GLT::UI::table_row* - you call it between begin_table() / end_table(), one call per row.
    bool table_row_asset_picker(std::string_view label, GLT::asset::handle& in_out, GLT::asset::type filter,
        const char* desc = nullptr, bool allow_clear = true);


    // Convenience overload: the (label, handle, type) form.
    inline bool asset_picker(std::string_view label, GLT::asset::handle& in_out, GLT::asset::type filter) {
    
        return asset_picker({ .label = label, .filter = filter }, in_out);
    }

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
