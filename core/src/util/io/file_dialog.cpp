
#include "util/pch.h"
#include "file_dialog.h"

#include <cstdio>
#include <cstdlib>
#include <array>

#if defined(PLATFORM_LINUX)
    #include <sys/wait.h>
#endif



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::io {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    enum class backend : u8 {
        
        none = 0,
        zenity,
        kdialog
    };
    
    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    backend detect_backend();


    // POSIX shell single-quote escaping.
    std::string shell_quote(const std::string& s);


    // Run a command through /bin/sh, capture stdout, return (exit_code, stdout).
    std::pair<int, std::string> run_capture(const std::string& cmd);


    std::vector<std::filesystem::path> split_lines(const std::string& s);


    std::string zenity_filters(const std::vector<std::pair<std::string, std::string>>& filters);


    // kdialog accepts newline-separated entries: "pat1 pat2|Desc1\npat3|Desc2"
    std::string kdialog_filter(const std::vector<std::pair<std::string, std::string>>& filters);


    std::filesystem::path zenity_single(const std::string& title, bool dir, const std::vector<std::pair<std::string, std::string>>& filters);


    std::vector<std::filesystem::path> zenity_multi(const std::string& title, const std::vector<std::pair<std::string, std::string>>& filters);


    std::filesystem::path kdialog_single(const std::string& title, bool dir, const std::vector<std::pair<std::string, std::string>>& filters);


    std::vector<std::filesystem::path> kdialog_multi(const std::string& title, const std::vector<std::pair<std::string, std::string>>& filters);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    backend detect_backend() {

        static const backend cached = []() -> backend {
            if (std::system("command -v kdialog >/dev/null 2>&1") == 0) 
                return backend::kdialog;
                
            if (std::system("command -v zenity  >/dev/null 2>&1") == 0) 
                return backend::zenity;

            return backend::none;
        }();
        return cached;
    }


    std::string shell_quote(const std::string& s) {

        std::string out;
        out.reserve(s.size() + 2);
        out += '\'';
        for (char c : s) {

            if (c == '\'') 
                out += "'\\''";

            else           
                out += c;
        }
        out += '\'';
        return out;
    }


    std::pair<int, std::string> run_capture(const std::string& cmd) {

        const std::string full = cmd + " 2>/dev/null";
        FILE* pipe = ::popen(full.c_str(), "r");
        if (!pipe) 
            return { -1, {} };

        std::string out;
        std::array<char, 4096> buf{};
        size_t n;
        while ((n = std::fread(buf.data(), 1, buf.size(), pipe)) > 0)
            out.append(buf.data(), n);

        const int status = ::pclose(pipe);
        const int code   = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
            out.pop_back();

        return { code, std::move(out) };
    }


    std::vector<std::filesystem::path> split_lines(const std::string& s) {

        std::vector<std::filesystem::path> out;
        size_t start = 0;
        while (start <= s.size()) {

            size_t nl = s.find('\n', start);
            if (nl == std::string::npos) nl = s.size();
            if (nl > start) out.emplace_back(s.substr(start, nl - start));
            if (nl == s.size()) break;
            start = nl + 1;
        }
        return out;
    }


    std::string zenity_filters(const std::vector<std::pair<std::string, std::string>>& filters) {

        std::string out;
        for (const auto& [desc, pattern] : filters) {

            std::string pat = pattern;
            std::replace(pat.begin(), pat.end(), ';', ' ');
            out += " --file-filter=" + shell_quote(desc + " | " + pat);
        }
        return out;
    }


    std::string kdialog_filter(const std::vector<std::pair<std::string, std::string>>& filters) {

        std::string combined;
        for (size_t i = 0; i < filters.size(); ++i) {

            if (i)
                combined += '\n';

            std::string pat = filters[i].second;
            std::replace(pat.begin(), pat.end(), ';', ' ');
            combined += pat + "|" + filters[i].first;
        }
        if (combined.empty()) 
            combined = "*|All files";

        return shell_quote(combined);
    }


    std::filesystem::path zenity_single(const std::string& title, bool dir, const std::vector<std::pair<std::string, std::string>>& filters) {

        std::string cmd = "zenity --file-selection --title=" + shell_quote(title);
        cmd += dir ? " --directory" : zenity_filters(filters);
        auto [code, out] = run_capture(cmd);
        return (code == 0 && !out.empty()) ? std::filesystem::path(out) : std::filesystem::path{};
    }


    std::vector<std::filesystem::path> zenity_multi(const std::string& title, const std::vector<std::pair<std::string, std::string>>& filters) {

        // zenity default separator is '|'  — paths with '|' are pathological, ignore that.
        std::string cmd = "zenity --file-selection --multiple --title=" + shell_quote(title) + zenity_filters(filters);

        auto [code, out] = run_capture(cmd);
        if (code != 0 || out.empty()) 
            return {};

        std::vector<std::filesystem::path> results;
        size_t start = 0;
        while (start <= out.size()) {

            size_t sep = out.find('|', start);
            if (sep == std::string::npos) 
                sep = out.size();

            if (sep > start)
                results.emplace_back(out.substr(start, sep - start));

            if (sep == out.size())
                break;

            start = sep + 1;
        }
        return results;
    }


    std::filesystem::path kdialog_single(const std::string& title, bool dir, const std::vector<std::pair<std::string, std::string>>& filters) {

        std::string cmd = "kdialog --title " + shell_quote(title);
        cmd += dir ? " --getexistingdirectory ." : " --getopenfilename . " + kdialog_filter(filters);
        auto [code, out] = run_capture(cmd);
        return (code == 0 && !out.empty()) ? std::filesystem::path(out) : std::filesystem::path{};
    }


    std::vector<std::filesystem::path> kdialog_multi(const std::string& title, const std::vector<std::pair<std::string, std::string>>& filters) {

        std::string cmd = "kdialog --title " + shell_quote(title) + " --getopenfilename . " + kdialog_filter(filters) + " --multiple --separate-output";
        auto [code, out] = run_capture(cmd);
        if (code != 0 || out.empty())
            return {};
        return split_lines(out);
    }


    [[nodiscard]] std::filesystem::path default_file_dialog(const std::string& title, bool select_directory,
        const std::vector<std::pair<std::string, std::string>>& filters) {

        #if defined(PLATFORM_WINDOWS)

            // Initialize COM if needed (required for modern dialogs)
            HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            bool comInitialized = SUCCEEDED(hr);

            std::filesystem::path result;

            if (select_directory) {
                // Use folder selection dialog
                IFileOpenDialog* pFileOpen;
                hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));

                if (SUCCEEDED(hr)) {
                    // Set options for folder selection
                    DWORD dwOptions;
                    pFileOpen->GetOptions(&dwOptions);
                    pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS);

                    // Set title
                    std::wstring wtitle(title.begin(), title.end());
                    pFileOpen->SetTitle(wtitle.c_str());

                    // Show the dialog
                    hr = pFileOpen->Show(NULL);
                    if (SUCCEEDED(hr)) {
                        IShellItem* pItem;
                        hr = pFileOpen->GetResult(&pItem);
                        if (SUCCEEDED(hr)) {
                            PWSTR pszFilePath;
                            hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                            if (SUCCEEDED(hr)) {
                                result = pszFilePath;
                                CoTaskMemFree(pszFilePath);
                            }
                            pItem->Release();
                        }
                    }
                    pFileOpen->Release();
                }
            } else {
                // Use modern file open dialog
                IFileOpenDialog* pFileOpen;
                hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));

                if (SUCCEEDED(hr)) {
                    // Set file types
                    if (!filters.empty()) {
                        std::vector<COMDLG_FILTERSPEC> fileTypes;
                        for (const auto& filter : filters) {
                            COMDLG_FILTERSPEC spec;
                            std::wstring desc(filter.first.begin(), filter.first.end());
                            std::wstring ext(filter.second.begin(), filter.second.end());

                            // Allocate memory for the strings
                            spec.pszName = _wcsdup(desc.c_str());
                            spec.pszSpec = _wcsdup(ext.c_str());
                            fileTypes.push_back(spec);
                        }
                        pFileOpen->SetFileTypes(static_cast<UINT>(fileTypes.size()), fileTypes.data());

                        // Free allocated memory
                        for (auto& spec : fileTypes) {
                            free(const_cast<wchar_t*>(spec.pszName));
                            free(const_cast<wchar_t*>(spec.pszSpec));
                        }
                    }

                    // Set title
                    std::wstring wtitle(title.begin(), title.end());
                    pFileOpen->SetTitle(wtitle.c_str());

                    // Show the dialog
                    hr = pFileOpen->Show(NULL);
                    if (SUCCEEDED(hr)) {
                        IShellItem* pItem;
                        hr = pFileOpen->GetResult(&pItem);
                        if (SUCCEEDED(hr)) {
                            PWSTR pszFilePath;
                            hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                            if (SUCCEEDED(hr)) {
                                result = pszFilePath;
                                CoTaskMemFree(pszFilePath);
                            }
                            pItem->Release();
                        }
                    }
                    pFileOpen->Release();
                }
            }

            if (comInitialized) {
                CoUninitialize();
            }

            return result;

        #elif defined(PLATFORM_LINUX)

            switch (detect_backend()) {
                case backend::zenity:  return zenity_single (title, select_directory, filters);
                case backend::kdialog: return kdialog_single(title, select_directory, filters);
                case backend::none:    break;
            }
            LOG(error, "No file-dialog backend found. Install `zenity` or `kdialog`.");
            return {};

        #else

            return {};

        #endif
    }


    [[nodiscard]] std::vector<std::filesystem::path> default_file_dialog_multi(const std::string& title, 
        const std::vector<std::pair<std::string, std::string>>& filters) {

        #if defined(PLATFORM_WINDOWS)

            HWND hwndOwner = GetActiveWindow();
            OPENFILENAMEW ofn;
            std::vector<std::wstring> filterW;
            for (auto& f : filters)
                filterW.push_back(std::wstring(f.first.begin(), f.first.end()) + L'\0' +
                                std::wstring(f.second.begin(), f.second.end()) + L'\0');
            // Concatenate filters and double-null terminate
            std::wstring filterStr;
            for (auto& s : filterW) filterStr += s;
            filterStr += L'\0';

            wchar_t szFiles[4096] = { 0 };
            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize  = sizeof(ofn);
            ofn.hwndOwner    = hwndOwner;
            ofn.lpstrFile    = szFiles;
            ofn.nMaxFile     = sizeof(szFiles) / sizeof(wchar_t);
            ofn.lpstrFilter  = filterStr.c_str();
            ofn.nFilterIndex = 1;
            ofn.Flags        = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST
                            | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
            std::wstring wtitle(title.begin(), title.end());
            ofn.lpstrTitle   = wtitle.c_str();

            if (GetOpenFileNameW(&ofn)) {
                std::vector<std::filesystem::path> results;
                std::wstring dir = szFiles;
                wchar_t* p = szFiles + dir.size() + 1;
                if (*p == L'\0') {
                    // Only one file selected
                    results.emplace_back(dir);
                } else {
                    // Multiple files: parse names after directory
                    while (*p) {
                        results.emplace_back(std::filesystem::path(dir) / p);
                        p += wcslen(p) + 1;
                    }
                }
                return results;
            }
            return {};


        #elif defined(PLATFORM_LINUX)

            switch (detect_backend()) {
                case backend::zenity:  return zenity_multi (title, filters);
                case backend::kdialog: return kdialog_multi(title, filters);
                case backend::none:    break;
            }
            LOG(error, "No file-dialog backend found. Install `zenity` or `kdialog`.");
            return {};

        #else

            return {};

        #endif
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // STATIC VARIABLES ================================================================================================

    static file_dialog_functions s_file_dialog = {

        default_file_dialog,
        default_file_dialog_multi,
    };

    // FUNCTION IMPLEMENTATION =========================================================================================

    [[nodiscard]] std::filesystem::path file_dialog(const std::string& title, bool select_directory,
        const std::vector<std::pair<std::string, std::string>>& filters) {
            
        return s_file_dialog.file_dialog(title, select_directory, filters);
    }


    [[nodiscard]] std::vector<std::filesystem::path> file_dialog_multi(const std::string& title,
        const std::vector<std::pair<std::string, std::string>>& filters) {

        return s_file_dialog.file_dialog_multi(title, filters);
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
