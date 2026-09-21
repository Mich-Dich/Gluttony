
#pragma once

#include "util/event/file_event.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor::file_watcher {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    void init();


    void shutdown();


    // Watch a directory. If `recursive` is true, subdirectories are watched too,
    // including new ones created later. Returns true on success.
    bool watch(const std::filesystem::path& directory, bool recursive = false);


    // Stop watching a directory (and all subdirectories if it was recursive).
    void unwatch(const std::filesystem::path& directory);


    // Stop watching everything.
    void unwatch_all();


    [[nodiscard]] bool is_watching(const std::filesystem::path& directory);

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
