
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor::io {

    // CONSTANTS =======================================================================================================

    // @var defaultFilters
    // @brief Default file filter set used by file‑dialog functions.
    //
    // Contains three filters:
    // - "Text Files"   (*.txt)
    // - "C++ Files"    (*.cpp;*.h;*.hpp)
    // - "All Files"    (*.*)
    //
    const std::vector<std::pair<std::string, std::string>> default_filters = {

        {"All Files", "*.*"},
        {"C++ Files", "*.cpp;*.h;*.hpp"},
        {"Text Files", "*.txt"}
    };


    const std::vector<std::pair<std::string, std::string>> project_filters = {

        { "Project Files", GLT::config::PROJECT_EXTENTION }
    };

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    using file_dialog_func = std::filesystem::path (*)(const std::string& title, bool select_directory, 
        const std::vector<std::pair<std::string, std::string>>& filters);

    using file_dialog_multi_func = std::vector<std::filesystem::path> (*)(const std::string& title,
        const std::vector<std::pair<std::string, std::string>>& filters);

    struct file_dialog_functions {

        file_dialog_func                        file_dialog;
        file_dialog_multi_func                  file_dialog_multi;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    void install_vfs_functions(const file_dialog_functions& funcs);


    // @brief Displays a file‑ or folder‑selection dialog and returns the chosen path.
    //
    // On **Windows** the modern `IFileOpenDialog` COM interface is used. When `select_directory`
    // is `true`, a folder picker is shown. On **Linux** `QFileDialog` is used; `init_qt()`
    // must have been called beforehand.
    //
    // @param title             Caption of the dialog window (default `"Open"`).
    // @param select_directory  If `true`, the dialog selects a folder instead of a file. Default `false`.
    // @param filters           List of file‑type filters as pairs of `{description, extension_pattern}`. 
    //                          Semicolons in patterns are converted to spaces on Linux. Uses `defaultFilters` if not specified.
    // @return The absolute path of the selected item, or an empty path if the dialog was cancelled.
    [[nodiscard]] std::filesystem::path file_dialog(const std::string& title = "Open", bool selectDirectory = false,
        const std::vector<std::pair<std::string, std::string>>& filters = default_filters);


    // @brief Displays a multi‑file selection dialog and returns all chosen paths.
    //
    // On **Windows** the classic `GetOpenFileNameW` API is used with the `OFN_ALLOWMULTISELECT` flag. 
    // On **Linux** `QFileDialog::getOpenFileNames()` is used; `init_qt()` must have been called first.
    //
    // @param title   Caption of the dialog window (default `"Select file"`).
    // @param filters File‑type filters (same format as in `fileDialog()`). Defaults to `defaultFilters`.
    // @return A vector of absolute paths to the selected files. Empty if the dialog was cancelled.
    [[nodiscard]] std::vector<std::filesystem::path> file_dialog_multi(const std::string& title = "Select file",
        const std::vector<std::pair<std::string, std::string>>& filters = default_filters);

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
